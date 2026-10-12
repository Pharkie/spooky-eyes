#pragma once
// Building blocks for the effects, ported from the helpers in the old main.py. Left and right always
// mean as you look at the pumpkin (the old show_char(a, b) put a on the RIGHT eye; that's swapped here).
// Random numbers come from engine.random(), called in the same order as the Python did.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include "display/font.h"
#include "display/frame.h"
#include "effects/engine.h"

// What one eye does in sparkles() and explode(). Replaces the old True / False / picture arguments.
struct EyeSetting
{
    enum class Kind : uint8_t
    {
        Sparkle, // True: random dots (or explode()'s burst)
        Blank,   // False: nothing
        Keep,    // a picture: keep showing it
    };

    Kind kind;
    Frame picture; // only for Keep

    static constexpr EyeSetting sparkle() { return {Kind::Sparkle, kBlank}; }
    static constexpr EyeSetting blank() { return {Kind::Blank, kBlank}; }
    static constexpr EyeSetting keep(const Frame &picture) { return {Kind::Keep, picture}; }

    // True = sparkle, false = blank, as the old code passed booleans
    static constexpr EyeSetting sparkleIf(bool on) { return on ? sparkle() : blank(); }
};

// One step of a sequence: a picture for each eye, then a pause
struct SequenceStep
{
    Frame left;
    Frame right;
    uint32_t ms;
};

// --- Small helpers ---

// Where the right eye starts, counting columns from the left edge of the left eye: one eye's width plus
// the gap. Scrolling text and sliding pictures cross this many columns to get from eye to eye.
int rightEyeColumn(const Engine &engine);

// Brightness for flashes and bangs: a little above normal, up to the MAX7219's top of 15
uint8_t flashBrightness(const Engine &engine);

// time.sleep(seconds) as milliseconds, for delays worked out as they go. Rounds like Python's round(),
// which takes halves to the even number (roll the dice's 0.0625 s is 62 ms).
uint32_t secondsToMs(double seconds);

// Python's // and % for negative numbers: round down, and a result with the sign of b
int floorDiv(int a, int b);
int floorMod(int a, int b);

// A copy of a picture with a different bottom row (the redstone wire runs along it)
Frame withBottomRow(const Frame &picture, uint8_t row);

// --- Showing pictures ---

// Show a picture on each eye, then pause
void showFor(Engine &engine, const Frame &left, const Frame &right, uint32_t ms);

// Show steps one after another
void sequence(Engine &engine, std::initializer_list<SequenceStep> steps);

// Swap two pictures back and forth between the eyes, like a cartoon. As in the old flip(), pic1 starts
// on the RIGHT eye.
void flip(Engine &engine, const Frame &pic1, const Frame &pic2, int times = 4, uint32_t delayMs = 500);

// --- Scrolling and flying ---

// The 8 columns of a message starting at column x (blank outside the message)
Frame textWindow(const Font &font, const char *message, size_t length, int x);

// Scroll a message into the right eye, then across into the left eye. A new mouth colour every frame.
void scrollMessage(Engine &engine, const Font &font, const char *message, uint32_t delayMs = 40);

// Scroll text with a space added at each end, as the old code always did (" Jack's Pumpkin ").
// Text longer than kMaxWordsLength is cut short.
void scrollPadded(Engine &engine, const Font &font, const char *text, uint32_t delayMs = 40);

// Fly a picture across the eyes like scrolling text, swapping between frames (messages in a sprite
// font, e.g. " a " and " b ") every few columns to animate it. The mouth stays as it is.
void fly(Engine &engine, const Font &font, std::initializer_list<const char *> frames, uint32_t delayMs = 40,
         int flap = 3);

// Slide a picture across the eyes, from column start to column end, over still pictures. Column 0 is the
// left edge of the left eye; the right eye starts at rightEyeColumn(). Frames change every 2 columns.
void slide(Engine &engine, std::initializer_list<Frame> frames, int start, int end, const Frame &leftBg = kBlank,
           const Frame &rightBg = kBlank, uint32_t delayMs = 30);

// --- Sparkles and bangs ---

// One row of random dots; higher sparseness means fewer dots
uint8_t randomDots(Engine &engine, int sparseness);

// A whole picture of random dots, top row first
Frame randomDotsPicture(Engine &engine, int sparseness);

// Random twinkly dots; higher sparseness means fewer dots
void sparkles(Engine &engine, int frames, int sparseness = 2, EyeSetting left = EyeSetting::sparkle(),
              EyeSetting right = EyeSetting::sparkle(), uint32_t delayMs = 60);

// BOOM: a bright burst, then smoke. Sparkle eyes burst, blank eyes stay dark, kept pictures stay.
void explode(Engine &engine, EyeSetting left = EyeSetting::sparkle(), EyeSetting right = EyeSetting::sparkle());
