#pragma once
// The mouth: a common-anode RGB LED (pins in board.h). A pin driven LOW lights that colour.
// On or off per colour, as in main.py: red, green or magenta. "On" is dimmed with PWM to save batteries
// (kMouthDutyPercent). Call only from the Arduino loop task.

#include <cstddef>
#include <cstdint>

namespace mouth_led
{
    constexpr size_t kColourCount = 3; // red, green, magenta (LED_COLOURS in main.py)

    // Set up the pins with the LED off
    void begin();

    // Switched off in settings = dark, and randomColour() does nothing. Switching it back on lights a
    // colour straight away.
    void setEnabled(bool enabled);
    bool enabled();

    // Light colour number (randomNumber % kColourCount), e.g. randomColour(esp_random())
    void randomColour(uint32_t randomNumber);

    // All colours off (it stays enabled, so the next randomColour() lights it again)
    void off();
}
