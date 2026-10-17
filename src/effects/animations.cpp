// The eye animations from eyes_ani.json, and the player that runs them.
// The first six (stareAndBlink to ghosts1) are GurgleApps' originals (GPL); the rest are the family's.

#include "effects/animations.h"

#include <algorithm>
#include <iterator>
#include "display/sprites.h"

namespace
{
    constexpr int8_t kSameBrightness = -1;

    // Brightness steps were written for the old fixed MAX_BRIGHT of 2, so 2 means "normal brightness".
    // They're scaled to the user's brightness setting (see scaledBrightness below).
    constexpr uint8_t kOriginalMaxBright = 2;

    // The last two steps dim both eyes to the dimmest setting (not off) for half a second, then back to
    // normal. "br" meant right-eye brightness in the GurgleApps original; the MAX7219 version sets both eyes.
    const AnimationStep kStareAndBlink[] = {
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 1000},
        {&Eyes::straightBlink1, &Eyes::straightBlink1, kSameBrightness, 20},
        {&Eyes::straightBlink2, &Eyes::straightBlink2, kSameBrightness, 20},
        {&Eyes::straightBlink3, &Eyes::straightBlink3, kSameBrightness, 20},
        {&Eyes::allOff, &Eyes::allOff, kSameBrightness, 20},
        {&Eyes::straightBlink2, &Eyes::straightBlink2, kSameBrightness, 20},
        {&Eyes::straightBlink1, &Eyes::straightBlink1, kSameBrightness, 20},
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 20},
        {nullptr, nullptr, 0, 500},
        {nullptr, nullptr, 2, 500},
    };

    const AnimationStep kDownLeftABit[] = {
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 200},
        {&Eyes::downLeft1, &Eyes::downLeft1, kSameBrightness, 2000},
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 200},
    };

    // Not used by any effect.
    const AnimationStep kDownRightABit[] = {
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 200},
        {&Eyes::downRight1, &Eyes::downRight1, kSameBrightness, 2000},
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 200},
    };

    const AnimationStep kRoll[] = {
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 200},
        {&Eyes::left1, &Eyes::left1, kSameBrightness, 100},
        {&Eyes::upLeft1, &Eyes::upLeft1, kSameBrightness, 100},
        {&Eyes::upLeft, &Eyes::upLeft, kSameBrightness, 100},
        {&Eyes::up2, &Eyes::up2, kSameBrightness, 100},
        {&Eyes::upRight, &Eyes::upRight, kSameBrightness, 100},
        {&Eyes::upRight1, &Eyes::upRight1, kSameBrightness, 100},
        {&Eyes::right3, &Eyes::right3, kSameBrightness, 100},
        {&Eyes::downRight, &Eyes::downRight, kSameBrightness, 100},
        {&Eyes::downRight1, &Eyes::downRight1, kSameBrightness, 100},
        {&Eyes::down3, &Eyes::down3, kSameBrightness, 100},
        {&Eyes::downLeft, &Eyes::downLeft, kSameBrightness, 100},
        {&Eyes::downLeftB, &Eyes::downLeftB, kSameBrightness, 100},
        {&Eyes::left2, &Eyes::left2, kSameBrightness, 100},
        {&Eyes::left1, &Eyes::left1, kSameBrightness, 100},
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 200},
    };

    const AnimationStep kGrowEyes[] = {
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 600},
        {&Eyes::straightX2, &Eyes::straightX2, kSameBrightness, 1000},
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 1000},
    };

    // Not used by any effect. Its brightness 15 is capped at normal brightness.
    const AnimationStep kGhosts1[] = {
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 20},
        {&Eyes::ghost1, &Eyes::ghost2, kSameBrightness, 2000},
        {&Eyes::ghost2, &Eyes::ghost1, kSameBrightness, 2000},
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 20},
        {nullptr, nullptr, 1, 1000},
        {nullptr, nullptr, 15, 2000},
    };

    // Winks the eye on your RIGHT as you look at the pumpkin (the pumpkin's own left eye): the JSON blinks
    // "l", and the old show_char() put "l" on the right.
    const AnimationStep kWinkLeft[] = {
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 2000},
        {&Eyes::straight, &Eyes::straightBlink1, kSameBrightness, 20},
        {&Eyes::straight, &Eyes::straightBlink2, kSameBrightness, 20},
        {&Eyes::straight, &Eyes::straightBlink3, kSameBrightness, 20},
        {&Eyes::straight, &Eyes::straightBlinkLine, kSameBrightness, 100},
        {&Eyes::straight, &Eyes::straightBlink3, kSameBrightness, 20},
        {&Eyes::straight, &Eyes::straightBlink2, kSameBrightness, 20},
        {&Eyes::straight, &Eyes::straightBlink1, kSameBrightness, 20},
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 500},
    };

    // Winks the eye on your LEFT. (Step 7 once had "l" and "r" swapped in eyes_ani.json, so for 20 ms the
    // other eye half-shut; fixed in both.)
    const AnimationStep kWinkRight[] = {
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 2000},
        {&Eyes::straightBlink1, &Eyes::straight, kSameBrightness, 20},
        {&Eyes::straightBlink2, &Eyes::straight, kSameBrightness, 20},
        {&Eyes::straightBlink3, &Eyes::straight, kSameBrightness, 20},
        {&Eyes::straightBlinkLine, &Eyes::straight, kSameBrightness, 100},
        {&Eyes::straightBlink3, &Eyes::straight, kSameBrightness, 20},
        {&Eyes::straightBlink2, &Eyes::straight, kSameBrightness, 20},
        {&Eyes::straightBlink1, &Eyes::straight, kSameBrightness, 20},
        {&Eyes::straight, &Eyes::straight, kSameBrightness, 500},
    };

    const AnimationStep kMinecraft[] = {
        {&Eyes::minecraft, &Eyes::minecraft, kSameBrightness, 1000},
        {&Eyes::minecraftLeft, &Eyes::minecraftLeft, kSameBrightness, 800},
        {&Eyes::minecraft, &Eyes::minecraft, kSameBrightness, 300},
        {&Eyes::minecraftRight, &Eyes::minecraftRight, kSameBrightness, 800},
        {&Eyes::minecraft, &Eyes::minecraft, kSameBrightness, 600},
        {&Eyes::minecraftBlink1, &Eyes::minecraftBlink1, kSameBrightness, 40},
        {&Eyes::minecraftBlink2, &Eyes::minecraftBlink2, kSameBrightness, 40},
        {&Eyes::minecraftBlink3, &Eyes::minecraftBlink3, kSameBrightness, 40},
        {&Eyes::allOff, &Eyes::allOff, kSameBrightness, 60},
        {&Eyes::minecraftBlink3, &Eyes::minecraftBlink3, kSameBrightness, 40},
        {&Eyes::minecraftBlink2, &Eyes::minecraftBlink2, kSameBrightness, 40},
        {&Eyes::minecraftBlink1, &Eyes::minecraftBlink1, kSameBrightness, 40},
        {&Eyes::minecraft, &Eyes::minecraft, kSameBrightness, 1000},
    };
}

namespace Animations
{
    const Animation stareAndBlink = {"stareAndBlink", kStareAndBlink, std::size(kStareAndBlink)};
    const Animation downLeftABit = {"downLeftABit", kDownLeftABit, std::size(kDownLeftABit)};
    const Animation downRightABit = {"downRightABit", kDownRightABit, std::size(kDownRightABit)};
    const Animation roll = {"roll", kRoll, std::size(kRoll)};
    const Animation growEyes = {"growEyes", kGrowEyes, std::size(kGrowEyes)};
    const Animation ghosts1 = {"ghosts1", kGhosts1, std::size(kGhosts1)};
    const Animation winkLeft = {"winkLeft", kWinkLeft, std::size(kWinkLeft)};
    const Animation winkRight = {"winkRight", kWinkRight, std::size(kWinkRight)};
    const Animation minecraft = {"minecraft", kMinecraft, std::size(kMinecraft)};
}

// An animation's brightness, scaled from the old fixed MAX_BRIGHT of 2 to the user's setting and capped
// there, as anim_runner capped it. Identical to the original when maxBright() is 2.
static uint8_t scaledBrightness(uint8_t level, uint8_t maxBright)
{
    return std::min(level, kOriginalMaxBright) * maxBright / kOriginalMaxBright;
}

void runAnimation(Engine &engine, const Animation &animation)
{
    for (size_t i = 0; i < animation.count; i++)
    {
        const AnimationStep &step = animation.steps[i];
        engine.mouthRandomColour();
        if (step.left && step.right)
            engine.show(*step.left, *step.right);
        if (step.brightness != kSameBrightness)
            engine.setBrightness(scaledBrightness(step.brightness, engine.maxBright()));
        engine.wait(step.delayMs);
    }
}
