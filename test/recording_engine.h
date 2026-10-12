#pragma once
// An Engine for tests on a computer: instead of driving LEDs it writes down everything an effect does,
// one line per event, in the same format as the golden files made by tools/capture_python_frames.py:
//
//   F <left> <right>   both eyes (viewer's left and right), 8 rows each as 16 hex digits
//   W <ms>             wait
//   B <level>          brightness
//   S <words>          the talking pumpkin says something
//   M                  the mouth LED picks a new random colour
//
// Random numbers come from the same generator the Python capture uses (xorshift32 seeded with 12345),
// so effects that use random() should record exactly what the MicroPython original did. The settings
// default to the ones the originals had built in, which the golden files were recorded with.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "effects/engine.h"

class RecordingEngine : public Engine
{
public:
    static constexpr uint32_t kSeed = 12345;

    // main.py's MAX_BRIGHT, EYE_GAP and the name the "name" button scrolled
    static constexpr uint8_t kGoldenMaxBright = 2;
    static constexpr uint8_t kGoldenEyeGap = 8;
    static constexpr const char *kGoldenName = "Jack's Pumpkin";

    explicit RecordingEngine(uint8_t maxBright = kGoldenMaxBright, uint8_t eyeGap = kGoldenEyeGap,
                             const char *name = kGoldenName)
        : maxBright_(maxBright), eyeGap_(eyeGap), name_(name ? name : "")
    {
    }

    std::vector<std::string> events;

    // Stop runaway effects: after this many events, wait() and show() throw EffectInterrupted
    // (0 = no limit). The test sets it well above the golden length so a never-ending effect fails.
    size_t eventLimit = 0;
    bool hitLimit = false;

    // How many times random() was called, for diagnosing mismatches
    size_t randomCalls = 0;

    void show(const Frame &left, const Frame &right) override
    {
        add("F " + hex(left) + " " + hex(right));
    }

    void setBrightness(uint8_t level) override
    {
        add("B " + std::to_string(level));
    }

    void wait(uint32_t ms) override
    {
        add("W " + std::to_string(ms));
    }

    void say(const char *words) override
    {
        add(std::string("S ") + (words ? words : ""));
    }

    uint32_t random(uint32_t n) override
    {
        randomCalls++;
        // xorshift32: x ^= x << 13; x ^= x >> 17; x ^= x << 5
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        if (n == 0)
            return 0; // Python would have raised an error here; never divide by zero
        return state_ % n;
    }

    void mouthRandomColour() override
    {
        // Python: color = random.choice(LED_COLOURS) (3 colours); set_rgb_color(*color)
        random(3);
        add("M");
    }

    uint8_t maxBright() const override { return maxBright_; }
    uint8_t eyeGap() const override { return eyeGap_; }
    const char *pumpkinName() const override { return name_.c_str(); }

    // One eye as 16 hex digits, top row first
    static std::string hex(const Frame &rows)
    {
        char out[17];
        for (int i = 0; i < 8; i++)
            std::snprintf(out + i * 2, 3, "%02x", rows[i]);
        return std::string(out, 16);
    }

private:
    uint8_t maxBright_;
    uint8_t eyeGap_;
    std::string name_;
    uint32_t state_ = kSeed;

    void add(std::string line)
    {
        if (eventLimit && events.size() >= eventLimit)
        {
            hitLimit = true;
            throw EffectInterrupted();
        }
        events.push_back(std::move(line));
    }
};
