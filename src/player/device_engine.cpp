#include "player/device_engine.h"

#include <Arduino.h>
#include <esp_random.h>
#include "core/settings.h"
#include "hardware/max7219_eyes.h"
#include "hardware/mouth_led.h"

namespace
{
    // wait() sleeps in slices this long, so a button press stops an effect within about 10 ms
    constexpr uint32_t kWaitSliceMs = 10;
}

DeviceEngine::DeviceEngine(const Hooks &hooks) : hooks_(hooks)
{
    strlcpy(name_, kDefaultPumpkinName, sizeof name_);
}

void DeviceEngine::show(const Frame &left, const Frame &right)
{
    max7219_eyes::show(left, right);
}

void DeviceEngine::setBrightness(uint8_t level)
{
    max7219_eyes::setBrightness(level);
}

void DeviceEngine::wait(uint32_t ms)
{
    const uint32_t start = millis();
    for (;;)
    {
        if (hooks_.poll)
            hooks_.poll();
        if (hooks_.interrupted && hooks_.interrupted())
            throw EffectInterrupted();

        const uint32_t elapsed = millis() - start;
        if (elapsed >= ms)
            return;

        // vTaskDelay (not a busy wait) lets the WiFi and web server tasks run meanwhile
        const uint32_t slice = ms - elapsed < kWaitSliceMs ? ms - elapsed : kWaitSliceMs;
        const TickType_t ticks = pdMS_TO_TICKS(slice);
        vTaskDelay(ticks > 0 ? ticks : 1);
    }
}

void DeviceEngine::say(const char *words)
{
    if (!words || !*words)
        return;
    Serial.printf("SAY:%s\n", words); // for tools listening on the USB cable
    if (hooks_.said)
        hooks_.said(words);
}

uint32_t DeviceEngine::random(uint32_t n)
{
    if (n == 0)
        return 0;
    return esp_random() % n;
}

void DeviceEngine::mouthRandomColour()
{
    mouth_led::randomColour(esp_random());
}

void DeviceEngine::refreshSettings(bool includeName)
{
    const Settings current = settings::get();
    maxBright_ = current.brightness > kMaxBrightness ? kMaxBrightness : current.brightness;
    eyeGap_ = current.eyeGap > kMaxEyeGap ? kMaxEyeGap : current.eyeGap;
    mouth_led::setEnabled(current.mouthLed);
    if (includeName)
    {
        // Clear the whole buffer first, so anything still reading the old name finds blanks, not junk
        memset(name_, 0, sizeof name_);
        strlcpy(name_, current.pumpkinName.length() ? current.pumpkinName.c_str() : kDefaultPumpkinName,
                sizeof name_);
    }
}
