#pragma once
// Every picture from the pumpkin, one 8x8 Frame each. The eyes and many shapes are from GurgleApps'
// original pumpkin (GPL); the rest are the family's own.
//
// Names are the old Python names, except all_off and all_on, which became allOff and allOn.
// Pictures shorter than 8 rows (spider has 4, slime has 5) are padded with blank rows at the bottom.
// That looks the same everywhere they're used: lower() pads with blank rows anyway, and spider_drop()
// drew blank rows below the spider.

#include "display/frame.h"

namespace Eyes
{
    extern const Frame straight;
    extern const Frame straightX2;
    extern const Frame straightX3;
    extern const Frame straightX4;
    extern const Frame straightX2Left1;
    extern const Frame straightX2Left2;
    extern const Frame straightX2Left3;
    extern const Frame straightX2Left4;
    extern const Frame straightX2Left5;
    extern const Frame straightR2;
    extern const Frame noEyeball;
    extern const Frame straightBlink1;
    extern const Frame straightBlink2;
    extern const Frame straightBlink3;
    extern const Frame straightBlinkLine;
    extern const Frame allOff;  // 'all_off'
    extern const Frame up1;
    extern const Frame up2;
    extern const Frame up3;
    extern const Frame up4;
    extern const Frame upLeft;
    extern const Frame upLeft1;
    extern const Frame upLeft2;
    extern const Frame upRight1;
    extern const Frame upRight;
    extern const Frame down1;
    extern const Frame down2;
    extern const Frame down3;
    extern const Frame down4;
    extern const Frame downRight;
    extern const Frame downRight1;
    extern const Frame downRight2;
    extern const Frame downLeft;
    extern const Frame downLeftB;
    extern const Frame downLeft1;
    extern const Frame downLeft1Blink1;
    extern const Frame downLeft1Blink2;
    extern const Frame downLeft1Blink3;
    extern const Frame downLeft2;
    extern const Frame left1;
    extern const Frame left2;
    extern const Frame left3;
    extern const Frame left4;
    extern const Frame right1;
    extern const Frame right2;
    extern const Frame right3;
    extern const Frame right4;
    extern const Frame ghost1;
    extern const Frame ghost2;
    // Minecraft eyes: a square with a square pupil in the middle
    extern const Frame minecraft;
    extern const Frame minecraftLeft;
    extern const Frame minecraftRight;
    extern const Frame minecraftBlink1;
    extern const Frame minecraftBlink2;
    extern const Frame minecraftBlink3;
    // Mood eyes: angry (frowning towards the nose), and sleepy lids drooping shut
    extern const Frame angryLeft;
    extern const Frame angryRight;
    extern const Frame sleepy1;
    extern const Frame sleepy2;
    extern const Frame sleepy3;
    extern const Frame sleepy4;
    // Sad eyes, looking down: the angry eyes' lids the other way round, drooping at the outside
    extern const Frame sadLeft;
    extern const Frame sadRight;
    // Enderman face: two thin eyes, small enough to fit in one pumpkin eye
    extern const Frame enderFace;
}

namespace Shapes
{
    extern const Frame smile;
    extern const Frame smileL;
    extern const Frame empty;
    extern const Frame allOn;  // 'all_on'
    extern const Frame arrow;
    extern const Frame invader1;
    extern const Frame invader2;
    extern const Frame tree1;
    extern const Frame tree1lit1;
    extern const Frame tree1lit2;
    extern const Frame tree2;
    extern const Frame bunny1;
    extern const Frame bunny2;
    extern const Frame bunny3;
    extern const Frame danbo;
    extern const Frame clock1;
    extern const Frame heart1F;
    extern const Frame heart1;
    extern const Frame heart2;
    extern const Frame heart2F;
    extern const Frame santaHat;
    extern const Frame santaHat2;
    extern const Frame star1;
    extern const Frame star2;
    extern const Frame star3;
    extern const Frame star4;
    extern const Frame star5;
    extern const Frame star6;
    extern const Frame star7;
    extern const Frame star8;
    extern const Frame star9;
    extern const Frame star10;
    extern const Frame star11;
    // Pumpkins: smiling, then mouth wide open
    extern const Frame pumpkin1;
    extern const Frame pumpkin2;
    extern const Frame doodle1;
    // Spider (4 rows tall, so it can drop down on its thread)
    extern const Frame spider;
    // Skull: mouth shut, then jaw dropped
    extern const Frame skull1;
    extern const Frame skull2;
    // Cat: eyes open, then shut
    extern const Frame cat1;
    extern const Frame cat2;
    // Minecraft things
    extern const Frame creeper;
    extern const Frame creeperFlash;
    extern const Frame diamond1;
    extern const Frame diamond2;
    extern const Frame sword1;
    extern const Frame sword2;
    // More Minecraft: villager (with nodding frame) and the emerald he trades for
    extern const Frame villager;
    extern const Frame villagerNod;
    extern const Frame emerald1;
    extern const Frame emerald2;
    // Panda: normal, chewing, and screwing its eyes up before a sneeze
    extern const Frame panda1;
    extern const Frame panda2;
    extern const Frame pandaSqueeze;
    // Mining: pickaxe, and the things you can find (each has a sparkle frame)
    extern const Frame pickaxe;
    extern const Frame iron1;
    extern const Frame iron2;
    extern const Frame redstone1;
    extern const Frame redstone2;
    // Digging: shovel and a flying clump of dirt
    extern const Frame shovel;
    extern const Frame dirt;
    // Wood: a sapling growing into an oak tree, the axe, the fallen tree and a log
    extern const Frame sapling1;
    extern const Frame sapling2;
    extern const Frame sapling3;
    extern const Frame oak;
    extern const Frame fallenTree;
    extern const Frame log;
    extern const Frame axe;
    // Iron golem and the poppy it gives you
    extern const Frame golem;
    extern const Frame poppy;
    // Redstone: torch, lamp (off, on, glowing) and TNT (bottom row is left free for the wire)
    extern const Frame torch;
    extern const Frame lampOff;
    extern const Frame lampOn;
    extern const Frame lampOn2;
    extern const Frame tnt;
    extern const Frame tntFlash;
    // Ghast (calm, then angry) and its fireball
    extern const Frame ghast1;
    extern const Frame ghast2;
    extern const Frame fireball1;
    extern const Frame fireball2;
    // Skeleton, its arrow and a target
    extern const Frame skeleton;
    extern const Frame bowArrow;
    extern const Frame target;
    // Slime: 5 rows tall so it can hop, and squished flat
    extern const Frame slime;
    extern const Frame slimeFlat;
    // Day and night
    extern const Frame sun;
    extern const Frame moon;
    // Dice faces 1-6
    extern const Frame dice1;
    extern const Frame dice2;
    extern const Frame dice3;
    extern const Frame dice4;
    extern const Frame dice5;
    extern const Frame dice6;
    // Fruit machine: the symbols on the reels (JACK on the left reel and POT on the right make the
    // jackpot), and WIN! and LOSE split across the eyes
    extern const Frame cherries;
    extern const Frame bell;
    extern const Frame bar;
    extern const Frame melon;
    extern const Frame doubleBar;
    extern const Frame jack;
    extern const Frame pot;
    extern const Frame winLeft;   // "WI"
    extern const Frame winRight;  // "N!"
    extern const Frame loseLeft;  // "LO"
    extern const Frame loseRight; // "SE"
}
