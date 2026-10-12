// The player: runs the normal show on the Arduino loop task, and swaps in effects that phones (via the
// web server task) or the USB serial port ask for.
//
// Threads: requests arrive on any task and are copied into one pending slot under a critical section
// (newest wins). The loop task polls an atomic flag in Engine::wait(), which throws EffectInterrupted,
// so the effect unwinds at once and loop() picks up the new request. Only the loop task touches the
// eyes, the mouth LED and the serial input.

#include "player/player.h"

#include <atomic>
#include <cstring>
#include <exception>
#include "core/constants.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/text.h"
#include "display/sprites.h"
#include "effects/registry.h"
#include "hardware/max7219_eyes.h"
#include "hardware/mouth_led.h"
#include "player/device_engine.h"

namespace
{
    constexpr size_t kTextSize = kMaxWordsLength + 1; // typed words, announcements and the show message
    constexpr size_t kAnnounceQueueSize = 3;
    constexpr size_t kSerialLineSize = kMaxWordsLength + 16; // room for "say " and the words
    constexpr const char *kBlinkId = "blink";                 // idle eyes while waiting for the next press
    constexpr const char *kWordsId = "words";                 // status effectId for typed words
    constexpr uint32_t kFallbackIdleMs = PumpkinTime::Seconds(1);

    enum class RequestKind : uint8_t
    {
        None,
        Effect,
        Words,
        BackToShow,
    };

    struct Request
    {
        RequestKind kind = RequestKind::None;
        const EffectInfo *effect = nullptr;
        char words[kTextSize] = {};
    };

    // --- Shared between tasks, guarded by lock. Only quick copies inside: no Strings, no logging. ---

    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

    Request pending;
    char announcements[kAnnounceQueueSize][kTextSize] = {};
    size_t announceFirst = 0;
    size_t announceCount = 0;
    char showMessage[kTextSize] = {};
    uint32_t showMessageSetMs = 0;
    uint32_t showMessageForMs = 0; // 0 = until changed

    bool statusInShow = true; // the show is what starts first
    const EffectInfo *statusEffect = nullptr;
    bool statusIsWords = false;
    char statusWords[kTextSize] = {};

    std::atomic<bool> hasPending{false}; // checked by wait() every few ms without taking the lock
    std::atomic<bool> hasAnnouncement{false}; // an announcement is queued
    std::atomic<bool> showRunning{false};     // a normal show pass is playing
    std::atomic<bool> settingsDirty{false};
    std::atomic<player::SayListener> sayListener{nullptr};
    std::atomic<player::StatusListener> statusListener{nullptr};

    class Locked
    {
    public:
        Locked() { portENTER_CRITICAL(&lock); }
        ~Locked() { portEXIT_CRITICAL(&lock); }
        Locked(const Locked &) = delete;
        Locked &operator=(const Locked &) = delete;
    };

    // --- Loop task only ---

    enum class Mode : uint8_t
    {
        Show,    // the normal show
        Waiting, // a button effect has finished: idle blinking until the next press or the timeout
    };

    Mode mode = Mode::Show;
    uint32_t waitingSince = 0;
    bool nameDirty = false;
    const EffectInfo *blinkEffect = nullptr;

    char serialLine[kSerialLineSize] = {};
    size_t serialLength = 0;
    bool serialOverflow = false;

    void housekeeping();

    // A request interrupts anything. An announcement (e.g. the address after a BOOT press) also cuts the
    // normal show short, as the show just starts again afterwards, but never someone's chosen effect.
    bool newRequestWaiting()
    {
        return hasPending.load() || (showRunning.load() && hasAnnouncement.load());
    }

    void forwardSay(const char *words)
    {
        const player::SayListener listener = sayListener.load();
        if (listener)
            listener(words);
    }

    DeviceEngine engine(DeviceEngine::Hooks{housekeeping, newRequestWaiting, forwardSay});

    // Words the font can show (text::plain's rules), at most kMaxWordsLength characters
    void cleanText(const char *in, char (&out)[kTextSize])
    {
        text::plain(in, out, sizeof out);
    }

    void setPending(RequestKind kind, const EffectInfo *effect, const char *words)
    {
        Locked locked;
        pending.kind = kind;
        pending.effect = effect;
        strlcpy(pending.words, words ? words : "", sizeof pending.words);
        hasPending.store(true);
    }

    bool takeRequest(Request &out)
    {
        Locked locked;
        if (!hasPending.load())
            return false;
        out = pending;
        pending.kind = RequestKind::None;
        hasPending.store(false);
        return true;
    }

    bool takeAnnouncement(char (&out)[kTextSize])
    {
        Locked locked;
        if (announceCount == 0)
            return false;
        strlcpy(out, announcements[announceFirst], sizeof out);
        announceFirst = (announceFirst + 1) % kAnnounceQueueSize;
        announceCount--;
        hasAnnouncement.store(announceCount > 0);
        return true;
    }

    // Update what the web page shows, and tell it if anything changed (or always, for a fresh start)
    void setStatus(bool inShow, const EffectInfo *effect, const char *words, bool always = false)
    {
        bool changed;
        {
            Locked locked;
            const bool isWords = words != nullptr;
            changed = statusInShow != inShow || statusEffect != effect || statusIsWords != isWords ||
                      (isWords && strcmp(statusWords, words) != 0);
            statusInShow = inShow;
            statusEffect = effect;
            statusIsWords = isWords;
            strlcpy(statusWords, isWords ? words : "", sizeof statusWords);
        }
        if (changed || always)
        {
            const player::StatusListener listener = statusListener.load();
            if (listener)
                listener();
        }
    }

    // Put things right after an effect, finished or interrupted: explode() and friends raise the
    // brightness for flashes, and the blink drops it to 0 for a moment.
    void tidyUp()
    {
        engine.setBrightness(engine.maxBright());
        mouth_led::setEnabled(mouth_led::enabled()); // lights a colour if it's on but dark
    }

    // Run part of the show, stopping quietly if a new request interrupts it. Nothing escapes loop().
    template <typename Fn>
    void runGuarded(Fn &&fn)
    {
        try
        {
            fn();
        }
        catch (const EffectInterrupted &)
        {
            // a new request is waiting: loop() picks it up next
        }
        catch (const std::exception &e)
        {
            LOG("PLAYER", "Effect failed: %s", e.what());
        }
        catch (...)
        {
            LOG("PLAYER", "Effect failed");
        }
        tidyUp();
    }

    // Ready for the next effect: sturdy chip settings and the normal brightness
    void freshStart()
    {
        max7219_eyes::reinit();
        engine.setBrightness(engine.maxBright());
    }

    void startWaiting()
    {
        mode = Mode::Waiting;
        waitingSince = millis();
        if (!hasPending.load()) // no flicker of "waiting" if the next effect is about to start
            setStatus(false, nullptr, nullptr);
    }

    void playEffect(const EffectInfo &effect)
    {
        LOG("PLAYER", "Playing %s", effect.id);
        setStatus(false, &effect, nullptr, true);
        freshStart();
        runGuarded([&] {
            if (effect.phrase)
                engine.say(effect.phrase);
            effect.run(engine);
        });
        startWaiting();
    }

    void playWords(const char *words)
    {
        LOG("PLAYER", "Scrolling \"%s\"", words);
        setStatus(false, nullptr, words, true);
        freshStart();
        runGuarded([&] {
            engine.say(words);
            ::scrollWords(engine, words);
        });
        startWaiting();
    }

    void handle(const Request &request)
    {
        switch (request.kind)
        {
        case RequestKind::Effect:
            if (request.effect)
                playEffect(*request.effect);
            break;
        case RequestKind::Words:
            playWords(request.words);
            break;
        case RequestKind::BackToShow:
            LOG("PLAYER", "Back to the normal show");
            mode = Mode::Show;
            setStatus(true, nullptr, nullptr);
            break;
        case RequestKind::None:
            break;
        }
    }

    bool playAnnouncement()
    {
        char message[kTextSize];
        if (!takeAnnouncement(message))
            return false;
        LOG("PLAYER", "Announcing \"%s\"", message);
        freshStart();
        runGuarded([&] { ::scrollWords(engine, message); });
        return true;
    }

    void runShowPass()
    {
        char message[kTextSize];
        {
            Locked locked;
            if (showMessageForMs && millis() - showMessageSetMs >= showMessageForMs)
                showMessage[0] = '\0'; // its time is up
            strlcpy(message, showMessage, sizeof message);
        }
        setStatus(true, nullptr, nullptr);
        freshStart();
        showRunning.store(true);
        runGuarded([&] {
            if (message[0])
                ::scrollWords(engine, message);
            ::normalShow(engine);
        });
        showRunning.store(false);
    }

    // Idle eyes between button presses
    void idle()
    {
        runGuarded([&] {
            if (blinkEffect)
            {
                blinkEffect->run(engine);
            }
            else
            {
                engine.show(Eyes::straight, Eyes::straight);
                engine.wait(kFallbackIdleMs);
            }
        });
    }

    // Nothing new asked for: the next show pass, or more idle blinking until it's been quiet too long
    void carryOn()
    {
        if (mode == Mode::Waiting && millis() - waitingSince >= kButtonIdleTimeoutMs)
        {
            LOG("PLAYER", "All quiet: back to the normal show");
            mode = Mode::Show;
        }

        if (mode == Mode::Show)
            runShowPass();
        else
            idle();
    }

    // --- USB serial commands, for testing without WiFi ---

    void printHelp()
    {
        Serial.println("Serial commands:");
        Serial.println("  list          the effects");
        Serial.println("  play <id>     play one, e.g. play creeper");
        Serial.println("  say <words>   scroll some words across the eyes");
        Serial.println("  show          back to the normal show");
        Serial.println("  name <name>   rename the pumpkin, e.g. name Jack's Pumpkin (restart to rename its WiFi)");
        Serial.println("  help          this list");
    }

    void listEffects()
    {
        size_t count = 0;
        const EffectInfo *effects = allEffects(count);
        Serial.printf("%u effects:\n", static_cast<unsigned>(count));
        for (size_t i = 0; i < count; i++)
            Serial.printf("  %-12s %s\n", effects[i].id, effects[i].label);
    }

    void runCommand(char *line)
    {
        // Split "command rest of line", ignoring spaces around both
        while (*line == ' ')
            line++;
        char *end = line + strlen(line);
        while (end > line && end[-1] == ' ')
            *--end = '\0';
        char *argument = strchr(line, ' ');
        if (argument)
        {
            *argument++ = '\0';
            while (*argument == ' ')
                argument++;
        }
        else
        {
            argument = end;
        }

        if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0)
        {
            printHelp();
        }
        else if (strcmp(line, "list") == 0)
        {
            listEffects();
        }
        else if (strcmp(line, "play") == 0)
        {
            if (player::play(String(argument)))
                LOG("SERIAL", "OK, playing %s", argument);
            else
                LOG("SERIAL", "No effect called \"%s\". Type list to see them.", argument);
        }
        else if (strcmp(line, "say") == 0)
        {
            if (*argument)
                player::scrollWords(String(argument));
            else
                LOG("SERIAL", "Say what? e.g. say Happy Halloween");
        }
        else if (strcmp(line, "name") == 0)
        {
            Settings updated = settings::get();
            updated.pumpkinName = argument;
            String error;
            if (settings::save(updated, error))
            {
                player::settingsChanged();
                LOG("SERIAL", "OK, it's now \"%s\" (web address %s.local). Restart to rename its WiFi.",
                    settings::get().pumpkinName.c_str(), settings::hostnameFor(settings::get().pumpkinName).c_str());
            }
            else
            {
                LOG("SERIAL", "Couldn't rename it: %s", error.c_str());
            }
        }
        else if (strcmp(line, "show") == 0)
        {
            LOG("SERIAL", "OK, back to the normal show");
            player::backToShow();
        }
        else if (*line)
        {
            LOG("SERIAL", "Unknown command \"%s\". Type help.", line);
        }
    }

    // Collect serial input a line at a time, without ever waiting for it
    void readSerial()
    {
        while (Serial.available() > 0)
        {
            const int c = Serial.read();
            if (c < 0)
                break;
            if (c == '\n' || c == '\r')
            {
                if (serialOverflow)
                {
                    LOG("SERIAL", "That line was too long");
                }
                else if (serialLength > 0)
                {
                    serialLine[serialLength] = '\0';
                    runCommand(serialLine);
                }
                serialLength = 0;
                serialOverflow = false;
            }
            else if (serialLength + 1 < sizeof serialLine)
            {
                serialLine[serialLength++] = static_cast<char>(c);
            }
            else
            {
                serialOverflow = true;
            }
        }
    }

    // Runs on every slice of Engine::wait() and at the top of loop()
    void housekeeping()
    {
        readSerial();
        if (settingsDirty.exchange(false))
        {
            engine.refreshSettings(false); // brightness, gap, mouth LED: straight away
            engine.setBrightness(engine.maxBright());
            nameDirty = true; // the name waits until no effect could be scrolling it
        }
    }
}

namespace player
{
    void begin()
    {
        mouth_led::begin();
        max7219_eyes::begin();
        engine.refreshSettings(true); // also switches the mouth LED on or off
        engine.setBrightness(engine.maxBright());
        engine.show(Eyes::straight, Eyes::straight); // straight-ahead eyes while WiFi connects

        blinkEffect = findEffect(kBlinkId);
        if (!blinkEffect)
            LOG("PLAYER", "No \"%s\" effect: idle eyes will just stare", kBlinkId);

        size_t count = 0;
        allEffects(count);
        LOG("PLAYER", "Eyes on, %u effects. Type help on serial for test commands.",
            static_cast<unsigned>(count));
    }

    void loop()
    {
        housekeeping();
        if (nameDirty)
        {
            engine.refreshSettings(true);
            nameDirty = false;
        }

        // A new request first, then any announcement, then whatever the mode says
        Request request;
        if (takeRequest(request))
            handle(request);
        else if (!playAnnouncement())
            carryOn();

        // Effects already sleep in wait(), but make sure the idle task (and its watchdog) always gets a turn
        vTaskDelay(1);
    }

    bool play(const String &effectId)
    {
        const EffectInfo *effect = findEffect(effectId.c_str());
        if (!effect)
            return false;
        setPending(RequestKind::Effect, effect, nullptr);
        return true;
    }

    void scrollWords(const String &words)
    {
        char cleaned[kTextSize];
        cleanText(words.c_str(), cleaned);
        if (cleaned[0])
            setPending(RequestKind::Words, nullptr, cleaned);
    }

    void backToShow()
    {
        Locked locked;
        if (!hasPending.load() && statusInShow)
            return; // already in the show: don't start it again from the top
        pending.kind = RequestKind::BackToShow;
        pending.effect = nullptr;
        pending.words[0] = '\0';
        hasPending.store(true);
    }

    void announce(const String &message)
    {
        char cleaned[kTextSize];
        cleanText(message.c_str(), cleaned);
        if (!cleaned[0])
            return;
        Locked locked;
        if (announceCount == kAnnounceQueueSize)
        {
            // Full: drop the oldest, as the newest news matters most
            announceFirst = (announceFirst + 1) % kAnnounceQueueSize;
            announceCount--;
        }
        strlcpy(announcements[(announceFirst + announceCount) % kAnnounceQueueSize], cleaned, kTextSize);
        announceCount++;
        hasAnnouncement.store(true);
    }

    void setShowMessage(const String &message, uint32_t durationMs)
    {
        char cleaned[kTextSize];
        cleanText(message.c_str(), cleaned);
        const uint32_t now = millis();
        Locked locked;
        strlcpy(showMessage, cleaned, sizeof showMessage);
        showMessageSetMs = now;
        showMessageForMs = durationMs;
    }

    void settingsChanged()
    {
        settingsDirty.store(true);
    }

    Status status()
    {
        bool inShow;
        const EffectInfo *effect;
        bool isWords;
        char words[kTextSize];
        {
            Locked locked;
            inShow = statusInShow;
            effect = statusEffect;
            isWords = statusIsWords;
            strlcpy(words, statusWords, sizeof words);
        }

        Status result;
        result.inShow = inShow;
        if (effect)
        {
            result.effectId = effect->id;
            result.label = effect->label;
        }
        else if (isWords)
        {
            result.effectId = kWordsId;
            result.label = words;
        }
        return result;
    }

    void onSay(SayListener listener)
    {
        sayListener.store(listener);
    }

    void onStatusChange(StatusListener listener)
    {
        statusListener.store(listener);
    }
}
