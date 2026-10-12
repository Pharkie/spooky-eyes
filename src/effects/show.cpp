// Shows: lists of blocks (effects and words), checked and played (see show.h), and the original show.

#include "effects/show.h"

#include "core/constants.h"
#include "core/text.h"
#include "effects/effects.h"

const Show &defaultShow()
{
    // The old main.py loop, step by step, as blocks. tools/capture_python_frames.py and
    // tools/mock_server.py read this table, so keep one {ShowBlock::Kind::..., "..."} entry per line.
    // Each step is the button's effect, which is exactly what main() did except for the second lot of
    // space invaders: main() flipped them 3 times at 0.5 s, the button does 8 flips at 0.3 s.
    static const Show kShow = {
        {ShowBlock::Kind::Effect, "minecraft"},
        {ShowBlock::Kind::Effect, "pumpkin"},
        {ShowBlock::Kind::Effect, "invaders"},
        {ShowBlock::Kind::Effect, "trees"},
        {ShowBlock::Kind::Effect, "bunnies"},
        {ShowBlock::Kind::Effect, "witch"},
        {ShowBlock::Kind::Effect, "ghosts"},
        {ShowBlock::Kind::Effect, "blink"},
        {ShowBlock::Kind::Effect, "name"},
        {ShowBlock::Kind::Effect, "hearts"},
        {ShowBlock::Kind::Effect, "winkleft"},
        {ShowBlock::Kind::Effect, "spell"},
        {ShowBlock::Kind::Effect, "roll"},
        {ShowBlock::Kind::Effect, "invaders"}, // main(): 3 flips at 0.5 s
        {ShowBlock::Kind::Effect, "lookdown"},
        {ShowBlock::Kind::Effect, "blink"},
        {ShowBlock::Kind::Effect, "trick"},
        {ShowBlock::Kind::Effect, "roll"},
        {ShowBlock::Kind::Effect, "blink"},
        {ShowBlock::Kind::Effect, "grow"},
        {ShowBlock::Kind::Effect, "blink"},
        {ShowBlock::Kind::Effect, "winkright"},
        {ShowBlock::Kind::Effect, "blink"},
        {ShowBlock::Kind::Effect, "halloween"},
    };
    return kShow;
}

bool canBeInShow(const EffectInfo &effect)
{
    // The surprise show carries on until it's interrupted, so the blocks after it would never play
    return effect.run != surpriseShow;
}

bool checkBlock(ShowBlock &block, std::string &error)
{
    switch (block.kind)
    {
    case ShowBlock::Kind::Effect:
    {
        const EffectInfo *effect = findEffect(block.value.c_str());
        if (!effect)
        {
            constexpr size_t kLongestIdShown = 32; // ids are short: don't echo a whole essay back
            error = "no effect called \"" + block.value.substr(0, kLongestIdShown) + "\"";
            return false;
        }
        if (!canBeInShow(*effect))
        {
            error = std::string(effect->label) + " never ends, so it can't go in a show";
            return false;
        }
        return true;
    }
    case ShowBlock::Kind::Words:
    {
        char cleaned[kMaxWordsLength + 1];
        text::plain(block.value.c_str(), cleaned, sizeof cleaned);
        if (!cleaned[0])
        {
            error = "no words to scroll (the eyes can't show emoji)";
            return false;
        }
        block.value = cleaned;
        return true;
    }
    }
    error = "unknown kind of block";
    return false;
}

bool checkShow(Show &show, std::string &error)
{
    if (show.empty())
    {
        error = "The show needs at least one block";
        return false;
    }
    if (show.size() > kMaxShowBlocks)
    {
        error = "That show is too long (up to " + std::to_string(kMaxShowBlocks) + " blocks)";
        return false;
    }
    for (size_t i = 0; i < show.size(); i++)
    {
        std::string problem;
        if (!checkBlock(show[i], problem))
        {
            error = "Block " + std::to_string(i + 1) + ": " + problem;
            return false;
        }
    }
    return true;
}

void playBlock(Engine &engine, const ShowBlock &block)
{
    switch (block.kind)
    {
    case ShowBlock::Kind::Effect:
    {
        // checkShow() has already turned away anything else, but never get stuck in a never-ending one
        const EffectInfo *effect = findEffect(block.value.c_str());
        if (effect && canBeInShow(*effect))
            playEffect(engine, *effect);
        break;
    }
    case ShowBlock::Kind::Words:
        if (!block.value.empty())
        {
            engine.say(block.value.c_str());
            scrollWords(engine, block.value.c_str());
        }
        break;
    }
}
