// The one list of effects (what the web page's buttons show and run), and the surprise show. Labels,
// emoji, sections and button order are from the old button page; the phrases are what its talking
// pumpkin said as each effect started. The shows built from these blocks are in show.cpp.

#include "effects/registry.h"

#include <cstring>
#include <iterator>
#include "display/fonts.h"
#include "effects/effects.h"
#include "effects/primitives.h"

// Who made the Minecraft effects, for their heading: "Minecraft (Eyes Edition, by ...)". Real names stay
// out of this public repo, so the name lives in local/credits.h (not in git) as
//   #define MINECRAFT_BY "Name"
// and builds without that file leave the "by" out.
#if __has_include("../../local/credits.h")
#include "../../local/credits.h"
#endif
#ifdef MINECRAFT_BY
#define MINECRAFT_TITLE "Minecraft (Eyes Edition, by " MINECRAFT_BY ")"
#else
#define MINECRAFT_TITLE "Minecraft (Eyes Edition)"
#endif

namespace
{
    // In button order: id, label, emoji, section, phrase, function, whether the surprise show picks it.
    // The surprise show leaves out the long witches' spell (and itself).
    const EffectInfo kEffects[] = {
        // Pictures
        {"pumpkin", "Pumpkins", "🎃", EffectGroup::Pictures, "Chomp chomp chomp!", pumpkins, true},
        {"witch", "Witch", "🧹", EffectGroup::Pictures, "Hee hee hee! Here I come on my broomstick!", witch, true},
        {"bat", "Bat", "🦇", EffectGroup::Pictures, "Squeak squeak! I'm a bat!", bat, true},
        {"spider", "Spider", "🕷️", EffectGroup::Pictures, "Eek! A spider!", spiderDrop, true},
        {"ghosts", "Ghosts", "👻", EffectGroup::Pictures, "Woooooooo!", ghosts, true},
        {"skull", "Skull", "💀", EffectGroup::Pictures, "Rattle rattle rattle!", skull, true},
        {"cat", "Cat", "🐱", EffectGroup::Pictures, "Meow!", catBlink, true},
        {"pacman", "Pac-Man", "🟡", EffectGroup::Pictures, "Waka waka waka waka!", pacMan, true},
        {"invaders", "Space invaders", "👾", EffectGroup::Pictures, "The space invaders are coming!", invaders, true},
        {"pong", "Pong", "🏓", EffectGroup::Pictures, "Boing! Boing! Boing!", pong, true},
        {"star", "Spinning star", "🌟", EffectGroup::Pictures, "Twinkle twinkle, little star!", starSpin, true},
        {"bunnies", "Bunnies", "🐰", EffectGroup::Pictures, "Hop hop hop!", bunnies, true},
        {"trees", "Christmas trees", "🎄", EffectGroup::Pictures, "Merry Christmas!", trees, true},
        {"hearts", "Hearts", "❤️", EffectGroup::Pictures, "I love you!", hearts, true},
        // Minecraft
        {"minecraft", "Minecraft eyes", "🟩", EffectGroup::Minecraft, "Minecraft eyes!", minecraft, true},
        {"creeper", "Creeper", "💥", EffectGroup::Minecraft, nullptr, creeperExplode, true},
        {"diamond", "Diamond", "💎", EffectGroup::Minecraft, "Diamonds!", diamond, true},
        {"sword", "Sword", "🗡️", EffectGroup::Minecraft, "Swish! Swoosh!", sword, true},
        {"enderman", "Enderman", "🟪", EffectGroup::Minecraft, "Don't look at the Enderman!", enderman, true},
        {"emerald", "Emerald trade", "💚", EffectGroup::Minecraft, nullptr, emeraldTrade, true},
        {"panda", "Panda", "🐼", EffectGroup::Minecraft, "I'm a hungry panda. Munch, munch, munch!", panda, true},
        {"mining", "Mining", "⛏️", EffectGroup::Minecraft, "Let's go mining!", mine, true},
        {"dig", "Shovel", "🪏", EffectGroup::Minecraft, "Dig, dig, dig!", dig, true},
        {"chop", "Chop a tree", "🪓", EffectGroup::Minecraft, "Look, a little sapling is growing!", chopTree, true},
        {"golem", "Iron golem", "🌹", EffectGroup::Minecraft, "Hello. I'm an iron golem.", ironGolem, true},
        {"lamp", "Redstone lamp", "💡", EffectGroup::Minecraft, "Redstone power!", redstoneLamp, true},
        {"tnt", "Redstone TNT", "🧨", EffectGroup::Minecraft, "Uh oh. Redstone, and TNT!", redstoneTnt, true},
        {"ghast", "Ghast", "🔥", EffectGroup::Minecraft, "Watch out, a ghast!", ghastFireball, true},
        {"skeleton", "Skeleton", "🏹", EffectGroup::Minecraft, "A skeleton with a bow and arrow!", skeletonArrow, true},
        {"slime", "Slime", "🟢", EffectGroup::Minecraft, nullptr, slimeBounce, true},
        {"daynight", "Day and night", "🌙", EffectGroup::Minecraft, nullptr, dayAndNight, true},
        {"portal", "Nether portal", "🌀", EffectGroup::Minecraft, "The Nether portal is opening!", netherPortal, true},
        // Eyes
        {"blink", "Stare and blink", "👀", EffectGroup::Eyes, "Blink blink.", blink, true},
        {"winkleft", "Wink left", "😉", EffectGroup::Eyes, "Wink!", winkLeft, true},
        {"winkright", "Wink right", "😜", EffectGroup::Eyes, "Wink wink!", winkRight, true},
        {"roll", "Roll eyes", "🙄", EffectGroup::Eyes, "Whatever!", rollEyes, true},
        {"lookdown", "Look down", "👇", EffectGroup::Eyes, "What's down there?", lookDown, true},
        {"grow", "Grow eyes", "🔍", EffectGroup::Eyes, "Whoa!", growEyes, true},
        {"angry", "Angry", "😠", EffectGroup::Eyes, "Grrrr! I'm really cross!", angryEyes, true},
        {"sleepy", "Sleepy", "😴", EffectGroup::Eyes, "I'm so sleepy. Snore... snore...", sleepyEyes, true},
        {"crosseyed", "Cross-eyed", "🤪", EffectGroup::Eyes, "Ooh, I'm going cross-eyed!", crossEyed, true},
        // Games
        {"dice", "Roll the dice", "🎲", EffectGroup::Games, "Rolling the dice!", rollDice, true},
        {"magic", "Magic pumpkin", "🎱", EffectGroup::Games, "Hmm, let me think.", magicAnswer, true},
        {"boo", "3, 2, 1… BOO!", "😱", EffectGroup::Games, nullptr, countdownBoo, true},
        {"fruit", "Fruit machine", "🎰", EffectGroup::Games, "Spin to win!", fruitMachine, true},
        {"surprise", "Surprise show", "🔀", EffectGroup::Games, "Surprise!", surpriseShow, false},
        // Words
        {"name", "My name", "⭐", EffectGroup::Words, nullptr, scrollName, true}, // says the name itself
        {"spell", "Witches' spell", "🧙", EffectGroup::Words, "Double, double, toil and trouble. Fire burn, and cauldron bubble!", witchesSpell, false},
        {"trick", "Trick or Treat?", "🍬", EffectGroup::Words, "Trick or treat!", trickOrTreat, true},
        {"halloween", "Happy Halloween!", "🦉", EffectGroup::Words, "Happy Halloween!", happyHalloween, true},
    };

    constexpr size_t kEffectCount = std::size(kEffects);
}

const EffectInfo *allEffects(size_t &count)
{
    count = kEffectCount;
    return kEffects;
}

const EffectInfo *findEffect(const char *id)
{
    if (!id)
        return nullptr;
    for (const EffectInfo &effect : kEffects)
        if (std::strcmp(effect.id, id) == 0)
            return &effect;
    return nullptr;
}

const char *groupName(EffectGroup group)
{
    switch (group)
    {
    case EffectGroup::Pictures:
        return "Pictures";
    case EffectGroup::Minecraft:
        return "Minecraft";
    case EffectGroup::Eyes:
        return "Eyes";
    case EffectGroup::Games:
        return "Games";
    case EffectGroup::Words:
        return "Words";
    }
    return "";
}

const char *groupTitle(EffectGroup group)
{
    return group == EffectGroup::Minecraft ? MINECRAFT_TITLE : groupName(group);
}

void playEffect(Engine &engine, const EffectInfo &effect)
{
    if (effect.phrase)
        engine.say(effect.phrase);
    effect.run(engine);
}

void scrollWords(Engine &engine, const char *words)
{
    scrollPadded(engine, textFont, words);
}

void surpriseShow(Engine &engine)
{
    surpriseShowFrom(engine, nullptr);
}

void surpriseShowFrom(Engine &engine, bool (*pick)(const EffectInfo &effect))
{
    // The old surprise show picked from the effect ids in alphabetical order, so the same random
    // numbers pick the same effects here
    const EffectInfo *mix[kEffectCount];
    size_t mixCount = 0;
    for (const EffectInfo &effect : kEffects)
    {
        if (!effect.inSurprise || (pick && !pick(effect)))
            continue;
        size_t at = mixCount++;
        for (; at > 0 && std::strcmp(mix[at - 1]->id, effect.id) > 0; at--)
            mix[at] = mix[at - 1];
        mix[at] = &effect;
    }
    if (mixCount == 0)
        return; // nothing to pick from

    while (true)
    {
        playEffect(engine, *mix[engine.random(static_cast<uint32_t>(mixCount))]);
        engine.wait(500);
    }
}
