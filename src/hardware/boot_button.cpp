// BOOT button: a short press shows how to reach the pumpkin and unlocks the risky web actions for a
// while; holding it forgets the home WiFi. The 50 ms debounce is Scribe Evolution's buttonDebounceMs.

#include "hardware/boot_button.h"

#include <Arduino.h>
#include <atomic>

#include "core/constants.h"
#include "core/firmware.h"
#include "core/logging.h"
#include "core/settings.h"
#include "hardware/board.h"
#include "hardware/status_led.h"

namespace
{
    constexpr uint32_t kDebounceMs = 50;
    constexpr uint8_t kFlashes = 10;
    constexpr uint32_t kFlashMs = 50;

    // Network task only
    bool lastReading = false;   // raw reading last time
    uint32_t lastChangeMs = 0;  // when the raw reading last changed
    bool pressed = false;       // debounced state
    uint32_t pressedSinceMs = 0;
    bool waitForRelease = false; // held at boot: ignore it until it's let go
    boot_button::ShortPressHandler shortPressHandler = nullptr;

    // The last short press, read by the web server task. unlockOn is cleared once kBootUnlockMs has
    // passed, so an old press can't count again when millis() wraps round after 49 days.
    std::atomic<uint32_t> lastShortPressMs{0};
    std::atomic<bool> unlockOn{false};

    // Set by begin(), so pressFromSerial() knows update() is being called. A serial "unlock" waits here
    // for the network task.
    std::atomic<bool> watching{false};
    std::atomic<bool> serialPress{false};

    bool readPressed()
    {
        return digitalRead(kBootButtonPin) == LOW;
    }

    // A short press, real or from serial: unlock the risky web actions and tell main.cpp. Network task.
    void shortPress(uint32_t now, const char *how)
    {
        lastShortPressMs = now;
        unlockOn = true;
        LOG("BUTTON", "Short press%s: updates and WiFi changes allowed for %lu minutes", how,
            static_cast<unsigned long>(kBootUnlockMs / 60000));
        if (shortPressHandler)
            shortPressHandler();
    }
}

namespace boot_button
{
    void begin()
    {
        pinMode(kBootButtonPin, INPUT_PULLUP);
        lastReading = readPressed();
        lastChangeMs = millis();
        pressed = lastReading;
        pressedSinceMs = lastChangeMs;
        waitForRelease = pressed;
        if (waitForRelease)
            LOG("BUTTON", "BOOT is held at start-up: ignored until it's let go");
        watching = true;
    }

    bool pressFromSerial()
    {
        if (!watching.load())
            return false;
        serialPress = true;
        return true;
    }

    void onShortPress(ShortPressHandler handler)
    {
        shortPressHandler = handler;
    }

    bool unlocked()
    {
        return unlockOn.load() && millis() - lastShortPressMs.load() < kBootUnlockMs;
    }

    void update()
    {
        const uint32_t now = millis();
        if (unlockOn.load() && now - lastShortPressMs.load() >= kBootUnlockMs)
            unlockOn = false;
        if (serialPress.exchange(false))
            shortPress(now, " (from serial)");

        const bool reading = readPressed();
        if (reading != lastReading)
        {
            lastReading = reading;
            lastChangeMs = now;
            return;
        }
        if (now - lastChangeMs < kDebounceMs)
            return;

        if (reading != pressed)
        {
            pressed = reading;
            if (pressed)
            {
                pressedSinceMs = lastChangeMs;
            }
            else
            {
                if (!waitForRelease)
                    shortPress(now, "");
                waitForRelease = false;
            }
            return;
        }

        if (pressed && !waitForRelease && now - pressedSinceMs >= kForgetWifiHoldMs)
        {
            waitForRelease = true;
            LOG("BUTTON", "BOOT held for %lu s: forgetting the home WiFi",
                static_cast<unsigned long>(kForgetWifiHoldMs / 1000));
            settings::forgetWifi();
            status_led::flash(kFlashes, kFlashMs);
            firmware::restart("BOOT held: back to setup mode");
        }
    }
}
