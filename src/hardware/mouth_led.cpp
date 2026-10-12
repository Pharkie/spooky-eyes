#include "hardware/mouth_led.h"

#include <Arduino.h>
#include <esp_random.h>
#include "hardware/board.h"

namespace
{
    struct Colour
    {
        bool red;
        bool green;
        bool blue;
    };

    // COLOUR_RED, COLOUR_GREEN and COLOUR_MAGENTA in main.py
    constexpr Colour kColours[mouth_led::kColourCount] = {
        {true, false, false},
        {false, true, false},
        {true, false, true},
    };

    constexpr Colour kOff = {false, false, false};

    bool isEnabled = false;
    bool lit = false;

    // Common anode: LOW lights a colour, HIGH turns it off
    void set(const Colour &colour)
    {
        digitalWrite(kMouthRedPin, colour.red ? LOW : HIGH);
        digitalWrite(kMouthGreenPin, colour.green ? LOW : HIGH);
        digitalWrite(kMouthBluePin, colour.blue ? LOW : HIGH);
        lit = colour.red || colour.green || colour.blue;
    }
}

namespace mouth_led
{
    void begin()
    {
        // Set the levels before making the pins outputs, so the LED doesn't flash white at boot
        set(kOff);
        pinMode(kMouthRedPin, OUTPUT);
        pinMode(kMouthGreenPin, OUTPUT);
        pinMode(kMouthBluePin, OUTPUT);
    }

    void setEnabled(bool enabled)
    {
        const bool wasEnabled = isEnabled;
        isEnabled = enabled;
        if (!enabled)
            set(kOff);
        else if (!wasEnabled || !lit)
            randomColour(esp_random());
    }

    bool enabled()
    {
        return isEnabled;
    }

    void randomColour(uint32_t randomNumber)
    {
        if (isEnabled)
            set(kColours[randomNumber % kColourCount]);
    }

    void off()
    {
        set(kOff);
    }
}
