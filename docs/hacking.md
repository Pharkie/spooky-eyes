# Building and hacking

For the curious and the tinkerers: how the pumpkin works inside, building and flashing it from source, the
tests, and making a release. Just want a pumpkin? The [README](../README.md) has the quick start.

## How it works

- **C++ firmware** on the ESP32-C3 (Arduino framework, built with PlatformIO). The effects are portable
  code with no Arduino headers, so they run in tests on your computer too.
- **The web pages are compiled into the firmware**, gzipped, so there's no filesystem to upload and a
  firmware update updates the pages too.
- **Its own WiFi with a captive portal** until it's set up: join "Spooky Pumpkin" and the setup page pops up.
  On your WiFi it answers at `pumpkin.local`.
- **A talking pumpkin** by server-sent events: the pumpkin tells the page what to say, and the phone says
  it with its own speech voices.
- **Updates from the Settings page** over WiFi, with a 30-second trial: firmware that crashes is rolled
  back by the bootloader.
- **Battery care:** 80 MHz CPU, a PWM-dimmed mouth LED, and the pumpkin's own WiFi off when idle.

## Building from source

You need [PlatformIO](https://platformio.org/) (`pip install platformio`).

```
pio run -e supermini              # build
pio run -e supermini -t upload    # flash over USB
pio device monitor                # serial log (115200)
pio test -e native                # effect tests on this computer, no board needed
python3 tools/mock_server.py      # try the web pages in a browser with a fake pumpkin
```

Over USB the board also takes simple serial commands: `list`, `play creeper`, `say Hello`, `show`,
`name Jack's Pumpkin`, `status`, `unlock` (a short BOOT press), `fs`, `help`.

**Adding an effect** is a function in `src/effects/effects.cpp` and one line in the table in
`src/effects/registry.cpp`: the button appears on the page, the surprise show can pick it, and the tests
smoke-test it. [CLAUDE.md](../CLAUDE.md) explains how the pieces fit together.

### Testing

```
pio test -e native                                   # the effects, frame by frame
python3 tools/api_check.py --mock                    # the web API cases on the mock, in every WiFi mode
python3 tools/api_check.py --board http://pumpkin.local             # the same cases on a real board
python3 tools/api_check.py --board http://pumpkin.local --compare   # board and mock side by side
```

The pages are mostly tried out against the mock server, so the mock must answer exactly as the firmware
does. `api_check` sends both the same 150 or so requests from [test/api/cases.py](../test/api/cases.py),
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
[docs/testing/pre-posting-checklist.md](testing/pre-posting-checklist.md).

### Making a release and preparing pumpkins

```
python3 scripts/build_release.py
```

This writes two images to `release/` (gitignored). A build in a checkout with `local/credits.h` includes
that private credit, so keep those files private. `python3 scripts/build_release.py --public` builds
without it, into `release/public/`, checks the images are clean, and those are the ones to attach to a
GitHub release. Tag the commit you flash (e.g. `git tag 2026.10.2`), and bump `FIRMWARE_VERSION` for every
image handed out, so a pumpkin's Settings page can show that an update worked.

- `spooky-pumpkin-eyes-<version>-full.bin`: for a new board, flashed at address 0x0.
- `spooky-pumpkin-eyes-<version>-ota.bin`: to update a pumpkin that's already running, from its Settings
  page (no cable needed; it must be on the home WiFi, and you'll be asked to press the BOOT button on the
  board first).

Every board gets the same firmware and leaves identical: it's called "Spooky Pumpkin" until its new owner
names it on the setup page (or later in Settings). Nothing to track per person.

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
3. Print the guide, [docs/instructions/2026-wifi-upgrade.pdf](instructions/2026-wifi-upgrade.pdf),
   once per pumpkin (`tools/make_instructions.sh` rebuilds it from the HTML next to it).

### How the code is organised

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

## Pins

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

The pin map lives in [src/hardware/board.h](../src/hardware/board.h). WiFi draws more than the old Raspberry Pi
Pico this project started on, so fresh batteries help.
