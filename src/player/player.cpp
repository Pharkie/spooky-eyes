// The player: on the Arduino loop task, plays the saved show (Show mode) or blinks idly between button
// presses (Buttons mode), and swaps in what phones (via the web server task) or the USB serial port ask
// for.
//
//   Show --(press, or the button page opens)--> Buttons --(2 min with no press or heartbeat)--> Show
//                                                       \--("start the show")--> Show
//
// Threads: requests arrive on any task and are copied into one pending slot under a critical section
// (newest wins). The loop task polls atomic flags in Engine::wait(), which throws EffectInterrupted,
// so the effect unwinds at once and loop() picks up the new request. Only the loop task touches the
// eyes, the mouth LED, the serial input and the show itself.

#include "player/player.h"

#include <atomic>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include "core/constants.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/show_store.h"
#include "core/text.h"
#include "display/sprites.h"
#include "effects/registry.h"
#include "effects/show.h"
#include "hardware/max7219_eyes.h"
#include "hardware/mouth_led.h"
#include "player/device_engine.h"

namespace
{
    using player::Mode;

    constexpr size_t kTextSize = kMaxWordsLength + 1; // typed words, announcements and the show message
    constexpr size_t kAnnounceQueueSize = 3;
    constexpr size_t kSerialLineSize = kMaxWordsLength + 16; // room for "say " and the words
    constexpr const char *kBlinkId = "blink";                 // idle eyes in Buttons mode
    constexpr const char *kWordsId = "words";                 // status effectId for words
    constexpr uint32_t kFallbackIdleMs = PumpkinTime::Seconds(1);
    constexpr int kNoBlock = -1;

    enum class RequestKind : uint8_t
    {
        None,
        Effect,       // play a chosen effect (Buttons mode)
        Words,        // scroll someone's words (Buttons mode)
        EnterButtons, // pause the show: idle eyes until the next press
        StartShow,    // the show, from the top
    };

    // The mode a request leads to
    Mode modeFor(RequestKind kind)
    {
        return kind == RequestKind::StartShow ? Mode::Show : Mode::Buttons;
    }

    struct Request
    {
        RequestKind kind = RequestKind::None;
        const EffectInfo *effect = nullptr;
        char words[kTextSize] = {};
    };

    // What may cut short whatever is playing, besides a new request (which cuts short anything)
    enum class CutBy : uint8_t
    {
        Nothing,      // someone's chosen effect, or an announcement: they finish first
        Announcement, // idle eyes, and the show's blocks and message
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

    // Buttons mode's idle timer: the last press, heartbeat, or chosen effect finishing (millis())
    uint32_t buttonsActiveMs = 0;

    // The status. mode is only changed by the loop task (and always under the lock), so the loop task
    // may read it without the lock.
    Mode mode = Mode::Show; // the show is what a pumpkin does at switch-on
    const EffectInfo *statusEffect = nullptr;
    bool statusIsWords = false;
    char statusWords[kTextSize] = {};
    int statusBlock = kNoBlock;

    std::atomic<bool> hasPending{false};      // checked by wait() every few ms without taking the lock
    std::atomic<bool> hasAnnouncement{false}; // an announcement is queued
    std::atomic<bool> showDirty{true};        // the saved show changed (or hasn't been loaded yet)
    std::atomic<CutBy> cutBy{CutBy::Nothing};
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

    Show show;                // the saved show (never empty once loaded)
    size_t nextBlock = 0;     // in Show mode, the block to play next
    bool passStarting = true; // the show message is due before the next block 0
    bool nameDirty = false;
    const EffectInfo *blinkEffect = nullptr;

    char serialLine[kSerialLineSize] = {};
    size_t serialLength = 0;
    bool serialOverflow = false;

    void housekeeping();

    // A request interrupts anything. An announcement (e.g. the address after a BOOT press) also cuts a
    // show block or the idle eyes short, and a newly saved show cuts the old one short, but neither
    // ever cuts short someone's chosen effect.
    bool newRequestWaiting()
    {
        if (hasPending.load())
            return true;
        switch (cutBy.load())
        {
        case CutBy::Nothing:
            return false;
        case CutBy::Announcement:
            return hasAnnouncement.load();
        }
        return false;
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

    // Where the player is heading: the pending request's mode if there is one, else the current mode.
    // Call with the lock held.
    Mode headingLocked()
    {
        return hasPending.load() ? modeFor(pending.kind) : mode;
    }

    void setPendingLocked(RequestKind kind, const EffectInfo *effect, const char *words)
    {
        pending.kind = kind;
        pending.effect = effect;
        strlcpy(pending.words, words ? words : "", sizeof pending.words);
        hasPending.store(true);
    }

    // A press: play this (in Buttons mode), and restart the idle timer
    void pressed(RequestKind kind, const EffectInfo *effect, const char *words)
    {
        const uint32_t now = millis();
        Locked locked;
        buttonsActiveMs = now;
        setPendingLocked(kind, effect, words);
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

    // Update what the web page shows, and tell it if anything changed (or always, for a fresh press)
    void setStatus(Mode newMode, const EffectInfo *effect, const char *words, int block, bool always = false)
    {
        bool changed;
        {
            Locked locked;
            const bool isWords = words != nullptr;
            changed = mode != newMode || statusEffect != effect || statusIsWords != isWords || statusBlock != block ||
                      (isWords && strcmp(statusWords, words) != 0);
            mode = newMode;
            statusEffect = effect;
            statusIsWords = isWords;
            strlcpy(statusWords, isWords ? words : "", sizeof statusWords);
            statusBlock = block;
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

    // Ready for the next effect: sturdy chip settings and the normal brightness
    void freshStart()
    {
        max7219_eyes::reinit();
        engine.setBrightness(engine.maxBright());
    }

    // Play something, stopping quietly if it's interrupted (see newRequestWaiting). Nothing escapes
    // loop(): loop() picks up whatever interrupted it next.
    template <typename Fn>
    void runGuarded(CutBy cuttableBy, Fn &&fn)
    {
        freshStart();
        cutBy.store(cuttableBy);
        try
        {
            fn();
        }
        catch (const EffectInterrupted &)
        {
            // something new is waiting: loop() picks it up next
        }
        catch (const std::exception &e)
        {
            LOG("PLAYER", "Effect failed: %s", e.what());
        }
        catch (...)
        {
            LOG("PLAYER", "Effect failed");
        }
        cutBy.store(CutBy::Nothing);
        tidyUp();
    }

    // --- The show ---

    // Read the saved show (or the original one). Slow-ish (flash and JSON), so only between effects.
    void loadShow()
    {
        bool isDefault = true;
        Show loaded = show_store::load(isDefault);
        std::string error;
        if (!checkShow(loaded, error))
        {
            LOG("PLAYER", "Can't play the saved show (%s): playing the original one", error.c_str());
            loaded = defaultShow();
            isDefault = true;
        }
        show = std::move(loaded);
        LOG("PLAYER", "Show: %u blocks%s", static_cast<unsigned>(show.size()), isDefault ? " (the original)" : "");
    }

    // The show from the top, next time it plays
    void rewindShow()
    {
        nextBlock = 0;
        passStarting = true;
    }

    void enterShow()
    {
        rewindShow();
        // Always tell the page: buttonsTimedOut() may already have switched the mode under the lock
        setStatus(Mode::Show, nullptr, nullptr, kNoBlock, true);
    }

    // The timed message (e.g. which WiFi to join) at the start of a pass. False if there isn't one.
    bool playShowMessage()
    {
        char message[kTextSize];
        {
            Locked locked;
            if (showMessageForMs && millis() - showMessageSetMs >= showMessageForMs)
                showMessage[0] = '\0'; // its time is up
            strlcpy(message, showMessage, sizeof message);
        }
        if (!message[0])
            return false;
        setStatus(Mode::Show, nullptr, nullptr, kNoBlock);
        runGuarded(CutBy::Announcement, [&] { ::scrollWords(engine, message); });
        return true;
    }

    // One step of the show: the message at the start of a pass, or the next block. Back to loop() after
    // each, so requests and announcements come first.
    void showStep()
    {
        if (show.empty())
            show = defaultShow(); // loadShow() never leaves it empty, but never index an empty show
        if (nextBlock >= show.size())
            rewindShow();
        if (nextBlock == 0 && passStarting)
        {
            passStarting = false;
            if (playShowMessage())
                return;
        }

        const size_t index = nextBlock++;
        const ShowBlock &block = show[index];
        if (block.kind == ShowBlock::Kind::Words)
            setStatus(Mode::Show, nullptr, block.value.c_str(), static_cast<int>(index));
        else
            setStatus(Mode::Show, findEffect(block.value.c_str()), nullptr, static_cast<int>(index));
        // Cut short by an announcement, it carries on with the next block; by a new show, from the top
        runGuarded(CutBy::Announcement, [&] { playBlock(engine, block); });
    }

    // --- Buttons mode ---

    // The idle timer has run out: back to the show. Checked and switched under the lock, so a heartbeat
    // arriving at the same moment either keeps Buttons mode or asks for it again.
    bool buttonsTimedOut()
    {
        const uint32_t now = millis();
        Locked locked;
        if (hasPending.load())
            return false; // a request decides instead
        // Signed, as a heartbeat may have set the timer just after `now` was read
        if (static_cast<int32_t>(now - buttonsActiveMs) < static_cast<int32_t>(kButtonsIdleTimeoutMs))
            return false;
        mode = Mode::Show;
        return true;
    }

    // A chosen effect or words have finished (or been interrupted): idle eyes until the next press
    void chosenFinished()
    {
        const uint32_t now = millis();
        {
            Locked locked;
            buttonsActiveMs = now; // the idle timer starts when the effect ends, not when it was pressed
        }
        if (!hasPending.load()) // no flicker of "idle" if the next effect is about to start
            setStatus(Mode::Buttons, nullptr, nullptr, kNoBlock);
    }

    void playChosen(const EffectInfo &effect)
    {
        LOG("PLAYER", "Playing %s", effect.id);
        setStatus(Mode::Buttons, &effect, nullptr, kNoBlock, true);
        runGuarded(CutBy::Nothing, [&] { playEffect(engine, effect); });
        chosenFinished();
    }

    void playChosenWords(const char *words)
    {
        LOG("PLAYER", "Scrolling \"%s\"", words);
        setStatus(Mode::Buttons, nullptr, words, kNoBlock, true);
        runGuarded(CutBy::Nothing, [&] {
            engine.say(words);
            ::scrollWords(engine, words);
        });
        chosenFinished();
    }

    // Idle eyes between button presses
    void idle()
    {
        runGuarded(CutBy::Announcement, [&] {
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

    // --- What loop() does ---

    void handle(const Request &request)
    {
        switch (request.kind)
        {
        case RequestKind::Effect:
            if (request.effect)
                playChosen(*request.effect);
            break;
        case RequestKind::Words:
            playChosenWords(request.words);
            break;
        case RequestKind::EnterButtons:
            LOG("PLAYER", "Buttons mode: the show is paused");
            setStatus(Mode::Buttons, nullptr, nullptr, kNoBlock);
            break;
        case RequestKind::StartShow:
            LOG("PLAYER", "Starting the show");
            enterShow();
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
        setStatus(mode, nullptr, nullptr, kNoBlock);
        runGuarded(CutBy::Nothing, [&] { ::scrollWords(engine, message); });
        return true;
    }

    // Nothing new asked for: the show's next step, or more idle blinking until it's been quiet too long
    void carryOn()
    {
        if (mode == Mode::Buttons && buttonsTimedOut())
        {
            LOG("PLAYER", "All quiet: back to the show");
            enterShow();
        }

        if (mode == Mode::Show)
            showStep();
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
        Serial.println("  show          start the show (from the top)");
        Serial.println("  buttons       pause the show for button presses (back to the show after 2 minutes idle)");
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
            LOG("SERIAL", "OK, the show");
            player::startShow();
        }
        else if (strcmp(line, "buttons") == 0)
        {
            LOG("SERIAL", "OK, buttons mode (back to the show after %u minutes with no presses)",
                static_cast<unsigned>(kButtonsIdleTimeoutMs / PumpkinTime::Minutes(1)));
            player::enterButtons();
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
        engine.show(Eyes::straight, Eyes::straight); // straight-ahead eyes until the first loop() starts the show

        blinkEffect = findEffect(kBlinkId);
        if (!blinkEffect)
            LOG("PLAYER", "No \"%s\" effect: idle eyes will just stare", kBlinkId);

        // The saved show is read by the first loop() (showDirty starts true), still on this task
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
        if (showDirty.exchange(false))
        {
            // The show page saves as people edit, so a playing show isn't cut short or restarted: it
            // carries on from the next block of the new list (wrapping round if that's now past the end)
            loadShow();
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
        pressed(RequestKind::Effect, effect, nullptr);
        return true;
    }

    void scrollWords(const String &words)
    {
        char cleaned[kTextSize];
        cleanText(words.c_str(), cleaned);
        if (cleaned[0])
            pressed(RequestKind::Words, nullptr, cleaned);
    }

    void startShow()
    {
        Locked locked;
        if (headingLocked() == Mode::Show)
            return; // already showing: don't start it again from the top
        setPendingLocked(RequestKind::StartShow, nullptr, nullptr);
    }

    void enterButtons()
    {
        const uint32_t now = millis();
        Locked locked;
        buttonsActiveMs = now;
        if (headingLocked() == Mode::Buttons)
            return; // already in Buttons mode (or about to be): that was just "still here"
        setPendingLocked(RequestKind::EnterButtons, nullptr, nullptr);
    }

    void showChanged()
    {
        showDirty.store(true); // the loop task picks it up between blocks
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
        Mode currentMode;
        const EffectInfo *effect;
        bool isWords;
        char words[kTextSize];
        int block;
        {
            Locked locked;
            currentMode = mode;
            effect = statusEffect;
            isWords = statusIsWords;
            strlcpy(words, statusWords, sizeof words);
            block = statusBlock;
        }

        Status result;
        result.mode = currentMode;
        result.block = currentMode == Mode::Show ? block : kNoBlock;
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
