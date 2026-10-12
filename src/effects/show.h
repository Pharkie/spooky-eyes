#pragma once
// A show: a list of blocks played in order, round and round. A block is an effect from the registry, or
// some words to scroll. The show creator page edits it; the original show is defaultShow(). Portable
// (no Arduino), so it's tested on the computer.

#include <string>
#include <vector>
#include "effects/engine.h"
#include "effects/registry.h"

struct ShowBlock
{
    enum class Kind : uint8_t
    {
        Effect, // value is an effect id, e.g. "creeper"
        Words,  // value is the words to scroll, e.g. "Happy Halloween!"
    };
    Kind kind;
    std::string value;
};

using Show = std::vector<ShowBlock>;

// The original show (the old main.py loop), as blocks
const Show &defaultShow();

// Whether an effect may go in a show: everything except effects that never end (the surprise show)
bool canBeInShow(const EffectInfo &effect);

// Check one block and tidy it: an effect must exist and be allowed in shows; words are made plain
// (text::plain) and trimmed to kMaxWordsLength, and mustn't end up empty. False (with a reason) if it
// can't be used.
bool checkBlock(ShowBlock &block, std::string &error);

// Check a whole show: 1..kMaxShowBlocks blocks, each checked as above. Error names the block (1-based).
bool checkShow(Show &show, std::string &error);

// Play one block once
void playBlock(Engine &engine, const ShowBlock &block);
