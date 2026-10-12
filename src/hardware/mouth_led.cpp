// The mouth LED, dimmed with PWM to save batteries. Its pins drive it straight, with no resistor, so a
// colour fully on can pull 10-20 mA (more for red, which needs the least voltage). Lit for
// kMouthDutyPercent of the time instead, it draws about a quarter of that and still glows.
//
// PWM comes from the LEDC peripheral: one channel per colour. Channels 0 and 1 share LEDC timer 0 and
// channel 2 uses timer 1, all set the same. Nothing else uses LEDC (the eyes' SPI doesn't).

#include "hardware/mouth_led.h"

#include <Arduino.h>
#include <esp_random.h>
#include "core/constants.h"
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

    constexpr uint8_t kRedChannel = 0;
    constexpr uint8_t kGreenChannel = 1;
    constexpr uint8_t kBlueChannel = 2;
    constexpr uint32_t kPwmHz = 5000; // far too fast to see flicker
    constexpr uint8_t kPwmBits = 8;
    constexpr uint32_t kPwmMax = (1u << kPwmBits) - 1; // ledcWrite() treats this as high all the time

    // Common anode: the pin low lights the colour, so the PWM's high time is when it's dark. Off = high
    // all the time; on = low for kMouthDutyPercent of each cycle.
    constexpr uint32_t kDutyOff = kPwmMax;
    constexpr uint32_t kDutyOn = kPwmMax - (kPwmMax * kMouthDutyPercent) / 100;
    static_assert(kMouthDutyPercent > 0 && kMouthDutyPercent <= 100, "kMouthDutyPercent should be 1-100");

    bool isEnabled = false;
    bool lit = false;

    void set(const Colour &colour)
    {
        ledcWrite(kRedChannel, colour.red ? kDutyOn : kDutyOff);
        ledcWrite(kGreenChannel, colour.green ? kDutyOn : kDutyOff);
        ledcWrite(kBlueChannel, colour.blue ? kDutyOn : kDutyOff);
        lit = colour.red || colour.green || colour.blue;
    }

    // Hand a pin to its PWM channel, dark. The pin is held high (dark) until then, so the LED doesn't
    // flash white at boot. (Attaching drives it low for the few microseconds before the next line: far
    // too short to see.)
    void attach(int pin, uint8_t channel)
    {
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
        ledcSetup(channel, kPwmHz, kPwmBits);
        ledcAttachPin(pin, channel);
        ledcWrite(channel, kDutyOff);
    }
}

namespace mouth_led
{
    void begin()
    {
        attach(kMouthRedPin, kRedChannel);
        attach(kMouthGreenPin, kGreenChannel);
        attach(kMouthBluePin, kBlueChannel);
        lit = false;
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
