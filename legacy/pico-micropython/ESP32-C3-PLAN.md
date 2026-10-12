> **Done, differently.** This plan (10 Oct 2026) was carried out as a C++ port rather than in MicroPython,
> reusing the WiFi code from Scribe Evolution. See the README and CLAUDE.md at the top of the repo.

# Plan: move the pumpkin to an ESP32-C3 so a phone can control it with no laptop

Written 10 Oct 2026 for Adam to pick up at home. The Pico version works today; this is the next step.

## The idea

The ESP32-C3 has WiFi built in. Instead of the laptop running the button page, the pumpkin runs it itself:

- The C3 makes its **own WiFi network** (an "access point"), e.g. `Jacks-Pumpkin`. No home WiFi password needed,
  and it works anywhere, even out on the doorstep on Halloween night.
- A phone joins that network and opens **http://192.168.4.1** to get the same button page.
- The **phone does the talking**, using the browser's built-in speech (`speechSynthesis`). Zarvox is a Mac voice,
  so the phone will offer its own voices instead.
- Optional later: join the home WiFi instead (station mode), with the WiFi name and password typed into a
  `wifi_config.py` file by you, never by Claude.

## Step by step

1. **Check the C3 has MicroPython.** Plug it in by USB and run:
   `perl tools/pico.pl exec "import os" "print(os.uname())"`
   (If the Pico is plugged in too, choose the port: `PORT=/dev/cu.usbmodemXXXX perl tools/pico.pl ...`.)
   If there's no MicroPython, flash the `ESP32_GENERIC_C3` firmware from micropython.org, using `esptool`
   (needs Python) or Espressif's browser flasher in Chrome.
2. **Back up whatever is on the C3** first: `perl tools/pico.pl ls`, then `get` each file.
3. **Copy the pumpkin files to the C3:** `main.py`, `matrix_fonts.py`, `max7219_matrix.py`, `eyes_ani.json`, `boot.py`.
   In `main.py` set `BOARD = "esp32c3"`.
4. **Wire the eyes to the C3** (unplug USB first):

   | Eye wire | Eye pin | ESP32-C3 pin |
   |---|---|---|
   | Blue | DIN | GPIO 7 |
   | White | CLK | GPIO 6 |
   | Grey | CS | GPIO 5 |
   | Power | VCC | 5V |
   | Ground | GND | GND |

   The RGB mouth LED is optional: GPIO 1 (red), 2 (green), 3 (blue), long leg to 3V3, then set
   `RGB_LED_CONNECTED = True` in the `esp32c3` section. Check your C3 board's pin labels match.
5. **Check the normal show runs.** If you get an SPI error, change `SPI_BUS` in the `esp32c3` section
   (try `1`). The laptop button page (`tools/pumpkin-buttons.pl`) should then work with the C3 too.
6. **Build the WiFi button page on the board.** This is the main new code:
   - `wait(seconds)`: replaces `time.sleep` everywhere in `main.py`. When the WiFi page is running, it keeps
     answering the phone while it waits, and a new button press raises a `ButtonPressed` exception that
     stops the current effect straight away (the same "new press interrupts" behaviour as the laptop page).
   - `say(text)`: replaces the `print('SAY:...')` lines. It still prints (so the laptop page keeps talking)
     and also queues the text for the phone to speak.
   - An `EFFECTS` table on the board (button name → function), moved out of `pumpkin-buttons.pl` so both
     share it. The laptop page then just sends `m.play('witch')` etc.
   - A new `pumpkin_web.py`: starts the access point (`network.WLAN(network.AP_IF)`), serves `buttons.html`
     (a copy of the laptop page that speaks with `speechSynthesis`, with each button's phrase in a
     `data-say` attribute), and handles `POST /play/<name>`, `POST /say`, `POST /resume` and
     `GET /said?after=N` (new SAY lines for the phone to speak).
   - The main loop wraps the show in `try/except ButtonPressed`. Button mode plays whatever is pressed and goes
     back to the normal show after "Back to the normal show" or 2 quiet minutes.
   - Watch memory: the C3 has less spare RAM than the Pico. If importing `main.py` runs out of memory,
     move the button-only effects into their own module.
7. **Try it:** power the pumpkin from a USB power bank, join `Jacks-Pumpkin` on a phone, open http://192.168.4.1.

## Asking Claude to do it

Open the repo folder in Claude Code and say:

> Read CLAUDE.md and ESP32-C3-PLAN.md, then help me with step 1 of the ESP32-C3 plan. The C3 is plugged in by USB.

## Also worth knowing

- The **Pimoroni Pico LiPo** has no WiFi either, but it can run from a LiPo battery and charge it over USB. It has
  the same GP pin numbers as the Pico, so it's a drop-in swap with `BOARD = "pico"` (check its pin labels).
- The **Pico W / Pico 2 W** would also work for WiFi, with the Pico pins, and is an alternative to the C3.
