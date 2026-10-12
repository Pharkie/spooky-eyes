#pragma once
// One 8x8 picture for one eye: 8 row bytes, top row first, most significant bit = leftmost dot.

#include <array>
#include <cstdint>

using Frame = std::array<uint8_t, 8>;

constexpr Frame kBlank = {0, 0, 0, 0, 0, 0, 0, 0};

// A picture moved x columns to the right (or left if x is negative). Dots pushed off the edge are lost.
constexpr Frame place(const Frame &picture, int x)
{
    Frame out{};
    for (int row = 0; row < 8; row++)
    {
        if (x >= 8 || x <= -8)
            out[row] = 0;
        else if (x >= 0)
            out[row] = picture[row] >> x;
        else
            out[row] = static_cast<uint8_t>(picture[row] << -x);
    }
    return out;
}

// A picture moved down by some rows, padded with blank rows at the top.
constexpr Frame lower(const Frame &picture, int rows)
{
    Frame out{};
    for (int row = 0; row < 8; row++)
        out[row] = (row - rows >= 0 && row - rows < 8) ? picture[row - rows] : 0;
    return out;
}

// Two pictures drawn on top of each other.
constexpr Frame overlay(const Frame &a, const Frame &b)
{
    Frame out{};
    for (int row = 0; row < 8; row++)
        out[row] = a[row] | b[row];
    return out;
}
