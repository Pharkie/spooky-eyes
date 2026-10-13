#pragma once
// Runs the eyes, on the Arduino loop task. Two modes:
//   Show     plays the saved show (a list of blocks: effects and words) round and round. The default,
//            and what a pumpkin does at switch-on.
//   Buttons  the show pauses and the eyes blink idly; each press plays one effect (interrupting whatever
//            is playing, via EffectInterrupted). The button page asks for this when it opens, and keeps
//            asking every kButtonsHeartbeatMs while it's on screen. kButtonsIdleTimeoutMs after the last
//            press or heartbeat (e.g. the phone went to sleep), it goes back to the show by itself.
// Requests come from the async web server task (and USB serial) and are handed over safely.

#include <Arduino.h>

namespace player
{
    // Set up the eyes and mouth hardware. Call once from setup(), after settings::begin().
    void begin();

    // Run whatever should be playing. Call from loop(); returns after each effect or show pass.
    void loop();

    // --- Requests (safe to call from any task) ---

    // Play an effect from the registry. False if the id is unknown.
    bool play(const String &effectId);

    // Scroll someone's own words (trimmed to kMaxWordsLength, only characters the font has)
    void scrollWords(const String &words);

    // Show mode: carry on with the saved show where it was paused (the block a button press cut short
    // plays again), or with fromTop from its first block (the show page's "Play the show", after edits).
    // Does nothing if it's already playing, unless fromTop.
    void startShow(bool fromTop = false);

    // Buttons mode (pause the show, idle eyes), or "still here" if already in it. Safe to call often.
    void enterButtons();

    // The saved show changed: use it from now on. A playing show carries on from its next block (the show
    // page saves as people edit, so the eyes mustn't jump back to the start at every change).
    void showChanged();

    // Scroll a message once, then carry on (the address at boot, or after a BOOT press). It cuts a show
    // block short (the show then carries on from the next block), but waits for a chosen effect to finish.
    void announce(const String &message);

    // A message scrolled at the start of the next `passes` passes of the show ("" = none; passes < 0 =
    // every pass until changed). Used in setup mode to tell people which WiFi to join.
    void setShowMessage(const String &message, int passes = -1);

    // Settings changed (brightness, mouth LED, name): apply them
    void settingsChanged();

    // --- What's happening, for the web page ---

    enum class Mode : uint8_t
    {
        Show,
        Buttons,
    };

    struct Status
    {
        Mode mode;
        String effectId;  // the effect playing now ("" if none, or "words" for words)
        String label;     // e.g. "Creeper", or the words
        int block;        // in Show mode, which block of the show is playing (0-based); -1 otherwise
    };
    Status status();

    // Called (from the effects task) whenever the talking pumpkin says something, and whenever the
    // status changes. The web server forwards these to phones as server-sent events.
    using SayListener = void (*)(const char *words);
    using StatusListener = void (*)();
    void onSay(SayListener listener);
    void onStatusChange(StatusListener listener);
}
