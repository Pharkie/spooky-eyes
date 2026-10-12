> **Historical.** Notes written for Claude on the family's Mac (Documents folder layout, Perl tools, no Python
> or git there). The paths below refer to that Mac, not this repo.

# Pumpkin eyes: notes for Claude

Halloween pumpkin: two daisy-chained MAX7219 8x8 LED matrices ("eyes") plus a common-anode RGB "mouth" LED,
on a Raspberry Pi Pico (USB-C clone, RP2040, MicroPython 1.23, no WiFi). Upstream repo:
https://github.com/Pharkie/pico-pumpkin (Adam's). A six-year-old comes up with the ideas, with a parent helping.
Read HANDOVER.md for the human side, and ESP32-C3-PLAN.md for the planned WiFi version.

## Working with the young designer

- Keep replies short and friendly, and say the fun bits out loud with the Mac's `say` command (it only makes
  sound when run outside the sandbox). The button page's voice is Zarvox.
- Show pictures before uploading when it helps (an 8x8 grid preview), and let them pick between options.

## Helping a parent in a chat (no access to the board)

The parent may be on a free Claude chat, attaching files from this folder. Give them **complete files** to save
(never diffs or commands), say exactly which folder each one goes in, and keep `pumpkin-buttons.pl` in step
with `main.py` when adding buttons (see below). She then double-clicks `Update Pumpkin.command`: it saves the
board's files to `saved-from-pumpkin/<date>/`, uploads `pumpkin-board/matrix_fonts.py`, `eyes_ani.json`
and `main.py` (as `main_new.py`), test-imports it, and only then swaps it in. If the import fails, it puts
the old files back and shows the traceback for her to paste to you. Memory is plentiful on the Pico
(about 145 KB free after importing `main.py`).

## Layout

In this Documents folder:
- `pumpkin-board/`: exactly what's on the board (`main.py`, `matrix_fonts.py`, `eyes_ani.json`,
  `max7219_matrix.py`, `boot.py`, `test_led.py`). Keep it in sync with the board.
- `board-backup/`: the board as it was before 10 Oct 2026 (including its `archive/` folder). Don't change it.
- `pumpkin-buttons.pl`: the laptop button page (web server on :8765). `PUMPKIN_PHONE=1` also listens on the
  LAN so phones can use it; `Start Pumpkin Buttons.command` sets that.
- `pico.pl`: copies files to and from the board over USB serial (MicroPython raw REPL).
- `github-pr/`: upload-ready copy for the GitHub repo (there, board files are at the top level and the
  two Perl tools are in `tools/`).

## Talking to the board

This Mac has no Python and no git (no Xcode command line tools), so everything uses the stock `/usr/bin/perl`:

```
perl pico.pl ls | get REMOTE LOCAL | put LOCAL REMOTE | exec 'code' ... | run
```

`exec` joins its arguments with newlines; set `TIMEOUT=200` for long tests (the default is 60 seconds).
`run` soft-resets the board, so `main.py` starts the normal show. Only one program can hold the serial
port, so **stop the button page before using `pico.pl`**. Set `PORT=/dev/cu.usbmodemXXXX` if two boards are
plugged in.

Safe upload routine: back up first, `put main.py main_new.py`, test it with
`exec "import sys" "sys.modules.pop('main_new', None)" "import main_new as m" "m.some_effect()"`
(also pop `matrix_fonts` if it changed), then rename it over `main.py` and `run`.

## How the code fits together

- Pictures are 8 row-bytes, top row first, most significant bit = leftmost dot.
- `max7219_eyes.show_char(a, b)`: `a` shows on the **right** eye as you look at the pumpkin. Prefer
  `show_eyes(left, right)`, which uses the viewer's left and right.
- Text and sprites scroll in from the right eye into the left. `EYE_GAP` (8) is the number of
  blank columns between the eyes. `slide()`, `fly()` and `place()` use a virtual strip: column 0 is the left
  edge of the left eye, and the right eye starts at column `8 + EYE_GAP`.
- `BOARD = "pico"` or `"esp32c3"` near the top of `main.py` picks the pin set.
- The normal show is the `while True` loop in `main()`. Button-only effects are functions in `main.py`.
- **Adding a button effect touches three places in `pumpkin-buttons.pl`:** `%EFFECTS` (the code to run, e.g.
  `"m.dig()"`), `%PHRASES` (what the talking pumpkin says when it starts), and a `<button data-fx="...">`
  in the HTML after `__DATA__`. The surprise show picks up new effects automatically.
- Board code can make the laptop talk in time with the eyes by printing `SAY:words`.
- A new button press sends Ctrl-C to the board, which interrupts the current effect (KeyboardInterrupt).
  Effects should leave brightness at `MAX_BRIGHT` (the `explode()` helper does).
- Power: avoid long full-brightness, all-on frames (USB current). Flashes above `MAX_BRIGHT + 4` aren't used.
