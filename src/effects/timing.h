#pragma once
// How long an effect, or some scrolled words, lasts: played on an engine that shows nothing and only adds
// up the waits, so it takes a moment of processor time, not the effect's real time. For the show page's
// "about how long" (GET /api/effects). Portable, so the native tests check it too.

#include <cstdint>
#include "effects/registry.h"

// The settings that change how long things take
struct TimingSettings
{
    uint8_t eyeGap;
    const char *pumpkinName; // the "My name" effect scrolls it
};

// Milliseconds the effect takes as a show block (it plays once). 0 for one that never ends (the surprise
// show), and random ones (the dice, the fruit machine...) give one typical run.
uint32_t effectMs(const EffectInfo &effect, const TimingSettings &settings);

// Milliseconds to scroll these words, as a show block does
uint32_t wordsMs(const char *words, const TimingSettings &settings);
