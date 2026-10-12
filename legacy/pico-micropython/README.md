> **Legacy.** This is the 2023-24 MicroPython version for the Raspberry Pi Pico, kept as one pumpkin's Pico still
> runs it. It's frozen: new effects go in the ESP32-C3 firmware at the top of this repo, and the golden tests
> there compare the C++ effects against these files.

# pico-pumpkin

Here's modification of 8x8 led matrix eyes for a halloween pumpkin.

I removed support for i2c and the HT16k33 LED matrix in the process of making the code clearer for myself using
the MAX7219. I used these (Amazon UK) for the eyes: https://amzn.eu/d/e5Kw5vX

I fixed the issue mentioned in the video that the left and right eyes needed to be swapped to display scrolling messages.

I couldn't get the LED matrices to work from the tutorial, so I used the PIN connections shown in:
https://microcontrollerslab.com/max7219-led-dot-matrix-display-raspberry-pi-pico/

I also added a (common anode) RGB LED to light up the mouth, which you could comment out if you don't need.

For the RGB LED, I used one of these: https://amzn.eu/d/9fQ5G3V

Known issues:
I think some of the animations don't work e.g. winkLeft, winkRight.

[Instructions](https://gurgleapps.com/learn/projects/8x8-led-matrix-halloween-jack-o-lantern-pumpkin-project-with-a-pico)

## Choosing the board

Set `BOARD` near the top of `main.py` to `"pico"` (Raspberry Pi Pico, the default) or `"esp32c3"`.

| Eye pin | Pico | ESP32-C3 |
|---|---|---|
| DIN | GP3 (pin 5) | GPIO 7 |
| CLK | GP2 (pin 4) | GPIO 6 |
| CS | GP1 (pin 2) | GPIO 5 |
| VCC | VBUS (pin 40, 5V) | 5V |
| GND | any GND | GND |

On the Pico, the RGB mouth LED uses GP14 (red), GP15 (green) and GP16 (blue), with its long leg on 3V3.
`EYE_GAP` sets how many blank columns scrolling text crosses between the eyes (8 by default).

## The button page

`tools/pumpkin-buttons.pl` is a small web page for a Mac (plain Perl, no installs) with a button for every
effect. Plug the pumpkin in by USB, then run:

```
perl tools/pumpkin-buttons.pl
```

and open http://localhost:8765. Run it with `PUMPKIN_PHONE=1` and phones on the same WiFi can use it too (the
address and a QR code appear at the bottom of the page). Pressing a button interrupts whatever is playing.
The "Talking pumpkin" switch makes the Mac speak along with each effect, in any of its voices.

The effects include chomping pumpkins, a witch on a broomstick, a bat, a spider, Pac-Man, Pong, a skull, a cat,
mood eyes, dice, a magic pumpkin, "3, 2, 1... BOO!", a surprise show, and lots of Minecraft: creeper, Enderman,
emerald trade, panda, mining, digging, chopping a tree, iron golem, redstone lamp and TNT, ghast, skeleton,
slime, day and night, and a Nether portal.

`tools/pico.pl` copies files to and from the board without Python:

```
perl tools/pico.pl ls
perl tools/pico.pl put main.py main.py
perl tools/pico.pl run
```

## What's next

`ESP32-C3-PLAN.md` is the plan for running the button page on an ESP32-C3, so a phone can control the
pumpkin with no laptop. `CLAUDE.md` has notes on how the code fits together.

Pictures, effects and ideas by a six-year-old Halloween fan.
