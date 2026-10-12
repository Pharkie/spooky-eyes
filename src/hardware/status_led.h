#pragma once
// The SuperMini's blue status LED (kStatusLedPin), with Scribe Evolution's WiFi patterns:
//   fast blink = joining the home WiFi, heartbeat = on it, slow blink = setup WiFi on, off = no WiFi
//   (or WiFi switched off to save batteries)
// (Scribe's mini boards stayed solid on home WiFi; a short flash every 3 s saves battery.)
// Only the network task drives it, so there's no locking.

#include <cstdint>

namespace status_led
{
    enum class Pattern : uint8_t
    {
        Off,
        FastBlink, // 250 ms on, 250 ms off
        SlowBlink, // 1 s on, 1 s off
        Heartbeat, // 50 ms flash every 3 s
    };

    // Set the pin up, LED off. Call once before the others.
    void begin();

    // Choose the pattern. Blinking starts with the LED on.
    void set(Pattern pattern);

    // Run the blinking: call every 10-50 ms
    void update();

    // Flash quickly (blocks for times * 2 * intervalMs), e.g. just before a restart
    void flash(uint8_t times, uint32_t intervalMs);
}
