#pragma once
// The eye animations from eyes_ani.json, and the player that runs them (the old anim_runner).
// Generated once from the MicroPython originals by tools/gen_sprites.py; edit by hand from now on.
//
// The old anim_runner called show_char(font[l], font[r]), and show_char put its first picture on the
// RIGHT eye as you look at the pumpkin. So JSON "l" pictures were shown on your right. The steps
// here are written as you look at the pumpkin (left = your left), and show exactly what it showed.

#include <cstddef>
#include <cstdint>
#include "display/frame.h"
#include "effects/engine.h"

// One step: show a picture on each eye (or keep what is showing), maybe change brightness, then pause
struct AnimationStep
{
    const Frame *left;  // your left eye (JSON "r"); nullptr for both = keep the pictures
    const Frame *right; // your right eye (JSON "l")
    int8_t brightness;  // JSON "br": 2 = normal, scaled to maxBright() when it plays (-1 = leave it alone)
    uint16_t delayMs;   // JSON "d", in milliseconds
};

struct Animation
{
    const char *name; // as in eyes_ani.json, e.g. "stareAndBlink"
    const AnimationStep *steps;
    size_t count;
};

namespace Animations
{
    extern const Animation stareAndBlink;
    extern const Animation downLeftABit;
    extern const Animation downRightABit;
    extern const Animation roll;
    extern const Animation growEyes;
    extern const Animation ghosts1;
    extern const Animation winkLeft;
    extern const Animation winkRight;
    extern const Animation minecraft;
}

// Play an animation once: a new mouth colour each step, as the old anim_runner did
void runAnimation(Engine &engine, const Animation &animation);
