#pragma once
// Pin map for the ESP32-C3 SuperMini. The wiring guide (docs/instructions/) must match these.
//
// SuperMini pins seen from underneath (the side the wires plug into, where the pin names are printed),
// USB socket at the top:
//   left side:  5V  G  3.3  4  3  2  1  0
//   right side:  5  6  7  8  9  10  20  21
//
// Avoided: GPIO 2 (boot strapping pin), 8 (onboard blue LED), 9 (BOOT button), 18/19 (USB).

#include <cstdint>

// Eyes: two daisy-chained MAX7219 8x8 matrices on SPI (three neighbouring pins on the right side, in the
// same order as the eye's header: DIN, CS, CLK from top to bottom)
constexpr int kEyesDinPin = 5; // DIN (MOSI)
constexpr int kEyesCsPin = 6;  // CS / LOAD
constexpr int kEyesClkPin = 7; // CLK
constexpr uint32_t kEyesSpiHz = 1000000; // slow and steady: long wires inside a pumpkin

// Mouth: common-anode RGB LED, long leg to 3.3. A pin driven LOW lights that colour.
constexpr int kMouthRedPin = 4;
constexpr int kMouthGreenPin = 3;
constexpr int kMouthBluePin = 1;

// Onboard parts
constexpr int kStatusLedPin = 8;       // blue LED, lit when LOW
constexpr bool kStatusLedActiveLow = true;
constexpr int kBootButtonPin = 9;      // BOOT button, reads LOW when pressed
