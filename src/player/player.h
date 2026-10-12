#pragma once
// Runs the eyes. The normal show loops on the Arduino loop task; web requests (from the async web
// server task) ask for something else, which interrupts whatever is playing via EffectInterrupted.
//
//   normal show --(button)--> effect --(done)--> waiting --(2 min idle or "back to show")--> normal show
//                                \--(another button)--> next effect straight away

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

    // Go back to the normal show (does nothing if it's already running)
    void backToShow();

    // Scroll a message once, then carry on (the address at boot, or after a BOOT press). It cuts the normal
    // show short, but waits for a chosen effect to finish.
    void announce(const String &message);

    // A message scrolled at the start of every normal show pass ("" = none), for durationMs (0 = until
    // changed). Used in setup mode to tell people which WiFi to join.
    void setShowMessage(const String &message, uint32_t durationMs = 0);

    // Settings changed (brightness, mouth LED, name): apply them
    void settingsChanged();

    // --- What's happening, for the web page ---

    struct Status
    {
        bool inShow;      // the normal show is running
        String effectId;  // the effect playing now ("" if none, or "words" for typed words)
        String label;     // e.g. "Creeper", or the typed words
    };
    Status status();

    // Called (from the effects task) whenever the talking pumpkin says something, and whenever the
    // status changes. The web server forwards these to phones as server-sent events.
    using SayListener = void (*)(const char *words);
    using StatusListener = void (*)();
    void onSay(SayListener listener);
    void onStatusChange(StatusListener listener);
}
