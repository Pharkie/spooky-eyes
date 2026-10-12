#pragma once
// The eyes: two daisy-chained MAX7219 8x8 LED matrices on SPI (pins in board.h).
// A port of legacy/pico-micropython/max7219_matrix.py with the same bytes on the wire.
// Call only from the Arduino loop task (the player and the effects run there).

#include <cstdint>
#include "display/frame.h"

namespace max7219_eyes
{
    // Start SPI and set up both chips (eyes blank until the first show()).
    void begin();

    // Send the set-up commands again: shutdown 0, display test 0, scan limit 7, decode mode 0, then the
    // last brightness and shutdown 1. Cheap insurance against glitches on long wires; the pictures stay.
    void reinit();

    // Show a picture on each eye: left and right as you look at the pumpkin
    void show(const Frame &left, const Frame &right);

    // Raw MAX7219 brightness 0-15 (higher values are capped at 15)
    void setBrightness(uint8_t level);
}
