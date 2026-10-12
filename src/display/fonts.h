#pragma once
// The fonts: textFont for words, and the sprite fonts, where each letter is one picture, so
// scrolling a "message" like " 12 " flies the picture across the eyes. Missing characters are blank.
// Generated once from the MicroPython originals by tools/gen_sprites.py; edit by hand from now on.

#include "display/font.h"

// Letters, numbers and symbols for scrolling words.
// There's no '$' (it scrolls as a gap), and ';' is a copy of '?'.
extern const Font textFont;
// Witch on a broomstick: 16 dots wide, so split into two halves ('1' front, '2' back)
// Scroll the message " 12 " to fly her across the eyes
extern const Font witchFont;
// Bat: wings up ('a') and wings down ('b'), for flapping as it flies across the eyes
extern const Font batFont;
// Pac-Man chasing a ghost: ghost 'g'/'h' (wiggly bottom), Pac-Man 'o' (mouth open) / 'c' (shut)
extern const Font pacmanFont;
