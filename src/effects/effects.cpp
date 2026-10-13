// Every effect, ported from the old main.py and the button page's list of what each button ran.
// Timings, pictures and random numbers match the originals exactly: see primitives.h for the rules.

#include "effects/effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include "display/fonts.h"
#include "display/sprites.h"
#include "effects/animations.h"
#include "effects/primitives.h"

namespace
{
    namespace E = Eyes;
    namespace S = Shapes;

    // --- Pictures worked out as they go ---

    // A bamboo stalk with joints and a leaf on top, some rows tall
    Frame bambooStalk(int height)
    {
        Frame rows{};
        for (int row = 8 - height; row < 8; row++)
            rows[row] = (row % 3 == 1) ? 0x3c : 0x18;
        if (height)
            rows[8 - height] |= 0x06;
        return rows;
    }

    // Where the pickaxe knocks chips out of the block, in order: (row, column)
    constexpr uint8_t kCracks[][2] = {{3, 3}, {4, 4}, {4, 3}, {2, 2}, {5, 5}, {2, 4}, {5, 2}, {1, 1},
                                      {6, 6}, {1, 5}, {6, 2}, {3, 6}, {4, 1}, {0, 3}, {7, 4}};

    // A stone block with the first few cracks knocked out of it
    Frame crackedBlock(int cracks)
    {
        Frame rows = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        for (int i = 0; i < cracks; i++)
            rows[kCracks[i][0]] &= static_cast<uint8_t>(~(0x80 >> kCracks[i][1]));
        return rows;
    }

    // A pile of dirt, some rows high
    Frame dirtPile(int height)
    {
        Frame rows{};
        for (int i = 0; i < height; i++)
            rows[7 - i] = static_cast<uint8_t>((0xff >> i) & (0xff << i));
        return rows;
    }

    // The tree leaning over as it falls: lean 7 is right over
    Frame leaningTree(const Frame &tree, int lean)
    {
        Frame rows{};
        for (int r = 0; r < 8; r++)
            rows[r] = static_cast<uint8_t>(tree[r] << (lean * (7 - r) / 7));
        return rows;
    }

    // A Nether portal frame with swirly dots inside, at time t
    Frame portalFrame(int t)
    {
        Frame rows{};
        rows[0] = 0xff;
        for (int r = 1; r < 7; r++)
        {
            uint8_t inside = 0;
            for (int c = 1; c < 7; c++)
                if ((r + c + t) % 4 == 0)
                    inside |= 0x80 >> c;
            rows[r] = 0x81 | inside;
        }
        rows[7] = 0xff;
        return rows;
    }

    // The spider hanging y rows down (negative = still above the top), with its thread above it
    Frame spiderFrame(int y)
    {
        Frame rows = lower(S::spider, y);
        for (int row = 0; row < std::min(y, 8); row++)
            rows[row] = 0x10;
        return rows;
    }

    // --- Things to say and find ---

    const char *const kMagicAnswers[] = {"Yes!", "No!", "Maybe...", "Definitely!", "No way!", "Ask again",
                                         "Of course!", "I think so"};

    struct MiningFind
    {
        const Frame *picture;
        const Frame *sparkle;
        const char *words; // "You found ...!"
    };

    const MiningFind kMiningFinds[] = {
        {&S::diamond1, &S::diamond2, "a diamond"},
        {&S::emerald1, &S::emerald2, "an emerald"},
        {&S::iron1, &S::iron2, "some iron"},
        {&S::redstone1, &S::redstone2, "some redstone"},
    };

    // A redstone signal zooms along the wire... and switches on a lamp, or lights the TNT!
    void redstone(Engine &engine, bool tnt)
    {
        const Frame &torch = S::torch;
        const Frame &target = tnt ? S::tnt : S::lampOff;
        const int far = rightEyeColumn(engine);
        for (int pos = -3; pos < far + 8; pos++)
        {
            uint8_t leftWire = 0x55;
            uint8_t rightWire = 0x55;
            for (int col = pos; col < pos + 3; col++)
            {
                if (col >= 0 && col < 8)
                    leftWire |= 0x80 >> col;
                else if (col >= far && col < far + 8)
                    rightWire |= 0x80 >> (col - far);
            }
            showFor(engine, withBottomRow(torch, leftWire), withBottomRow(target, rightWire), 30);
        }
        if (tnt)
        {
            engine.say("Shhhhhhh!"); // a hiss: phone voices spell "Sssss" out letter by letter
            for (int i = 0; i < 8; i++)
                showFor(engine, torch, (i % 2) ? S::tntFlash : S::tnt, 250);
            engine.say("BOOM!");
            explode(engine, EyeSetting::keep(torch));
            showFor(engine, torch, S::empty, 500);
        }
        else
        {
            engine.say("Ding! The lamp is on!");
            for (int i = 0; i < 10; i++)
            {
                const Frame &glow = (i % 2) ? S::lampOn2 : S::lampOn;
                showFor(engine, withBottomRow(torch, 0xff), withBottomRow(glow, 0xff), 200);
            }
        }
    }
}

// --- Pictures ---

void pumpkins(Engine &engine)
{
    flip(engine, S::pumpkin1, S::pumpkin2, 6, 400);
}

void witch(Engine &engine)
{
    scrollMessage(engine, witchFont, " 12 ");
}

void bat(Engine &engine)
{
    fly(engine, batFont, {" a ", " b "});
}

void spiderDrop(Engine &engine)
{
    // Down, a little bounce at the bottom, then back up twice as slowly
    constexpr uint32_t kDelayMs = 100;
    for (int y = -4; y <= 4; y++)
        showFor(engine, spiderFrame(y), spiderFrame(y), kDelayMs);
    for (int y : {3, 4, 3, 4})
        showFor(engine, spiderFrame(y), spiderFrame(y), kDelayMs);
    for (int y = 4; y >= -4; y--)
        showFor(engine, spiderFrame(y), spiderFrame(y), kDelayMs * 2);
}

void ghosts(Engine &engine)
{
    // The old show_char(ghost1, ghost2) put ghost1 on the right
    showFor(engine, E::ghost2, E::ghost1, 1000);
}

void skull(Engine &engine)
{
    flip(engine, S::skull1, S::skull2, 10, 150);
}

void catBlink(Engine &engine)
{
    const Frame &awake = S::cat1;
    const Frame &shut = S::cat2;
    sequence(engine, {{awake, awake, 1200}, {shut, shut, 150}, {awake, awake, 800}, {shut, shut, 150},
                      {awake, awake, 150}, {shut, shut, 150}, {awake, awake, 1000}});
}

void pacMan(Engine &engine)
{
    fly(engine, pacmanFont, {" g o ", " h c "}, 40, 4);
}

void invaders(Engine &engine)
{
    flip(engine, S::invader1, S::invader2, 8, 300);
}

void pong(Engine &engine)
{
    constexpr int kSteps = 120;
    constexpr uint32_t kDelayMs = 40;
    const int far = rightEyeColumn(engine);
    const int width = far + 8; // both eyes and the gap between them
    int x = 2;
    int y = static_cast<int>(engine.random(7)); // random.randint(0, 6)
    int dx = 1;
    int dy = 1;
    for (int i = 0; i < kSteps; i++)
    {
        x += dx;
        y += dy;
        if (x <= 1 || x >= width - 3)
            dx = -dx;
        if (y <= 0 || y >= 6)
            dy = -dy;
        Frame left{};
        Frame right{};
        const int paddle = std::min(std::max(y - 1, 0), 5);
        for (int row = paddle; row < paddle + 3; row++)
        {
            left[row] |= 0x80;
            right[row] |= 0x01;
        }
        for (int col = x; col <= x + 1; col++)
        {
            for (int row = y; row <= y + 1; row++)
            {
                // If the ball starts on row 6, its first frame reaches row 8, below the eye. The Python
                // crashed there (IndexError); here that half of the ball is just not drawn.
                if (row > 7)
                    continue;
                if (col < 8)
                    left[row] |= 0x80 >> col;
                else if (col >= far)
                    right[row] |= 0x80 >> (col - far);
            }
        }
        showFor(engine, left, right, kDelayMs);
    }
}

void starSpin(Engine &engine)
{
    constexpr int kTurns = 3;
    constexpr uint32_t kSpinMs = 80;
    const Frame *const star[] = {nullptr, &S::star1, &S::star2, &S::star3, &S::star4, &S::star5, &S::star6,
                                 &S::star7, &S::star8, &S::star9, &S::star10, &S::star11};
    for (int n : {1, 2, 3, 4})
        showFor(engine, *star[n], *star[n], 150);
    for (int turn = 0; turn < kTurns; turn++)
        for (int n = 5; n <= 11; n++)
            showFor(engine, *star[n], *star[n], kSpinMs);
    for (int n : {4, 3, 2, 1})
        showFor(engine, *star[n], *star[n], 150);
    showFor(engine, S::empty, S::empty, 300);
}

void bunnies(Engine &engine)
{
    flip(engine, S::bunny1, S::bunny3, 8, 250);
}

void trees(Engine &engine)
{
    flip(engine, S::tree1, S::tree1lit2, 6, 400);
}

void hearts(Engine &engine)
{
    flip(engine, S::heart1, S::heart2, 3, 500);
}

// --- Minecraft ---

void minecraft(Engine &engine)
{
    runAnimation(engine, Animations::minecraft);
}

void creeperExplode(Engine &engine)
{
    const Frame &creeper = S::creeper;
    const Frame &flash = S::creeperFlash;
    showFor(engine, creeper, creeper, 1500);
    engine.say("Shhhhhhhhhh!"); // the hiss (spelt so phone voices make the sound)
    for (int i = 0; i < 10; i++)
    {
        const Frame &face = (i % 2) ? flash : creeper;
        showFor(engine, face, face, secondsToMs(0.3 - i * 0.025)); // flashing faster and faster
    }
    engine.say("BOOM!");
    explode(engine);
    showFor(engine, S::empty, S::empty, 500);
}

void diamond(Engine &engine)
{
    flip(engine, S::diamond1, S::diamond2, 8, 300);
}

void sword(Engine &engine)
{
    flip(engine, S::sword1, S::sword2, 8, 200);
}

void enderman(Engine &engine)
{
    const Frame &face = E::enderFace;
    const Frame &blank = E::allOff;
    bool onLeft = true;
    auto stare = [&](uint32_t ms) {
        if (onLeft)
            showFor(engine, face, blank, ms);
        else
            showFor(engine, blank, face, ms);
    };
    for (uint32_t stayMs : {2000, 1500, 1000, 700, 500})
    {
        stare(stayMs);
        engine.say("Whoosh!");
        sparkles(engine, 5, 3, EyeSetting::sparkleIf(onLeft), EyeSetting::sparkleIf(!onLeft));
        onLeft = !onLeft;
        sparkles(engine, 5, 3, EyeSetting::sparkleIf(onLeft), EyeSetting::sparkleIf(!onLeft));
    }
    stare(1500);
}

void emeraldTrade(Engine &engine)
{
    const Frame &villager = S::villager;
    const Frame &nod = S::villagerNod;
    const Frame &blank = S::empty;
    const int far = rightEyeColumn(engine);
    for (int i = 0; i < 3; i++)
        sequence(engine, {{villager, S::emerald1, 300}, {villager, S::emerald2, 300}});
    engine.say("Hurrmm?"); // the villager's "hrmm" (with a vowel, or phone voices spell it out)
    slide(engine, {S::emerald1, S::emerald2}, far, 8, villager, kBlank, 60);
    engine.say("Hurrmm! Hurrmm!");
    for (int i = 0; i < 3; i++)
        sequence(engine, {{nod, blank, 150}, {villager, blank, 150}});
    slide(engine, {S::diamond1}, 8, far, villager, kBlank, 60);
    for (int i = 0; i < 3; i++)
        sequence(engine, {{villager, S::diamond1, 300}, {villager, S::diamond2, 300}});
}

void panda(Engine &engine)
{
    const Frame &face = S::panda1;
    const Frame &chew = S::panda2;
    const Frame &squeeze = S::pandaSqueeze;
    const Frame &blank = S::empty;
    for (int height = 8; height >= 0; height--)
    {
        const Frame bamboo = bambooStalk(height);
        sequence(engine, {{face, bamboo, 250}, {chew, bamboo, 150}});
    }
    engine.say("Ah... ah... ACHOO!");
    sequence(engine, {{squeeze, blank, 600}, {face, blank, 300}, {squeeze, blank, 600}});
    sparkles(engine, 8, 2, EyeSetting::keep(lower(face, -1))); // face jolted up a row by the sneeze
    showFor(engine, face, blank, 1000);
}

void mine(Engine &engine)
{
    const Frame &pick = S::pickaxe;
    const Frame swing = lower(place(pick, 2), 1);
    for (int hit = 0; hit < 5; hit++)
        sequence(engine, {{pick, crackedBlock(hit * 3), 350}, {swing, crackedBlock(hit * 3 + 3), 150}});
    sparkles(engine, 6, 2, EyeSetting::keep(pick));
    const MiningFind &find = kMiningFinds[engine.random(std::size(kMiningFinds))];
    char words[40];
    std::snprintf(words, sizeof(words), "You found %s!", find.words);
    engine.say(words);
    for (int i = 0; i < 4; i++)
        sequence(engine, {{pick, *find.picture, 300}, {pick, *find.sparkle, 300}});
}

void dig(Engine &engine)
{
    const Frame &shovel = S::shovel;
    const int far = rightEyeColumn(engine);
    for (int scoop = 1; scoop < 5; scoop++)
    {
        const Frame pile = dirtPile(scoop - 1);
        sequence(engine, {{shovel, pile, 300}, {lower(shovel, 2), pile, 250}});
        slide(engine, {S::dirt}, 0, far, shovel, pile, 15);
        showFor(engine, shovel, dirtPile(scoop), 300);
    }
    showFor(engine, shovel, dirtPile(4), 1000);
}

void chopTree(Engine &engine)
{
    const Frame &tree = S::oak;
    const Frame &axe = S::axe;
    const Frame &blank = S::empty;
    sequence(engine, {{S::sapling1, blank, 600}, {S::sapling2, blank, 600}, {S::sapling3, blank, 600}});
    sequence(engine, {{tree, blank, 800}, {tree, axe, 800}});
    for (int i = 0; i < 3; i++)
    {
        engine.say("Chop!");
        sequence(engine, {{tree, place(axe, -1), 150}, {place(tree, 1), axe, 250}, {tree, axe, 300}});
    }
    engine.say("Timber!");
    for (int lean : {1, 2, 4, 7})
        showFor(engine, leaningTree(tree, lean), axe, 120);
    showFor(engine, S::fallenTree, axe, 300);
    sparkles(engine, 5, 3, EyeSetting::sparkle(), EyeSetting::keep(axe));
    engine.say("You got some wood!");
    showFor(engine, S::log, axe, 2000);
}

void ironGolem(Engine &engine)
{
    const Frame &golem = S::golem;
    const Frame &poppy = S::poppy;
    showFor(engine, golem, S::empty, 1500);
    slide(engine, {poppy}, 8, rightEyeColumn(engine), golem, kBlank, 60);
    engine.say("Here's a flower for you!");
    showFor(engine, golem, poppy, 1500);
    for (int i = 0; i < 3; i++)
        sequence(engine, {{golem, S::heart1, 400}, {golem, S::heart2, 400}});
    showFor(engine, golem, poppy, 1000);
}

void redstoneLamp(Engine &engine)
{
    redstone(engine, false);
}

void redstoneTnt(Engine &engine)
{
    redstone(engine, true);
}

void ghastFireball(Engine &engine)
{
    constexpr int kShots = 2;
    const Frame &calm = S::ghast1;
    const Frame &angry = S::ghast2;
    for (int shot = 0; shot < kShots; shot++)
    {
        sequence(engine, {{calm, S::empty, 1200}, {angry, S::empty, 400}});
        engine.say("Waaaaah!");
        slide(engine, {S::fireball1, S::fireball2}, 8, rightEyeColumn(engine), angry, kBlank, 80);
        engine.say("Boom!");
        explode(engine, EyeSetting::keep(calm));
    }
    showFor(engine, calm, S::empty, 1000);
}

void skeletonArrow(Engine &engine)
{
    const Frame &skeleton = S::skeleton;
    const Frame &target = S::target;
    const Frame &arrow = S::bowArrow;
    showFor(engine, skeleton, target, 1200);
    engine.say("Twang!");
    slide(engine, {arrow}, 8, rightEyeColumn(engine) - 3, skeleton, target, 40);
    engine.say("Thwack! Bullseye!");
    // The arrow wobbles in the target: up a row, back, down a row, back (twice)
    const Frame wobbles[] = {lower(arrow, -1), arrow, lower(arrow, 1), arrow};
    for (int i = 0; i < 2; i++)
        for (const Frame &wobble : wobbles)
            showFor(engine, skeleton, overlay(target, place(wobble, -3)), 80);
    engine.wait(1500);
}

void slimeBounce(Engine &engine)
{
    constexpr int kHops = 4;
    const Frame &tall = S::slime;
    const Frame &flat = S::slimeFlat;
    const int far = rightEyeColumn(engine);
    int x = far;
    for (int hop = 0; hop < kHops; hop++)
    {
        const int target = x ? 0 : far;
        int step = 0;
        for (int height : {0, 1, 2, 3, 3, 3, 2, 1, 0})
        {
            const Frame picture = lower(tall, 3 - height);
            const int at = x + floorDiv((target - x) * step, 8);
            showFor(engine, place(picture, at), place(picture, at - far), 50);
            step++;
        }
        x = target;
        engine.say("Squish!");
        showFor(engine, place(flat, x), place(flat, x - far), 200);
    }
    const Frame sitting = lower(tall, 3);
    showFor(engine, place(sitting, x), place(sitting, x - far), 1000);
}

void dayAndNight(Engine &engine)
{
    const Frame &sun = S::sun;
    const Frame &moon = S::moon;
    engine.say("The sun is setting.");
    for (int k = 0; k < 9; k++)
        showFor(engine, lower(sun, k), lower(moon, 8 - k), 250);
    engine.say("Goodnight!");
    for (int i = 0; i < 20; i++)
    {
        const Frame stars = randomDotsPicture(engine, 4);
        showFor(engine, stars, moon, 150);
    }
    engine.say("Good morning!");
    for (int k = 0; k < 9; k++)
        showFor(engine, lower(sun, 8 - k), lower(moon, k), 250);
    engine.wait(1000);
}

void netherPortal(Engine &engine)
{
    for (int t = 0; t < 30; t++)
        showFor(engine, portalFrame(t), portalFrame(t + 2), 80);
    const Frame &ghast = S::ghast2;
    const Frame emptyPortal = {0xff, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xff};
    slide(engine, {ghast}, 0, rightEyeColumn(engine), emptyPortal, kBlank, 50);
    engine.say("Waaaaah!");
    for (int t = 0; t < 20; t++)
        showFor(engine, portalFrame(t), ghast, 80);
}

// --- Eyes ---

void blink(Engine &engine)
{
    runAnimation(engine, Animations::stareAndBlink);
}

void winkLeft(Engine &engine)
{
    runAnimation(engine, Animations::winkLeft);
}

void winkRight(Engine &engine)
{
    runAnimation(engine, Animations::winkRight);
}

void rollEyes(Engine &engine)
{
    runAnimation(engine, Animations::roll);
}

void lookDown(Engine &engine)
{
    runAnimation(engine, Animations::downLeftABit);
}

void growEyes(Engine &engine)
{
    runAnimation(engine, Animations::growEyes);
}

void angryEyes(Engine &engine)
{
    const Frame &left = E::angryLeft;
    const Frame &right = E::angryRight;
    sequence(engine, {{E::straight, E::straight, 500}, {left, right, 1000}});
    for (int i = 0; i < 8; i++)
    {
        // Shaking: down a row and back
        if (i % 2)
            showFor(engine, left, right, 70);
        else
            showFor(engine, lower(left, 1), lower(right, 1), 70);
    }
    sequence(engine, {{left, right, 1500}, {E::straight, E::straight, 300}});
}

void sleepyEyes(Engine &engine)
{
    sequence(engine, {{E::straight, E::straight, 800}, {E::sleepy1, E::sleepy1, 500}, {E::sleepy2, E::sleepy2, 800},
                      {E::sleepy3, E::sleepy3, 1000}, {E::sleepy2, E::sleepy2, 300}, {E::sleepy3, E::sleepy3, 800},
                      {E::sleepy4, E::sleepy4, 2500}, {E::straight, E::straight, 800}});
}

void crossEyed(Engine &engine)
{
    sequence(engine, {{E::straight, E::straight, 600}, {E::right1, E::left1, 150}, {E::right2, E::left2, 2000},
                      {E::right1, E::left1, 150}, {E::straight, E::straight, 600}, {E::right2, E::left2, 1200},
                      {E::straight, E::straight, 500}});
}

// --- Games ---

void rollDice(Engine &engine)
{
    const Frame *const dice[] = {&S::dice1, &S::dice2, &S::dice3, &S::dice4, &S::dice5, &S::dice6};
    double delay = 0.05; // seconds, slowing down as the dice settle
    int a = 0;
    int b = 0;
    while (delay < 0.4)
    {
        a = 1 + static_cast<int>(engine.random(6)); // random.randint(1, 6)
        b = 1 + static_cast<int>(engine.random(6));
        showFor(engine, *dice[a - 1], *dice[b - 1], secondsToMs(delay));
        delay *= 1.25;
    }
    char words[48];
    std::snprintf(words, sizeof(words), "You rolled %d and %d. That makes %d!", a, b, a + b);
    engine.say(words);
    engine.wait(3000);
}

void magicAnswer(Engine &engine)
{
    sequence(engine, {{E::upLeft, E::upLeft, 500}, {E::upRight, E::upRight, 500}, {E::upLeft, E::upLeft, 500},
                      {E::up1, E::up1, 600}});
    const char *answer = kMagicAnswers[engine.random(std::size(kMagicAnswers))];
    engine.say(answer);
    scrollPadded(engine, textFont, answer, 30);
}

void countdownBoo(Engine &engine)
{
    const struct
    {
        char digit;
        const char *word;
    } counts[] = {{'3', "Three"}, {'2', "Two"}, {'1', "One"}};
    for (const auto &count : counts)
    {
        engine.say(count.word);
        const Frame &digit = textFont.get(count.digit);
        showFor(engine, digit, digit, 1000);
    }
    engine.say("BOO!");
    engine.setBrightness(flashBrightness(engine));
    for (int i = 0; i < 6; i++)
    {
        const Frame &flash = (i % 2 == 0) ? S::allOn : S::empty;
        showFor(engine, flash, flash, 80);
    }
    engine.setBrightness(engine.maxBright());
    scrollMessage(engine, textFont, " BOO! ", 20);
}

void fruitMachine(Engine &engine)
{
    const uint32_t roll = engine.random(fruit_machine::kRolls);
    const uint32_t pick = engine.random(fruit_machine::kPicks);
    fruit_machine::play(engine, fruit_machine::spinFor(roll, pick));
}

// --- Words ---

// The pumpkin's own name, e.g. "Jack's Pumpkin" (the old button page always scrolled one fixed name)
void scrollName(Engine &engine)
{
    engine.say(engine.pumpkinName());
    scrollPadded(engine, textFont, engine.pumpkinName());
}

void witchesSpell(Engine &engine)
{
    scrollMessage(engine, textFont, " Double, double toil and trouble Fire burn and caldron bubble ", 30);
}

void trickOrTreat(Engine &engine)
{
    scrollMessage(engine, textFont, " Trick or Treat? ", 20);
}

void happyHalloween(Engine &engine)
{
    scrollMessage(engine, textFont, " Happy Halloween! ", 30);
}

// --- Inside the fruit machine ---

namespace
{
    using fruit_machine::kSymbols;
    using fruit_machine::Spin;
    using fruit_machine::Symbol;

    // The reels: the same symbols in the same order, but JACK on the left one and POT on the right
    const Frame *const kLeftReel[kSymbols] = {&S::cherries, &S::bell, &S::bar, &S::melon, &S::doubleBar, &S::jack};
    const Frame *const kRightReel[kSymbols] = {&S::cherries, &S::bell, &S::bar, &S::melon, &S::doubleBar, &S::pot};

    // Each symbol is 8 rows tall, with 2 blank rows before the next one round the reel
    constexpr int kReelPitch = 10;
    constexpr uint32_t kSpinFrameMs = 35;

    // A reel some rows into its spin. The symbols move down, so the next one comes in at the top; every
    // kReelPitch rows a symbol fills the eye exactly (0 rows: the cherries).
    Frame reelPicture(const Frame *const reel[], int rows)
    {
        const int symbol = floorDiv(rows, kReelPitch);
        const int down = floorMod(rows, kReelPitch);
        const Frame &leaving = *reel[floorMod(symbol, kSymbols)];
        const Frame &coming = *reel[floorMod(symbol + 1, kSymbols)];
        return overlay(lower(leaving, down), lower(coming, down - kReelPitch));
    }

    // How a reel spins: at full speed, then slowing down evenly to a crawl as it reaches its symbol,
    // then a wobble as it lands
    struct ReelSpin
    {
        Symbol symbol;  // where it lands
        int speed;      // rows a frame at full speed
        int slowFrames; // frames it takes to slow down
        int landFrame;  // the frame it reaches its symbol
    };

    // How fast a reel is crawling when it reaches its symbol, in rows a frame
    constexpr double kCrawlSpeed = 0.5;

    // The wobble as a reel lands: rows past its symbol on each frame from landFrame on. It clunks a
    // little too far, bounces back the other way, then sits still.
    constexpr int kWobble[] = {0, 1, 2, 2, 1, 0, -1, -1, 0};
    constexpr int kWobbleFrames = static_cast<int>(std::size(kWobble));

    // Rows still to go when a reel is this many frames from its symbol, slowing down evenly from full
    // speed to kCrawlSpeed
    int slowingRows(const ReelSpin &reel, int frames)
    {
        const double extra = (reel.speed - kCrawlSpeed) * frames * frames / (2.0 * reel.slowFrames);
        return static_cast<int>(std::lround(kCrawlSpeed * frames + extra));
    }

    // How many rows into its spin a reel is on a frame (see reelPicture()). Worked out backwards from
    // where it lands, so it always lands exactly on its symbol, whatever the speeds.
    int reelRows(const ReelSpin &reel, int frame)
    {
        const int landing = static_cast<int>(reel.symbol) * kReelPitch;
        const int toGo = reel.landFrame - frame; // frames until it reaches its symbol
        if (toGo <= 0)
            return landing + kWobble[std::min(-toGo, kWobbleFrames - 1)];
        if (toGo <= reel.slowFrames)
            return landing - slowingRows(reel, toGo);
        return landing - slowingRows(reel, reel.slowFrames) - reel.speed * (toGo - reel.slowFrames);
    }

    // The landed symbols flashing, brighter than usual
    void flashSymbols(Engine &engine, const Spin &spin, int times)
    {
        const Frame &left = fruit_machine::picture(spin.left, false);
        const Frame &right = fruit_machine::picture(spin.right, true);
        for (int i = 0; i < times; i++)
        {
            engine.setBrightness(flashBrightness(engine));
            showFor(engine, left, right, 250);
            engine.setBrightness(engine.maxBright());
            showFor(engine, S::empty, S::empty, 150);
        }
    }

    // WIN! is too wide for one eye, so: WI on the left, N! on the right, WI again, then both together
    void winWords(Engine &engine, uint32_t ms)
    {
        const Frame &wi = S::winLeft;
        const Frame &n = S::winRight;
        const SequenceStep steps[] = {{wi, S::empty, ms}, {S::empty, n, ms}, {wi, S::empty, ms}, {wi, n, ms * 3}};
        for (const SequenceStep &step : steps)
        {
            engine.mouthRandomColour();
            showFor(engine, step.left, step.right, step.ms);
        }
    }

    // Coins (dots) rain down both eyes and pile up at the bottom. A coin that lands on another tumbles
    // down beside it if there's room, so the pile spreads out like a heap of treasure.
    void coinRain(Engine &engine, int frames)
    {
        Frame piles[2] = {};
        Frame falling[2] = {};
        for (int frame = 0; frame < frames; frame++)
        {
            for (int eye = 0; eye < 2; eye++)
            {
                Frame &pile = piles[eye];
                Frame next{};
                // Bottom row first, so a coin can land on one that has only just landed
                for (int row = 7; row >= 0; row--)
                {
                    for (int col = 0; col < 8; col++)
                    {
                        if (!(falling[eye][row] & (0x80 >> col)))
                            continue;
                        // Down if it can, else down to one side (which side it tries first changes every
                        // frame), else it stays where it is
                        const uint8_t below = row < 7 ? pile[row + 1] | next[row + 1] : 0xff; // full at the bottom
                        const int side = frame % 2 ? 1 : -1;
                        bool moved = false;
                        for (int to : {col, col + side, col - side})
                        {
                            if (to >= 0 && to < 8 && !(below & (0x80 >> to)))
                            {
                                next[row + 1] |= 0x80 >> to;
                                moved = true;
                                break;
                            }
                        }
                        if (!moved)
                            pile[row] |= 0x80 >> col;
                    }
                }
                next[0] |= 0x80 >> engine.random(8); // a new coin at the top
                falling[eye] = next;
            }
            if (frame % 4 == 0)
                engine.mouthRandomColour();
            showFor(engine, overlay(piles[0], falling[0]), overlay(piles[1], falling[1]), 40);
        }
    }

    void win(Engine &engine, const Spin &spin)
    {
        engine.say("You win!");
        flashSymbols(engine, spin, 3);
        winWords(engine, 400);
        sparkles(engine, 15);
        showFor(engine, S::winLeft, S::winRight, 1000);
    }

    void lose(Engine &engine)
    {
        engine.say("Oh no! Try again!");
        for (int i = 0; i < 2; i++)
            sequence(engine, {{S::loseLeft, S::loseRight, 350}, {S::empty, S::empty, 150}});
        showFor(engine, S::loseLeft, S::loseRight, 600);
        // The letters droop, then fall off the bottom, faster and faster
        for (int rows = 1; rows <= 8; rows++)
            showFor(engine, lower(S::loseLeft, rows), lower(S::loseRight, rows), 240 / rows);
        // Sad eyes, with a sigh
        const Frame &sadLeft = E::sadLeft;
        const Frame &sadRight = E::sadRight;
        sequence(engine,
                 {{sadLeft, sadRight, 700}, {lower(sadLeft, 1), lower(sadRight, 1), 500}, {sadLeft, sadRight, 300}});
    }

    void jackpot(Engine &engine)
    {
        engine.say("Jackpot!");
        for (int i = 0; i < 2; i++)
            sequence(engine, {{S::jack, S::empty, 300}, {S::empty, S::pot, 300}});
        engine.setBrightness(flashBrightness(engine));
        showFor(engine, S::jack, S::pot, 800);
        engine.setBrightness(engine.maxBright());
        scrollMessage(engine, textFont, " JACKPOT WIN! ", 20);
        coinRain(engine, 40);
        engine.setBrightness(flashBrightness(engine));
        sparkles(engine, 10, 1);
        engine.setBrightness(engine.maxBright());
        winWords(engine, 300);
        explode(engine);
    }
}

namespace fruit_machine
{
    Spin spinFor(uint32_t roll, uint32_t pick)
    {
        constexpr int kCanWin = kSymbols - 1; // every symbol but JACK and POT
        pick %= kPicks;
        if (roll == 0)
            return {Result::Jackpot, Symbol::JackOrPot, Symbol::JackOrPot};
        if (roll <= 3)
        {
            const Symbol both = static_cast<Symbol>(pick % kCanWin);
            return {Result::Win, both, both};
        }
        // Any symbol on the left (JACK with a fruit is a near miss), and any of the other 5 on the right
        const int left = static_cast<int>(pick) / kCanWin;
        const int right = (left + 1 + static_cast<int>(pick) % kCanWin) % kSymbols;
        return {Result::Lose, static_cast<Symbol>(left), static_cast<Symbol>(right)};
    }

    const Frame &picture(Symbol symbol, bool rightReel)
    {
        const int i = static_cast<int>(symbol) % kSymbols;
        return rightReel ? *kRightReel[i] : *kLeftReel[i];
    }

    void spinReels(Engine &engine, const Spin &spin)
    {
        // Both reels start together, the right one faster. The left one lands first, about 2 s in. The
        // right one spins on, then slows down and lands a second or so later, and more slowly still if
        // the left one shows JACK: will it be POT?
        const bool tease = spin.left == Symbol::JackOrPot;
        const ReelSpin left = {spin.left, 2, 30, 54}; // lands about 1.9 s in
        const ReelSpin right = {spin.right, 3, tease ? 54 : 30, tease ? 114 : 90}; // 4 s or 3.2 s in
        for (int frame = 0; frame < right.landFrame + kWobbleFrames; frame++)
        {
            if (frame % 6 == 0)
                engine.mouthRandomColour(); // the machine's flashing lights
            showFor(engine, reelPicture(kLeftReel, reelRows(left, frame)),
                    reelPicture(kRightReel, reelRows(right, frame)), kSpinFrameMs);
        }
    }

    void play(Engine &engine, const Spin &spin)
    {
        spinReels(engine, spin);
        engine.wait(500);
        switch (spin.result)
        {
        case Result::Win:
            win(engine, spin);
            break;
        case Result::Jackpot:
            jackpot(engine);
            break;
        case Result::Lose:
            lose(engine);
            break;
        }
    }
}
