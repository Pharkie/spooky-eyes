# Spooky Pumpkin Eyes

**[Colour guide (PDF)](docs/instructions/2026-wifi-upgrade.pdf)**: wiring and setup on one page.

LED eyes for a Halloween pumpkin: two 8x8 LED matrices and a colour-changing mouth LED, run by an ESP32-C3
SuperMini with WiFi. Out of the box it plays a show of eyes, pictures and messages. Open its web page on a
phone, tablet or laptop and the show pauses for buttons: one for each of about 50 effects (creepers, a witch on a broomstick,
Pac-Man, a Nether portal...), a talking pumpkin (the phone or laptop speaks along), and a box for scrolling your own
words across the eyes. The show page builds your own show from effects and messages (it saves as you go), and
that's what the pumpkin plays at switch-on.

Pictures, effects and ideas by a six-year-old Halloween fan.

## For the people with a pumpkin

The one-page guide is in [docs/instructions/](docs/instructions/): how to connect the wires from the old
Raspberry Pi Pico to the new board, and how to set up the WiFi. In short:

1. Switch on. With no home WiFi saved, the pumpkin makes its own WiFi network called "Spooky Pumpkin",
   and its eyes scroll how to join it. The password is `spookyeyes`.
2. Join it on a phone, tablet or laptop. A setup page pops up (or open http://pumpkin.local or http://192.168.4.1): give the pumpkin a name (e.g.
   "Jack's Pumpkin"), pick your home WiFi (it must be 2.4 GHz), type its password, tap Connect.
   The pumpkin tries the password first (up to 30 seconds) and only saves one that works. WiFi is
   optional: save just a name, or tap "just use the buttons" at the bottom, and the pumpkin keeps its own
   WiFi.
3. The pumpkin restarts, joins your WiFi, and scrolls its web address across its eyes:
   `http://pumpkin.local  or  192.168.1.23`. The setup page waits for it and opens the buttons by itself;
   later, open that address on any phone, tablet or laptop on the same WiFi (if `pumpkin.local` doesn't
   open, type the numbers). The WiFi name and address are the same on every pumpkin (one per house), so the
   guide can print them; the name you give it is what it scrolls on its eyes.

If the eyes say "Join WiFi..." again after that, the password was probably wrong: join the pumpkin's WiFi
again and the setup page says what went wrong. If it loses home WiFi for 3 minutes (carried out to the
doorstep, say) it makes its own network again, so you can still join it and open http://pumpkin.local or
http://192.168.4.1. It rejoins home WiFi when it's back in range, unless its own WiFi has been unused for
10 minutes and switched off by then: then switch it off and on.

The BOOT button on the board: a quick press scrolls the address again (and unlocks firmware updates for 5
minutes, which only work on the home WiFi); hold it for 5 seconds to make it forget the home WiFi. That
erases the WiFi name and password from the board's memory, so do it before passing the pumpkin on. If the
eyes say "New batteries please", they mean it: weak batteries can't power the WiFi.

To make the batteries last, the pumpkin's own WiFi (before it's set up, or on the doorstep) switches off when
nobody has used it for 10 minutes. On your home WiFi it stays connected, dozing between messages, so the page
always works. The show carries on as usual. To get its own WiFi back, press BOOT quickly or switch the
pumpkin off and on. You can turn this off in Settings ("Save batteries").

## Wiring (ESP32-C3 SuperMini)

| Wire | SuperMini pin |
|---|---|
| + power (battery pack + and eyes VCC) | 5V |
| − ground (battery pack − and eyes GND) | G |
| Eyes DIN | 5 |
| Eyes CS | 6 |
| Eyes CLK | 7 |
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
`name Jack's Pumpkin`, `status`, `unlock` (a short BOOT press), `fs`, `help`.

### Testing

```
pio test -e native                                   # the effects, frame by frame
python3 tools/api_check.py --mock                    # the web API cases on the mock, in every WiFi mode
python3 tools/api_check.py --board http://pumpkin.local             # the same cases on a real board
python3 tools/api_check.py --board http://pumpkin.local --compare   # board and mock side by side
```

The pages are mostly tried out against the mock server, so the mock must answer exactly as the firmware
does. `api_check` sends both the same 150 or so requests from [test/api/cases.py](test/api/cases.py),
nasty ones included (emoji, accents, curly quotes, NUL, `%"\`, lengths at and one past every limit), and
fails on any wrong status code or JSON shape; a different error message is a warning. On a board it notes
the settings and saved show first and puts them back afterwards, and it never restarts the board or saves
or forgets WiFi unless told to (`--allow-restart`, `--allow-wifi-save`, `--allow-forget`). For a board on
its own setup WiFi, join "Spooky Pumpkin" and use `--board http://192.168.4.1`. Don't press BOOT in the 5
minutes before a board run (`--serial` presses it over USB for the few cases that need it).

With a board on USB (these need pyserial, which PlatformIO's Python has:
`/Library/Frameworks/Python.framework/Versions/3.11/bin/python3`):

```
python3 tools/pumpkin_serial.py status               # WiFi mode, address, player, memory, uptime, version
python3 tools/pumpkin_serial.py unlock               # the same as a short BOOT press
python3 tools/pumpkin_serial.py fs                   # storage used, and whether a show is saved
python3 tools/pumpkin_serial.py monitor --reset      # the log, timestamped, from the start
python3 tools/flash_check.py release/spooky-pumpkin-eyes-<version>-full.bin
```

`flash_check` erases and flashes a board as for posting, then reads its first boot and prints PASS or FAIL
for each line it should log (version, fresh settings, storage, effects, setup WiFi, web server) and for
no crash, brownout or restart. `--log-file boot.log` checks a saved log instead.

What needs real phones and routers is in
[docs/testing/pre-posting-checklist.md](docs/testing/pre-posting-checklist.md).

### Making a release

```
python3 scripts/build_release.py
```

This writes two images to `release/` (gitignored). Keep them private, never on a public GitHub release:
the release build includes `local/credits.h`, whose names aren't for the public repo. Tag the commit you
flash (e.g. `git tag 2026.10.0`), and bump `FIRMWARE_VERSION` for every image handed out, so a pumpkin's
Settings page can show that an update worked.

- `spooky-pumpkin-eyes-<version>-full.bin`: for a new board, flashed at address 0x0.
- `spooky-pumpkin-eyes-<version>-ota.bin`: to update a pumpkin that's already running, from its Settings
  page (no cable needed; it must be on the home WiFi, and you'll be asked to press the BOOT button on the
  board first).

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
   `python3 tools/flash_check.py release/spooky-pumpkin-eyes-<version>-full.bin` does this step and checks
   the board's first boot (see Testing).
2. Switch it on with the eyes attached and check they scroll "Join WiFi: Spooky Pumpkin ...".
3. Print the guide, [docs/instructions/2026-wifi-upgrade.pdf](docs/instructions/2026-wifi-upgrade.pdf),
   once per pumpkin (`tools/make_instructions.sh` rebuilds it from the HTML next to it).

## How the code is organised

| Folder | What's there |
|---|---|
| `src/effects/` | The effects, the building blocks they use, and `registry.cpp`, the one table of buttons |
| `src/display/` | Frames, fonts and all the pictures |
| `src/player/` | Show mode (plays the saved show) and buttons mode (plays presses, each interrupting the last) |
| `src/hardware/` | Pin map and drivers: MAX7219 eyes, mouth LED, status LED, BOOT button |
| `src/core/` | Settings (saved in flash), WiFi and captive portal (from Scribe Evolution), constants |
| `src/web/` | Web server and JSON API |
| `web/` | The pages: buttons, show creator, setup, settings |
| `test/` | Golden tests: the C++ effects frame-for-frame against recordings of the 2023-24 MicroPython version; `test/api/`: the web API cases |
| `docs/instructions/` | The printed one-page guide for the 2026 WiFi upgrade |
| `docs/testing/` | The checks before posting that need phones and routers |

[CLAUDE.md](CLAUDE.md) explains how the pieces fit together, including how to add an effect (write the
function, add one line to the table).

## Credits

- Original pumpkin eyes code and tutorial by [GurgleApps](https://gurgleapps.com/learn/projects/8x8-led-matrix-halloween-jack-o-lantern-pumpkin-project-with-a-pico)
  ([gurgleapps/pico-pumpkin](https://github.com/gurgleapps/pico-pumpkin), GPL): the eye pictures, many of
  the shapes, the first eye animations and the scrolling font are theirs. This project began in 2023 as a fork of it, called
  pico-pumpkin while it ran on a Raspberry Pi Pico; it became spooky-eyes in 2026 with the move to the
  ESP32-C3 and a rewrite in C++. The 2023-24 MicroPython version isn't kept here any more: it lives on in
  the history before then, and in GurgleApps' repo.
- WiFi setup, captive portal and web server patterns from Adam's Scribe Evolution project.
- Libraries: [ArduinoJson](https://arduinojson.org/) (MIT),
  [ESPAsyncWebServer and AsyncTCP](https://github.com/esphome/ESPAsyncWebServer) (LGPL 3.0), and
  [arduino-esp32](https://github.com/espressif/arduino-esp32) (LGPL 2.1), each under its own licence.
- Eyes: MAX7219 8x8 matrices ([these, Amazon UK](https://amzn.eu/d/e5Kw5vX)); mouth: a common-anode RGB LED
  ([these](https://amzn.eu/d/9fQ5G3V)).

## Licence

GNU General Public License, version 3 or (at your option) any later version: see [LICENSE](LICENSE). It
includes GurgleApps' GPL pictures, animations and font, released under the GPL without a version number (which
lets anyone choose any version); this project uses version 3 or later throughout.
