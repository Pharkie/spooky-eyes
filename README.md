# Spooky Pumpkin Eyes

LED eyes for a Halloween pumpkin: two 8x8 LED matrices and a colour-changing mouth LED, run by an ESP32-C3
SuperMini with WiFi. Out of the box it plays a show of eyes, pictures and messages. Open its web page on a
phone and the show pauses for buttons: one for each of about 50 effects (creepers, a witch on a broomstick,
Pac-Man, a Nether portal...), a talking pumpkin (the phone speaks along), and a box for scrolling your own
words across the eyes. The show page builds your own show from effects and messages (it saves as you go), and
that's what the pumpkin plays at switch-on.

Pictures, effects and ideas by a six-year-old Halloween fan.

## For the people with a pumpkin

The one-page guide is in [docs/instructions/](docs/instructions/): how to move the wires from the old
Raspberry Pi Pico to the new board, and how to set up the WiFi. In short:

1. Switch on. With no home WiFi saved, the pumpkin makes its own WiFi network, named after the pumpkin
   (a new one is called "Spooky Pumpkin"), and its eyes scroll how to join it. The password is `spookyeyes`.
2. Join it on a phone. A setup page pops up (or open http://192.168.4.1): give the pumpkin a name (e.g.
   "Jack's Pumpkin"), pick your home WiFi (it must be 2.4 GHz), type its password, tap Check and save.
   The pumpkin tries the password first (about 10 seconds) and only saves one that works.
3. The pumpkin restarts, joins your WiFi, and scrolls its web address across its eyes, e.g.
   `http://jacks-pumpkin.local  or  192.168.1.23`. Open that on any phone or computer on the same WiFi
   (Android phones often need the numbers).

If the eyes say "Join WiFi..." again after that, the password was probably wrong: join the pumpkin's WiFi
again and the setup page says what went wrong. If it loses home WiFi for 3 minutes (carried out to the
doorstep, say) it makes its own network again, so you can still join it and open http://192.168.4.1; it
rejoins home WiFi when it's back in range.

The BOOT button on the board: a quick press scrolls the address again (and unlocks firmware updates for 5
minutes); hold it for 5 seconds to make it forget the home WiFi. If the eyes say "New batteries please",
they mean it: weak batteries can't power the WiFi.

## Wiring (ESP32-C3 SuperMini)

| Wire | SuperMini pin |
|---|---|
| + power (battery pack + and eyes VCC) | 5V |
| − ground (battery pack − and eyes GND) | G |
| Eyes DIN | 7 |
| Eyes CLK | 6 |
| Eyes CS | 5 |
| Mouth LED long leg (+, common anode) | 3.3 |
| Mouth LED red | 4 |
| Mouth LED green | 3 |
| Mouth LED blue | 1 |

The pin map lives in [src/hardware/board.h](src/hardware/board.h). Power is 3 AA batteries (about 4.5 V)
into the 5V pin, or a USB power bank in the USB-C socket. WiFi draws more than the old Pico, so fresh
batteries help.

## Building

You need [PlatformIO](https://platformio.org/) (`pip install platformio`).

```
pio run -e supermini              # build
pio run -e supermini -t upload    # flash over USB
pio device monitor                # serial log (115200)
pio test -e native                # effect tests on this computer, no board needed
python3 tools/mock_server.py      # try the web pages in a browser with a fake pumpkin
```

The web pages in `web/` are compiled into the firmware, so there's no separate filesystem upload, and a
firmware update updates the pages too.

Over USB the board also takes simple serial commands: `list`, `play creeper`, `say Hello`, `show`,
`name Jack's Pumpkin`, `help`.

### Making a release

```
python3 scripts/build_release.py
```

This writes two images to `release/` (attach them to a GitHub release rather than committing them):

- `spooky-pumpkin-eyes-<version>-full.bin`: for a new board, flashed at address 0x0.
- `spooky-pumpkin-eyes-<version>-ota.bin`: to update a pumpkin that's already running, from its Settings
  page (no cable needed; you'll be asked to press the BOOT button on the board first).

### Preparing pumpkins for posting

Every board gets the same firmware and leaves identical: it's called "Spooky Pumpkin" until its new
owner names it on the setup page (or later in Settings). Nothing to track per person.

1. Erase and flash each board over USB. Erasing matters: it clears any test WiFi details and old settings.
   ```
   esptool --chip esp32c3 write-flash -e 0x0 release/spooky-pumpkin-eyes-<version>-full.bin
   ```
   (esptool v4: `esptool.py --chip esp32c3 write_flash -e 0x0 ...`; nothing installed:
   `pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32c3 write_flash -e 0x0 ...`; or Chrome and
   Espressif's [web flasher](https://espressif.github.io/esptool-js/): Erase Flash, then program at 0x0.)
   If esptool can't connect, hold BOOT while plugging in the USB cable, then press RST after flashing.
2. Switch it on with the eyes attached and check they scroll "Join WiFi: Spooky Pumpkin ...".
3. Print the guide, [docs/instructions/2026-wifi-upgrade.pdf](docs/instructions/2026-wifi-upgrade.pdf),
   once per pumpkin.

(If you'd rather post a named one, `name Jack's Pumpkin` over USB serial or the Settings page does it,
and `tools/make_instructions.sh "Jack's Pumpkin"` makes a matching guide. Not needed.)

## How the code is organised

| Folder | What's there |
|---|---|
| `src/effects/` | The effects, the building blocks they use, and `registry.cpp`, the one table of buttons |
| `src/display/` | Frames, fonts and all the pictures |
| `src/player/` | Show mode (plays the saved show) and buttons mode (plays presses, each interrupting the last) |
| `src/hardware/` | Pin map and drivers: MAX7219 eyes, mouth LED, status LED, BOOT button |
| `src/core/` | Settings (saved in flash), WiFi and captive portal (from Scribe Evolution), constants |
| `src/web/` | Web server and JSON API |
| `web/` | The pages: buttons, setup, settings |
| `test/` | Golden tests: the C++ effects frame-for-frame against the MicroPython originals |
| `docs/instructions/` | The printed one-page guide for the 2026 WiFi upgrade |
| `legacy/pico-micropython/` | The 2023-24 MicroPython version for the Raspberry Pi Pico, and its Mac tools |

[CLAUDE.md](CLAUDE.md) explains how the pieces fit together, including how to add an effect (write the
function, add one line to the table).

## Credits

- Original pumpkin eyes code and tutorial by [GurgleApps](https://gurgleapps.com/learn/projects/8x8-led-matrix-halloween-jack-o-lantern-pumpkin-project-with-a-pico).
- WiFi setup, captive portal and web server patterns from Adam's Scribe Evolution project.
- Eyes: MAX7219 8x8 matrices ([these, Amazon UK](https://amzn.eu/d/e5Kw5vX)); mouth: a common-anode RGB LED
  ([these](https://amzn.eu/d/9fQ5G3V)).

Licence: GNU General Public License (GPL), as the original.
