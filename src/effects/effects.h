#pragma once
// Every effect, one function each, ported from the old main.py and the button page's list of what
// each button ran. Each plays once and returns (except surpriseShow, which carries on until interrupted).
// A six-year-old designed most of these, so they look and time exactly as the originals did.

#include "effects/engine.h"

// --- Pictures ---
void pumpkins(Engine &engine);  // Pumpkins chomping
void witch(Engine &engine);     // Witch flies across on her broomstick
void bat(Engine &engine);       // Bat flaps across the eyes
void spiderDrop(Engine &engine); // a spider drops down on its thread, bounces, and climbs back up
void ghosts(Engine &engine);
void skull(Engine &engine);     // jaw opening and shutting
void catBlink(Engine &engine);  // a cat face that blinks
void pacMan(Engine &engine);    // Pac-Man chases a ghost across the eyes
void invaders(Engine &engine);  // space invaders
void pong(Engine &engine);      // a ball bounces from eye to eye between two paddles
void starSpin(Engine &engine);  // a star grows, spins round, then shrinks away
void bunnies(Engine &engine);
void trees(Engine &engine);     // Christmas trees with twinkly lights
void hearts(Engine &engine);

// --- Minecraft ---
void minecraft(Engine &engine);      // Minecraft eyes look around and blink
void creeperExplode(Engine &engine); // a creeper stares, hisses and flashes... then BOOM
void diamond(Engine &engine);
void sword(Engine &engine);
void enderman(Engine &engine);     // stares out of one eye, then teleports from eye to eye, faster and faster
void emeraldTrade(Engine &engine); // a villager takes your emerald and gives you a diamond
void panda(Engine &engine);        // munches bamboo until it's all gone... then sneezes
void mine(Engine &engine);         // a pickaxe breaks a block open... what's inside?
void dig(Engine &engine);          // a shovel digs, and the pile of dirt grows with every scoop
void chopTree(Engine &engine);     // a sapling grows into a tree, then an axe chops it down
void ironGolem(Engine &engine);    // an iron golem gives you a poppy
void redstoneLamp(Engine &engine); // a redstone signal runs along the wire and switches on a lamp
void redstoneTnt(Engine &engine);  // ...or lights the TNT
void ghastFireball(Engine &engine); // a ghast wails and shoots fireballs into the other eye
void skeletonArrow(Engine &engine); // a skeleton fires an arrow into a target
void slimeBounce(Engine &engine);  // a slime hops from eye to eye, squishing flat as it lands
void dayAndNight(Engine &engine);  // the sun sets as the moon rises, then morning comes
void netherPortal(Engine &engine); // a swirly portal opens and a ghast floats out

// --- Eyes ---
void blink(Engine &engine);     // stare and blink
void winkLeft(Engine &engine);  // winks the eye on your right (the pumpkin's own left eye)
void winkRight(Engine &engine); // winks the eye on your left
void rollEyes(Engine &engine);
void lookDown(Engine &engine);
void growEyes(Engine &engine);
void angryEyes(Engine &engine);  // cross, frowning eyes that shake with rage
void sleepyEyes(Engine &engine); // eyelids droop, fall asleep, then wake up with a jump
void crossEyed(Engine &engine);  // both eyes look in at the nose

// --- Games ---
void rollDice(Engine &engine);     // two dice, one on each eye
void magicAnswer(Engine &engine);  // thinks hard, then answers yes, no, maybe...
void countdownBoo(Engine &engine); // 3, 2, 1... BOO!
void fruitMachine(Engine &engine); // the eyes are two reels: win 1 in 4, JACKPOT 1 in 16
void surpriseShow(Engine &engine); // random effects until interrupted (in registry.cpp, as it uses the list)

// --- Words ---
void scrollName(Engine &engine); // the pumpkin's name, e.g. "Jack's Pumpkin"
void witchesSpell(Engine &engine);
void trickOrTreat(Engine &engine);
void happyHalloween(Engine &engine);

// --- Inside the fruit machine (here so the tests can check the odds and where the reels stop) ---
namespace fruit_machine
{
    // The symbols, in order round both reels. The last one says JACK on the left reel and POT on the
    // right, so a pair of them is the jackpot.
    enum class Symbol : uint8_t
    {
        Cherries,
        Bell,
        Bar,
        Melon,
        DoubleBar,
        JackOrPot,
    };
    constexpr int kSymbols = 6;

    enum class Result : uint8_t
    {
        Lose,    // two different symbols
        Win,     // the same symbol on both reels
        Jackpot, // JACK and POT
    };

    struct Spin
    {
        Result result;
        Symbol left; // where each reel stops
        Symbol right;
    };

    // A spin needs two random numbers. The roll decides the result: 0 is the jackpot, 1 to 3 a win and
    // the rest a lose, so any win is 1 in 4 and the jackpot 1 in 16. The pick decides the symbols: 30
    // shares out evenly between the 5 symbols that can win, and between the 30 pairs of different
    // symbols (6 on the left x 5 others on the right) that lose.
    constexpr uint32_t kRolls = 16;
    constexpr uint32_t kPicks = 30;

    // Where the reels stop for a roll (0 to kRolls - 1) and a pick (0 to kPicks - 1)
    Spin spinFor(uint32_t roll, uint32_t pick);

    // A symbol's picture on one reel (they're the same on both, except JACK and POT)
    const Frame &picture(Symbol symbol, bool rightReel);

    // Spin both reels until they stop on the spin's symbols, the left reel first
    void spinReels(Engine &engine, const Spin &spin);

    // The whole effect for a spin: the reels, then the win, lose or jackpot
    void play(Engine &engine, const Spin &spin);
}
