// Building blocks for the effects, ported from the helpers in the old main.py.

#include "effects/primitives.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "core/constants.h"
#include "display/sprites.h"

namespace
{
    // One eye's picture for a sparkles() frame
    Frame sparkleEye(Engine &engine, const EyeSetting &setting, int sparseness)
    {
        switch (setting.kind)
        {
        case EyeSetting::Kind::Sparkle:
            return randomDotsPicture(engine, sparseness);
        case EyeSetting::Kind::Keep:
            return setting.picture;
        case EyeSetting::Kind::Blank:
            break;
        }
        return kBlank;
    }

    // One eye's picture for a frame of explode()'s burst
    const Frame &burstEye(const EyeSetting &setting, const Frame &burst)
    {
        switch (setting.kind)
        {
        case EyeSetting::Kind::Sparkle:
            return burst;
        case EyeSetting::Kind::Keep:
            return setting.picture;
        case EyeSetting::Kind::Blank:
            break;
        }
        return Shapes::empty;
    }
}

int rightEyeColumn(const Engine &engine)
{
    return 8 + engine.eyeGap();
}

uint8_t flashBrightness(const Engine &engine)
{
    return static_cast<uint8_t>(std::min<int>(engine.maxBright() + 4, kChipMaxBrightness));
}

uint32_t secondsToMs(double seconds)
{
    // nearbyint() rounds halves to even in the default rounding mode, as Python's round() does
    return static_cast<uint32_t>(std::nearbyint(seconds * 1000.0));
}

int floorDiv(int a, int b)
{
    int quotient = a / b;
    if (a % b != 0 && (a < 0) != (b < 0))
        quotient--;
    return quotient;
}

int floorMod(int a, int b)
{
    return a - floorDiv(a, b) * b;
}

Frame withBottomRow(const Frame &picture, uint8_t row)
{
    Frame out = picture;
    out[7] = row;
    return out;
}

void showFor(Engine &engine, const Frame &left, const Frame &right, uint32_t ms)
{
    engine.show(left, right);
    engine.wait(ms);
}

void sequence(Engine &engine, std::initializer_list<SequenceStep> steps)
{
    for (const SequenceStep &step : steps)
        showFor(engine, step.left, step.right, step.ms);
}

void flip(Engine &engine, const Frame &pic1, const Frame &pic2, int times, uint32_t delayMs)
{
    for (int i = 0; i < times; i++)
    {
        if (i % 2 == 0)
            engine.show(pic2, pic1);
        else
            engine.show(pic1, pic2);
        engine.wait(delayMs);
    }
}

Frame textWindow(const Font &font, const char *message, size_t length, int x)
{
    const int charPos = floorDiv(x, 8);
    const int shift = floorMod(x, 8);
    const int count = static_cast<int>(length);
    const Frame &leftChar = (charPos >= 0 && charPos < count) ? font.get(message[charPos]) : kBlank;
    const Frame &rightChar = (charPos + 1 >= 0 && charPos + 1 < count) ? font.get(message[charPos + 1]) : kBlank;
    Frame out{};
    for (int row = 0; row < 8; row++)
        out[row] = static_cast<uint8_t>((leftChar[row] << shift) | (rightChar[row] >> (8 - shift)));
    return out;
}

void scrollMessage(Engine &engine, const Font &font, const char *message, uint32_t delayMs)
{
    // The left eye runs behind the right eye by one eye's width plus the gap between them
    const size_t length = std::strlen(message);
    const int lag = rightEyeColumn(engine);
    const int end = static_cast<int>(length) * 8 + lag + 1;
    for (int x = -8; x < end; x++)
    {
        engine.mouthRandomColour();
        const Frame right = textWindow(font, message, length, x);
        const Frame left = textWindow(font, message, length, x - lag);
        engine.show(left, right);
        engine.wait(delayMs);
    }
}

void scrollPadded(Engine &engine, const Font &font, const char *text, uint32_t delayMs)
{
    char message[kMaxWordsLength + 3];
    std::snprintf(message, sizeof(message), " %.*s ", static_cast<int>(kMaxWordsLength), text ? text : "");
    scrollMessage(engine, font, message, delayMs);
}

void fly(Engine &engine, const Font &font, std::initializer_list<const char *> frames, uint32_t delayMs, int flap)
{
    const int lag = rightEyeColumn(engine);
    size_t width = 0;
    for (const char *frame : frames)
        width = std::max(width, std::strlen(frame));
    const int count = static_cast<int>(frames.size());
    const int end = static_cast<int>(width) * 8 + lag + 1;
    for (int x = -8; x < end; x++)
    {
        const char *message = frames.begin()[floorMod(floorDiv(x, flap), count)];
        const size_t length = std::strlen(message);
        engine.show(textWindow(font, message, length, x - lag), textWindow(font, message, length, x));
        engine.wait(delayMs);
    }
}

void slide(Engine &engine, std::initializer_list<Frame> frames, int start, int end, const Frame &leftBg,
           const Frame &rightBg, uint32_t delayMs)
{
    const int far = rightEyeColumn(engine);
    const int step = end >= start ? 1 : -1;
    const int count = static_cast<int>(frames.size());
    int i = 0;
    for (int x = start; x != end + step; x += step, i++)
    {
        const Frame &sprite = frames.begin()[(i / 2) % count];
        engine.show(overlay(leftBg, place(sprite, x)), overlay(rightBg, place(sprite, x - far)));
        engine.wait(delayMs);
    }
}

uint8_t randomDots(Engine &engine, int sparseness)
{
    uint8_t dots = 0xff;
    for (int i = 0; i < sparseness; i++)
        dots &= static_cast<uint8_t>(engine.random(256));
    return dots;
}

Frame randomDotsPicture(Engine &engine, int sparseness)
{
    Frame out{};
    for (uint8_t &row : out)
        row = randomDots(engine, sparseness);
    return out;
}

void sparkles(Engine &engine, int frames, int sparseness, EyeSetting left, EyeSetting right, uint32_t delayMs)
{
    for (int i = 0; i < frames; i++)
    {
        // Left eye first, as Python worked out show_eyes()'s arguments left to right
        const Frame leftPicture = sparkleEye(engine, left, sparseness);
        const Frame rightPicture = sparkleEye(engine, right, sparseness);
        showFor(engine, leftPicture, rightPicture, delayMs);
    }
}

void explode(Engine &engine, EyeSetting left, EyeSetting right)
{
    const Frame *const burst[] = {&Shapes::star1, &Shapes::star2, &Shapes::star3, &Shapes::star4, &Shapes::allOn};
    engine.setBrightness(flashBrightness(engine));
    for (const Frame *picture : burst)
        showFor(engine, burstEye(left, *picture), burstEye(right, *picture), 70);
    engine.setBrightness(engine.maxBright());
    sparkles(engine, 8, 2, left, right);
    sparkles(engine, 8, 3, left, right);
}
