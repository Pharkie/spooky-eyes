// See timing.h

#include "effects/timing.h"

#include "effects/show.h"

namespace
{
    // Longer than any effect: stops a runaway one
    constexpr uint32_t kGiveUpMs = 10UL * 60UL * 1000UL;

    class TimingEngine : public Engine
    {
    public:
        explicit TimingEngine(const TimingSettings &settings) : settings_(settings) {}

        void show(const Frame &, const Frame &) override {}
        void setBrightness(uint8_t) override {}
        void wait(uint32_t ms) override
        {
            totalMs_ += ms;
            if (totalMs_ > kGiveUpMs)
                throw EffectInterrupted{};
        }
        void say(const char *) override {}
        uint32_t random(uint32_t n) override
        {
            if (n == 0)
                return 0;
            // xorshift32: the same "typical run" every time
            seed_ ^= seed_ << 13;
            seed_ ^= seed_ >> 17;
            seed_ ^= seed_ << 5;
            return seed_ % n;
        }
        void mouthRandomColour() override {}
        uint8_t maxBright() const override { return 2; } // brightness doesn't change the timing
        uint8_t eyeGap() const override { return settings_.eyeGap; }
        const char *pumpkinName() const override { return settings_.pumpkinName ? settings_.pumpkinName : ""; }

        uint32_t totalMs() const { return totalMs_; }

    private:
        TimingSettings settings_;
        uint32_t totalMs_ = 0;
        uint32_t seed_ = 0x5EED1234;
    };
}

uint32_t effectMs(const EffectInfo &effect, const TimingSettings &settings)
{
    if (!canBeInShow(effect))
        return 0; // never ends
    TimingEngine engine(settings);
    try
    {
        effect.run(engine);
    }
    catch (const EffectInterrupted &)
    {
        return 0; // ran away: no estimate
    }
    return engine.totalMs();
}

uint32_t wordsMs(const char *words, const TimingSettings &settings)
{
    TimingEngine engine(settings);
    try
    {
        scrollWords(engine, words ? words : "");
    }
    catch (const EffectInterrupted &)
    {
        return 0;
    }
    return engine.totalMs();
}
