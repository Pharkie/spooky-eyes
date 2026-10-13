# Spooky Pumpkin Eyes: notes for Claude

Halloween pumpkin: two daisy-chained MAX7219 8x8 LED matrices ("eyes") and a common-anode RGB "mouth" LED on
an ESP32-C3 SuperMini, C++/Arduino via PlatformIO. Phones control it over WiFi. A six-year-old designed most
of the pictures and effects; four pumpkins were posted to friends and family in October 2026.

## Golden rules

- **Only flash a board or read its serial port when Adam asks** (he plugs one in and says so). Then flash
  the full image with an erase (`esptool --chip esp32c3 --port /dev/cu.usbmodem… write-flash -e 0x0
  release/…-full.bin`, outside the sandbox), and read the log by opening the port with DTR and RTS low and
  pulsing RTS to reset it (pyserial is in PlatformIO's Python, /Library/Frameworks/Python.framework/…/python3).
- Pins live only in `src/hardware/board.h`, and the wiring guide (`docs/instructions/`) must match it.
- Effects code (`src/display/`, `src/effects/`) is portable: no Arduino headers, so it runs in native tests.
- The effects table in `src/effects/registry.cpp` is the single source of truth for buttons, phrases and the
  surprise show. The web page builds its buttons from it via `GET /api/effects`.
- Web handlers never touch the eyes or the WiFi driver. They call `player::`, `settings::` and `network::`
  (including `network::scan()`), which are thread-safe.
- `settings::save()` is the only validator; limits live in `src/core/constants.h` (the pages and the mock
  server read them from the API or the header), text rules (plain ASCII, WiFi passwords) in
  `src/core/text.h`. Don't re-implement them elsewhere.
- Every restart goes through `firmware::restart()`. A firmware update is only kept after running 30 s
  without crashing; otherwise the bootloader rolls back.
- Every POST from the pages carries the `X-Pumpkin: 1` header (cheap CSRF protection). Risky actions
  (firmware update; WiFi changes while strangers could be on the setup WiFi) also need a short BOOT press in
  the last 5 minutes (`boot_button::unlocked()`).
- The repo is public: no real people's names in it. Credits with names go in `local/credits.h` (gitignored,
  in the main checkout where releases are built), e.g. `#define MINECRAFT_BY "..."` for the Minecraft heading
  (`groupTitle()` in `src/effects/registry.cpp`).

## Commands

```
pio run -e supermini            # build firmware (also regenerates src/generated/web_assets.h from web/)
pio test -e native              # golden-frame tests: C++ effects vs the MicroPython originals
python3 tools/mock_server.py    # web pages with a fake API, no board needed
python3 scripts/build_release.py  # release/ images: full (new board, 0x0) and ota (Settings page upload)
tools/make_instructions.sh      # one-page A4 wiring + setup PDF
```

If several builds run at once, give each its own `PLATFORMIO_BUILD_DIR`.

## How it fits together

- `main.cpp`: settings -> saved show (LittleFS) -> player (eyes on) -> network (starts its task and returns:
  home WiFi, or the setup WiFi + captive portal, joins in the background) -> web server -> loop runs the
  player, so the show starts at once. The network task tells main.cpp the outcome through onModeChange.
- **Two modes** (`src/player/`): Show plays the saved show (`show_store`, LittleFS on the "spiffs"
  partition; default = `defaultShow()` in `src/effects/show.cpp`) round and round. Buttons pauses it for
  presses: the button page asks for it on opening and sends a heartbeat every 30 s while on screen; 2
  minutes after the last press or heartbeat it's back to the show. The show creator page (`web/show.html`)
  edits the blocks (effects and words) and saves as you go; a save takes effect from the next block. The capture script and the mock server read show.cpp's table.
- **Threads.** Effects run on the Arduino loop task. The async web server has its own task, and so does
  the network housekeeping (captive DNS, reconnects, status LED, BOOT button). A web request sets a pending
  request in the player; `Engine::wait()` notices and throws `EffectInterrupted`, which unwinds the effect
  at once. That's how a new button press interrupts whatever is playing.
- **Engine** (`src/effects/engine.h`): everything an effect may do (show, brightness, wait, say, random,
  mouth colour, settings). `DeviceEngine` implements it on the board; `test/recording_engine.h` records
  events for the tests.
- **Pictures** are 8 row bytes, top row first, most significant bit = leftmost dot. `Engine::show(left,
  right)` uses the viewer's left and right.
- Things scroll in from the right eye into the left. `eyeGap` blank columns sit between the eyes; `slide()`
  and friends use a virtual strip where the right eye starts at column `8 + eyeGap`.
- **Talking pumpkin:** effects call `engine.say()`; the web server forwards it to phones as a server-sent
  event and the phone speaks it with `speechSynthesis`. Button presses say the effect's phrase as it starts; the show is silent (the player mutes the engine while a show block plays).
- **What the eyes say about WiFi** (`main.cpp`): in setup mode, "Join WiFi: <name>  Password: spookyeyes
  then open 192.168.4.1" once and again at the start of the next show pass, then the show plays on by
  itself (WiFi is optional); on home WiFi, "http://pumpkin.local
  or <IP>" once. A short BOOT press repeats it. After a brownout restart it skips WiFi and asks for new
  batteries.
- **WiFi** is ported from Scribe Evolution (`~/dev/Scribe Evolution/VS Code Workspace/Git repo/scribe`,
  `src/core/network.cpp`): pre-scan, connect, AP+STA fallback with captive DNS, mDNS, reconnects, status LED.
  Unlike Scribe, the setup WiFi ("Spooky Pumpkin") and web address (`pumpkin.local`) are fixed, not made from
  the name (`kSetupApName`, `kHostname`): one pumpkin per house, and the guide prints them. SuperMini boards need reduced
  WiFi transmit power to connect reliably. Our own captive-portal DNS (`captive_dns.cpp`) replaces
  Arduino's, which ignores queries with EDNS records. Losing home WiFi for 3 minutes (carried out to the
  doorstep) starts the setup WiFi; it rejoins home WiFi when it's back in range. Before saving, the setup
  page has the network task try the WiFi on the station side (`network::startWifiTest()`, `/api/wifi/test`)
  and only saves one that joined; skipped on home WiFi, where a check would drop the connection.
- **Saving batteries:** the CPU runs at 80 MHz (`kCpuMhz`), the mouth LED is dimmed with LEDC PWM
  (`kMouthDutyPercent`), and with the `wifiSaver` setting on (the default) the network task switches the
  WiFi off (`network::Mode::Off`, status LED off; the show carries on without a word) once nobody has used
  the pumpkin for `kWifiIdleOffMs`.
  Using it means a web request or an open event stream (the web server calls `network::noteActivity()`
  from its first handler and the SSE keep-alive, not per handler) or a phone on the setup WiFi. The
  decision is `wifi_saver::timeToSwitchOff()` (portable, unit-tested). Only a restart brings WiFi back: a
  short BOOT press while it's off does one.
- **Web pages** are plain HTML/CSS/JS in `web/`, gzipped into the firmware at build time by
  `scripts/embed_web.py`. No filesystem upload step, and OTA updates update the pages too.

## Adding an effect

1. Write `void myEffect(Engine &e)` in `src/effects/effects.cpp` (declare it in `effects.h`), using the
   primitives in `primitives.h` (`sequence`, `flip`, `slide`, `scrollMessage`, `sparkles`, `explode`...).
   New pictures go in `src/display/sprites.cpp`.
2. Add one line to the table in `src/effects/registry.cpp`: id, label, emoji, group, phrase (or
   `nullptr`), function, and whether the surprise show may pick it.

That's it: the button appears on the page, the surprise show can pick it, and `pio test -e native`
smoke-tests it at extreme settings (effects from the old Pico are also compared frame by frame against
the MicroPython originals). Leave brightness at
`maxBright()` when the effect ends (`explode()` does). Avoid long full-brightness, all-on frames (battery
current).

## Legacy

`legacy/pico-micropython/` is the 2023-24 MicroPython version for the Raspberry Pi Pico, plus the Mac
tools that drove it over USB. Frozen: the original Pico pumpkin still runs it, but new work goes in the C++ firmware.
The golden tests read it, so don't change it.
