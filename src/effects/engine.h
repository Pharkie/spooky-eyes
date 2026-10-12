#pragma once
// Everything an effect needs from the outside world. Effects are plain functions taking an Engine&,
// so the same code runs on the pumpkin (DeviceEngine in src/player/) and in tests on a computer
// (a recording engine in test/), with no Arduino calls inside src/effects/.

#include <cstdint>
#include "display/frame.h"

// Thrown by Engine::wait() when someone presses a new button, so the current effect stops at once
// (the same "a new press interrupts what's playing" behaviour as the old laptop button page).
struct EffectInterrupted
{
};

class Engine
{
public:
    virtual ~Engine() = default;

    // Show a picture on each eye: left and right as you look at the pumpkin
    virtual void show(const Frame &left, const Frame &right) = 0;

    // Raw MAX7219 brightness 0-15. Effects normally stay at maxBright(); flashes go a little above it.
    virtual void setBrightness(uint8_t level) = 0;

    // Pause for some milliseconds. Throws EffectInterrupted if a new request has arrived.
    virtual void wait(uint32_t ms) = 0;

    // Words for the talking pumpkin (phones speak them; also printed as "SAY:..." on USB serial)
    virtual void say(const char *words) = 0;

    // A random whole number from 0 to n-1
    virtual uint32_t random(uint32_t n) = 0;

    // Pick a new random colour for the RGB mouth LED (does nothing if it's switched off)
    virtual void mouthRandomColour() = 0;

    // User settings the effects use
    virtual uint8_t maxBright() const = 0;      // normal brightness, 0 to kMaxBrightness (default 2)
    virtual uint8_t eyeGap() const = 0;         // blank columns between the eyes when things scroll (default 8)
    virtual const char *pumpkinName() const = 0; // e.g. "Jack's Pumpkin"
};
