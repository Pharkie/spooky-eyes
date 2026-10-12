#pragma once
// A font maps characters to 8x8 pictures. Scrolling text uses textFont; the sprite fonts
// (witch, bat, pacman) use letters as animation frames, so a "message" like " 12 " flies a picture.

#include <cstddef>
#include "display/frame.h"

struct Glyph
{
    char ch;
    Frame rows;
};

struct Font
{
    const Glyph *glyphs;
    size_t count;

    // The picture for c, or blank if the font doesn't have it
    const Frame &get(char c) const
    {
        for (size_t i = 0; i < count; i++)
            if (glyphs[i].ch == c)
                return glyphs[i].rows;
        return kBlank;
    }
};
