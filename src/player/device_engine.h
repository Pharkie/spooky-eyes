#pragma once
// The Engine on the pumpkin itself: effects draw on the real eyes and mouth. Used only by the player,
// on the Arduino loop task.

#include <cstddef>
#include <cstdint>
#include "core/constants.h"
#include "effects/engine.h"

class DeviceEngine : public Engine
{
public:
    // How the engine reaches back into the player
    struct Hooks
    {
        void (*poll)();                   // housekeeping, run on every slice of wait() (serial commands...)
        bool (*interrupted)();            // true when a new request is waiting: wait() then throws
        void (*said)(const char *words);  // the talking pumpkin said something
    };

    explicit DeviceEngine(const Hooks &hooks);

    // Engine
    void show(const Frame &left, const Frame &right) override;
    void setBrightness(uint8_t level) override;
    void wait(uint32_t ms) override;
    void say(const char *words) override;
    uint32_t random(uint32_t n) override;
    void mouthRandomColour() override;
    uint8_t maxBright() const override { return maxBright_; }
    uint8_t eyeGap() const override { return eyeGap_; }
    const char *pumpkinName() const override { return name_; }

    // Copy the current settings and switch the mouth LED on or off. The name is only copied when
    // includeName is true, so the player can leave it alone while an effect might be scrolling it.
    void refreshSettings(bool includeName);

private:
    static constexpr size_t kNameBufferSize = kMaxNameLength * 4 + 1; // room for accented letters

    Hooks hooks_;
    uint8_t maxBright_ = kDefaultBrightness;
    uint8_t eyeGap_ = kDefaultEyeGap;
    char name_[kNameBufferSize] = {};
};
