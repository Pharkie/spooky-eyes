#pragma once
// The one list of effects. The web page builds its buttons from this (GET /api/effects), the surprise
// show picks from it, and the talking pumpkin says each effect's phrase when it starts. Adding an
// effect = write the function in effects.cpp, then add one line to the table in registry.cpp.

#include <cstddef>
#include "effects/engine.h"

using EffectFn = void (*)(Engine &);

enum class EffectGroup : uint8_t
{
    Pictures,
    Minecraft,
    Eyes,
    Games,
    Words,
};

struct EffectInfo
{
    const char *id;     // URL-safe name, e.g. "creeper"
    const char *label;  // button text, e.g. "Creeper"
    const char *emoji;  // button picture, e.g. "💥"
    EffectGroup group;  // which section of the page it's in
    const char *phrase; // what the talking pumpkin says as it starts (nullptr = nothing)
    EffectFn run;       // plays the effect once (the surprise show never returns until interrupted)
    bool inSurprise;    // whether the surprise show may pick it
};

// All effects, in button order
const EffectInfo *allEffects(size_t &count);

// The effect with this id, or nullptr
const EffectInfo *findEffect(const char *id);

// Section heading for a group, e.g. "Minecraft"
const char *groupName(EffectGroup group);

// One pass of the normal show (main() loop body in the old main.py)
void normalShow(Engine &engine);

// Scroll someone's own words across the eyes
void scrollWords(Engine &engine, const char *words);

// The surprise show picking only from the effects pick() says yes to (as well as being inSurprise). The
// tests use it to replay the old show, which didn't have the effects added since. surpriseShow() is this
// with every effect.
void surpriseShowFrom(Engine &engine, bool (*pick)(const EffectInfo &effect));
