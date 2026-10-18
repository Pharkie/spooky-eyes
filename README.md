<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/title-dark.png">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/title-light.png">
    <img src="docs/assets/title-light.png" alt="The All Hallows Spooky Pumpkin Eyes" width="70%">
  </picture>
</p>

<p align="center">
  <img src="docs/assets/eyes-demo.gif" alt="The eyes blinking and winking, a creeper exploding, a witch flying past, Pac-Man, and the eyes rolling" width="412">
</p>

**Two glowing eyes. One pumpkin. A phone full of buttons.**

Spooky Pumpkin Eyes turns a Halloween pumpkin into a character: two 8×8 LED eyes and a colour-changing
mouth, run by a tiny ESP32-C3 board with WiFi. Switch it on and it plays its own show. Open its web page on
any phone, tablet or laptop and the show pauses for you: about 50 buttons, a talking pumpkin, and a box for
scrolling your own words across the eyes.

### Reasons to love it

- **Works straight out of the box.** Switch on and the show starts: blinks, winks, bats, ghosts, a witch on a
  broomstick, Pac-Man, a Nether portal, round and round.
- **A button for everything.** About 50 effects on one page, from eyes and pictures to creepers, endermen
  and games.
  Each press interrupts the last, so mash away.
- **A talking pumpkin.** Your phone speaks each effect's line as it plays ("Hissss!"). Pick any voice your
  phone has.
- **Your own words.** Type anything and it scrolls across the eyes.
- **Design your own show.** Add, remove and reorder blocks on the show page. It saves as you go, and that's
  what plays at switch-on.
- **No app, no cloud, no account.** Plain web pages, served by the pumpkin itself at `http://pumpkin.local`.
  Your WiFi password is stored on the board and never sent anywhere.
- **Doorstep-ready.** Carried out of WiFi range, it makes its own WiFi so trick-or-treaters' phones can
  still press the buttons.
- **Runs on 3 AA batteries.** The processor idles, the mouth LED is dimmed, and the pumpkin's own WiFi
  switches off when nobody's using it.
- **One printed page to set it up.** [The guide](docs/instructions/2026-wifi-upgrade.pdf): wiring diagram,
  WiFi setup and troubleshooting on one sheet of A4.

<p align="center">
  <img src="docs/assets/pages.jpg" alt="The setup page, the buttons page and the show page on a phone" width="100%">
</p>

## The effects

About 50 buttons in five groups: pictures (pumpkins, a witch, bats, ghosts, Pac-Man, Space Invaders…),
That Blocky Game (a creeper, an enderman, mining…), the eyes themselves (blinks, winks, rolls, angry,
sleepy…), games (dice, magic, a surprise) and words. The talking pumpkin has a line for most of them.
**[See every effect, animated](docs/effects.md)**.

## Quick start

Got a pumpkin in the post? It's already flashed and wired: skip to **Switch on**, or just follow
**👉 [the one-page guide (PDF)](docs/instructions/2026-wifi-upgrade.pdf)** that came with it.

### What you need

- An **ESP32-C3 SuperMini** board
- Two **MAX7219 8×8 LED matrices**, daisy-chained ([these, Amazon UK](https://amzn.eu/d/e5Kw5vX))
- A **common-anode RGB LED** for the mouth ([these](https://amzn.eu/d/9fQ5G3V))
- A **3 × AA battery box** (about 4.5 V into the 5V pin), or a USB power bank in the USB-C socket
- Wires: the jumper leads that come with the matrices do nicely

### 1. Flash the firmware

No toolchain needed: a ready-made firmware file and a web browser.

1. Download `spooky-pumpkin-eyes-<version>-full.bin` from the
   [latest release](https://github.com/Pharkie/spooky-eyes/releases/latest).
2. Plug the ESP32-C3 SuperMini into your computer with a USB-C **data** cable.
3. In Chrome or Edge, open Espressif's [web flasher](https://espressif.github.io/esptool-js/): **Connect**
   and pick the board's port, **Erase Flash**, then add the file at address `0x0` and **Program**.
   Prefer a terminal? `pip install esptool`, then
   `esptool --chip esp32c3 write-flash -e 0x0 spooky-pumpkin-eyes-<version>-full.bin`.
   If it can't connect, hold the board's BOOT button while plugging the cable in.
4. Unplug it. Done: it's a pumpkin brain now.

### 2. Wire it

<p align="center">
  <img src="docs/assets/wiring.png" alt="Wiring diagram: the eye from behind, the mouth LED and the battery pack, to the ESP32-C3 from underneath" width="100%">
</p>

The board is drawn from **underneath**, where its pin names are printed; the eye from **behind**.

- **Power:** battery pack **+** → `5V`, **−** → `G`. The first eye's `VCC` and `GND` join the same two.
- **The first eye** (its *input* header): `DIN` → `5`, `CS` → `6`, `CLK` → `7`.
- **The second eye:** its input header to the first eye's **output** header, pin for pin: `VCC`→`VCC`,
  `GND`→`GND`, `DOUT`→`DIN`, `CS`→`CS`, `CLK`→`CLK`. Five short wires, or the little pin headers that come
  with the modules.
- The eye wired to the board is the **right eye as you look at the pumpkin**; the chained one is the left.
- **Mouth LED:** the long leg (+) → `3.3`; red → `4`, green → `3`, blue → `1`. Mixed up the short legs? The
  mouth just shows other colours.

The pin map lives in [src/hardware/board.h](src/hardware/board.h).

New to these LED modules? [GurgleApps' original tutorial](https://gurgleapps.com/learn/projects/8x8-led-matrix-halloween-jack-o-lantern-pumpkin-project-with-a-pico)
shows the matrices and the eye-to-eye chain step by step (with a Raspberry Pi Pico, so its pins differ).

### 3. Switch on

The eyes start the show, and scroll how to join the pumpkin's own WiFi: network `Spooky Pumpkin`,
password `spookyeyes`.

### 4. Connect it to your WiFi (optional)

Join that WiFi on a phone. A setup page pops up (or open `http://pumpkin.local`): name your pumpkin, pick
your WiFi, type its password, tap **Connect**. The pumpkin checks the password first, restarts, joins your
WiFi, and opens the buttons by itself. From then on, `http://pumpkin.local` on any phone, tablet or laptop
on your WiFi.

WiFi is optional: save just a name, or tap "just use the buttons", and the pumpkin keeps its own WiFi.

<details>
<summary><b>Good to know</b> (what the eyes mean, the BOOT button, batteries)</summary>

- **"Join WiFi…" again after setup?** The password was probably wrong: join the pumpkin's WiFi again and
  the setup page says what went wrong.
- **On the doorstep.** Out of reach of your WiFi for 3 minutes, the pumpkin makes its own network again, so
  you can still join it and open `http://pumpkin.local` or `http://192.168.4.1`. Back in range it rejoins
  your WiFi, unless its own WiFi has been unused for 10 minutes and switched off by then: then switch it
  off and on.
- **The BOOT button** on the board: a quick press scrolls the address again (and unlocks firmware updates
  for 5 minutes, which only work on your WiFi). Hold it for 5 seconds to forget your WiFi. That erases the
  WiFi name and password from the board's memory, so do it before passing the pumpkin on.
- **"New batteries please"** means it: weak batteries can't power the WiFi.
- **Saving batteries.** The pumpkin's own WiFi (before it's set up, or on the doorstep) switches off when
  nobody has used it for 10 minutes; the show carries on. On your WiFi it stays connected, dozing between
  messages, so the page always works. A quick BOOT press, or switching off and on, brings its own WiFi
  back. You can turn this off in Settings ("Save batteries").

</details>

## Under the hood

Curious, or want to change something? [Building and hacking](docs/hacking.md) covers how it works,
building and flashing from source, the serial commands, adding an effect, the tests, making a release
and how the code is organised. [CLAUDE.md](CLAUDE.md) is the map of the code for Claude Code sessions.

## Like this project?

If it made a pumpkin grin, give it a ⭐ so other Halloween tinkerers can find it.

## LLM generated, human reviewed

This code was written with [Claude Code](https://claude.com/claude-code) (Anthropic), mostly on Claude Opus
5.5 and Fable 5.1, directed and reviewed throughout by the human author. The firmware was tested on real
boards and phones, with automated tests for the effects and the web API, and reviewed for security and
privacy before the pumpkins were posted. Still, it has had limited manual review: make your own checks, and
use it at your own risk.

## Credits

- Original pumpkin eyes code and tutorial by [GurgleApps](https://gurgleapps.com/learn/projects/8x8-led-matrix-halloween-jack-o-lantern-pumpkin-project-with-a-pico)
  ([gurgleapps/pico-pumpkin](https://github.com/gurgleapps/pico-pumpkin), GPL): the eye pictures, many of
  the shapes, the first eye animations and the scrolling font are theirs. This project began in 2023 as a
  fork of it, called pico-pumpkin while it ran on a Raspberry Pi Pico; it became spooky-eyes in 2026 with
  the move to the ESP32-C3 and a rewrite in C++. The 2023-24 MicroPython version isn't in the current tree,
  but it's still in this repo's git history
  ([legacy/pico-micropython at the last commit before it was removed](https://github.com/Pharkie/spooky-eyes/tree/876fc9d/legacy/pico-micropython))
  and in GurgleApps' repo.
- WiFi setup, captive portal and web server patterns from Adam's Scribe Evolution project.
- Libraries: [ArduinoJson](https://arduinojson.org/) (MIT),
  [ESPAsyncWebServer and AsyncTCP](https://github.com/esphome/ESPAsyncWebServer) (LGPL 3.0), and
  [arduino-esp32](https://github.com/espressif/arduino-esp32) (LGPL 2.1), each under its own licence.
- Eyes: MAX7219 8x8 matrices ([these, Amazon UK](https://amzn.eu/d/e5Kw5vX)); mouth: a common-anode RGB LED
  ([these](https://amzn.eu/d/9fQ5G3V)).
- The "That Blocky Game" effects are a six-year-old's fan art. Minecraft is a trademark of Mojang
  Synergies AB; this project isn't affiliated with or endorsed by Mojang or Microsoft.

## Licence

GNU General Public License, version 3 or (at your option) any later version: see [LICENSE](LICENSE). It
includes GurgleApps' GPL pictures, animations and font, released under the GPL without a version number
(which lets anyone choose any version); this project uses version 3 or later throughout.
