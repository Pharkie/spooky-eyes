// Status LED patterns from Scribe Evolution's updateStatusLED() (src/core/network.cpp, mini boards).
// Scribe drove its LED active-high; the SuperMini's blue LED lights when the pin is LOW.

#include "hardware/status_led.h"

#include <Arduino.h>

#include "hardware/board.h"

namespace
{
    constexpr uint32_t kFastBlinkMs = 250;
    constexpr uint32_t kSlowBlinkMs = 1000;
    constexpr uint32_t kHeartbeatOnMs = 50;
    constexpr uint32_t kHeartbeatOffMs = 2950;

    status_led::Pattern pattern = status_led::Pattern::Off;
    bool lit = false;
    bool written = false; // false = write the pin on the next update()
    uint32_t lastToggleMs = 0;

    void write(bool on)
    {
        digitalWrite(kStatusLedPin, (on != kStatusLedActiveLow) ? HIGH : LOW);
        lit = on;
        written = true;
    }
}

namespace status_led
{
    void begin()
    {
        pinMode(kStatusLedPin, OUTPUT);
        pattern = Pattern::Off;
        write(false);
    }

    void set(Pattern next)
    {
        if (next == pattern)
            return;
        pattern = next;
        written = false;
    }

    void update()
    {
        const uint32_t now = millis();
        switch (pattern)
        {
        case Pattern::Off:
            if (!written || lit)
                write(false);
            break;

        case Pattern::FastBlink:
        case Pattern::SlowBlink:
        {
            const uint32_t period = (pattern == Pattern::FastBlink) ? kFastBlinkMs : kSlowBlinkMs;
            if (!written)
            {
                write(true);
                lastToggleMs = now;
            }
            else if (now - lastToggleMs >= period)
            {
                write(!lit);
                lastToggleMs = now;
            }
            break;
        }

        case Pattern::Heartbeat:
            if (!written)
            {
                write(true);
                lastToggleMs = now;
            }
            else if (now - lastToggleMs >= (lit ? kHeartbeatOnMs : kHeartbeatOffMs))
            {
                write(!lit);
                lastToggleMs = now;
            }
            break;
        }
    }

    void flash(uint8_t times, uint32_t intervalMs)
    {
        for (uint8_t i = 0; i < times; i++)
        {
            write(true);
            delay(intervalMs);
            write(false);
            delay(intervalMs);
        }
        written = false; // back to the normal pattern on the next update()
    }
}
