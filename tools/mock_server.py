#!/usr/bin/env python3
"""A pretend pumpkin, for working on the web pages without a board.

    python3 tools/mock_server.py                  http://localhost:8080, on home WiFi
    python3 tools/mock_server.py --mode setup     setup mode, never set up: / goes to /setup
    python3 tools/mock_server.py --mode lost      setup mode, home WiFi saved but out of reach (the doorstep):
                                                  phones' sign-in sheets open the buttons
    python3 tools/mock_server.py --mode wrong     setup mode, home WiFi saved but it turned the pumpkin
                                                  away (wrong password): sign-in sheets open /setup
    python3 tools/mock_server.py --mode off       WiFi switched off to save batteries (nobody used it for
                                                  kWifiIdleOffMs). On the board no page could ask then;
                                                  here /api/status says "off", so the pages' words can be
                                                  checked
    PUMPKIN_MODE=setup python3 tools/mock_server.py   the same, from the environment
    python3 tools/mock_server.py --port 9000
    python3 tools/mock_server.py --port 0         any free port (it prints which: tools/api_check.py uses this)

Flip the mode while it runs by adding ?mode=home, ?mode=setup, ?mode=lost, ?mode=wrong or ?mode=off to any
address, e.g. http://localhost:8080/settings?mode=lost or curl 'http://localhost:8080/api/status?mode=setup'.

The player has two modes, as on the board: show (the saved show, round and round: what it does at
switch-on) and buttons (the button page is open: the show pauses and each press plays one effect). Buttons
mode ends kButtonsIdleTimeoutMs after the last press or heartbeat (MOCK_IDLE_SECONDS=20 to wait less). The
saved show lives in memory and starts as the default show from src/effects/show.cpp.

The setup page's WiFi check (POST then GET /api/wifi/test) takes WIFI_TEST_SECONDS, then: "That password
didn't work" if the password is empty (for a WiFi that needs one) or has "wrong" in it, "unsure" (the router
didn't answer in time: the page offers Save anyway) if it has "slow" in it, "Can't find ..." if the WiFi
isn't in the fake scan list, otherwise it passes. On home WiFi it's skipped, as on the board.
MOCK_WIFI_DROP_SECONDS=8 pretends the phone drops off the setup WiFi for that long when a check starts
(the radio changing channel): API requests get no answer meanwhile.

The BOOT button: firmware updates always need a short BOOT press first, and so do WiFi checks, WiFi and name
changes in lost/wrong mode (strangers may be on the setup WiFi). Press it with ?boot=1 on any address, or
curl -X POST http://localhost:8080/mock/boot. It counts for kBootUnlockMs, as on the board; ?boot=0
lets go of it.

Serves web/ straight from disk (edit a page, reload the browser) and fakes every API endpoint the
firmware has (src/web/*.cpp): buttons from src/effects/registry.cpp, limits, timings and the setup
WiFi password from src/core/constants.h, server-sent events ("say" with each effect's phrase, "status"
as it plays), the show creator's show, a WiFi scan and check, settings, restarts and firmware uploads. Like the
firmware, every POST needs the "X-Pumpkin: 1" header. Python standard library only.

Its replies (status codes, JSON shapes and error messages) should match the firmware's exactly:
tools/api_check.py checks both against the same cases (test/api/cases.py), and --compare runs them on a
board and here side by side. Lengths are counted in UTF-8 bytes, as the firmware does.
"""

import argparse
import json
import os
import queue
import random
import re
import sys
import threading
import time
import unicodedata
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
REGISTRY = ROOT / "src" / "effects" / "registry.cpp"
SHOW_SOURCE = ROOT / "src" / "effects" / "show.cpp"
CONSTANTS = ROOT / "src" / "core" / "constants.h"

MAX_JSON_BODY = 1024          # web_utils.h kMaxJsonBodyBytes
MAX_SHOW_BODY = 16 * 1024     # web_utils.h kMaxShowBodyBytes (POST /api/show)
MAX_EFFECT_ID = 32            # api_handlers.cpp kMaxEffectIdLength (bytes)
UTF8_BYTES_PER_CHAR = 4       # api_handlers.cpp kUtf8BytesPerChar: the words' raw cap is this x kMaxWordsLength
LONGEST_ID_SHOWN = 32         # show.cpp checkBlock() kLongestIdShown: an unknown id is echoed up to this many bytes
MAX_UPLOAD = 2 * 1024 * 1024  # bigger than an app slot
EFFECT_SECONDS = 4
BOOT_SECONDS = 5              # how long a pretend restart keeps the pumpkin away
BOOT_PRESS_MESSAGE = "Press the small BOOT button on the pumpkin's board, then try again"  # web_utils.h
WIFI_TEST_SECONDS = 3         # a pretend WiFi check takes this long (up to about 30 s on the board)
WIFI_TEST_KEEP_SECONDS = 60   # network.cpp kWifiTestKeepMs: a finished check is kept this long
# Pretend the phone drops off the setup WiFi for this long when a check starts (0 = it doesn't)
WIFI_DROP_SECONDS = float(os.environ.get("MOCK_WIFI_DROP_SECONDS") or 0)

CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".ico": "image/x-icon",
}

PROBE_PATHS = {
    "/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
    "/connecttest.txt", "/ncsi.txt", "/canonical.html", "/success.txt", "/redirect",
}

FAKE_NETWORKS = [
    {"ssid": "Haunted House 2.4G", "rssi": -48, "secure": True, "channel": 6},
    {"ssid": "Haunted House 2.4G", "rssi": -71, "secure": True, "channel": 36},
    {"ssid": "BatCave-5G", "rssi": -63, "secure": True, "channel": 1},
    {"ssid": "PumpkinPatch", "rssi": -77, "secure": True, "channel": 11},
    {"ssid": "Garden Café 🌻", "rssi": -82, "secure": False, "channel": 6},
    {"ssid": "BT-Hub-X7Q2", "rssi": -88, "secure": True, "channel": 1},
]


# --- Reading the firmware's source, so the mock can't drift from it ---

def read_constants():
    """constexpr values from constants.h: numbers, PumpkinTime::Seconds/Minutes(n) as ms, strings."""
    try:
        source = CONSTANTS.read_text(encoding="utf-8")
    except OSError as e:
        sys.exit(f"Can't read {CONSTANTS.relative_to(ROOT)} ({e.strerror})")
    values = {}
    for name, value in re.findall(r"constexpr\s+[\w:\s\*]+?\b(k\w+)\s*=\s*([^;]+);", source):
        value = value.strip()
        timed = re.fullmatch(r"PumpkinTime::(Seconds|Minutes)\((\d+)\)", value)
        if timed:
            values[name] = int(timed.group(2)) * (1000 if timed.group(1) == "Seconds" else 60000)
        elif re.fullmatch(r"\d+", value):
            values[name] = int(value)
        elif re.fullmatch(r'"[^"]*"', value):
            values[name] = value[1:-1]
        elif value in ("true", "false"):
            values[name] = value == "true"
    match = re.search(r'#define FIRMWARE_VERSION "([^"]+)"', source)
    values["FIRMWARE_VERSION"] = match.group(1) if match else "unknown"
    return values


FIRMWARE = read_constants()


def constant(name):
    if name not in FIRMWARE:
        sys.exit(f"{name} isn't in {CONSTANTS.relative_to(ROOT)} (has it been renamed?)")
    return FIRMWARE[name]


MAX_NAME = constant("kMaxNameLength")
MAX_WORDS = constant("kMaxWordsLength")
MAX_BRIGHTNESS = constant("kMaxBrightness")
MAX_EYE_GAP = constant("kMaxEyeGap")
MAX_SSID_BYTES = constant("kMaxSsidBytes")
MIN_WIFI_PASSWORD = constant("kMinWifiPasswordLength")
MAX_WIFI_PASSWORD = constant("kMaxWifiPasswordLength")  # 64 only as hex digits
SETUP_SSID = constant("kSetupApName")
SETUP_PASSWORD = constant("kSetupApPassword")
HOSTNAME = constant("kHostname")
DEFAULT_NAME = constant("kDefaultPumpkinName")
BOOT_UNLOCK_SECONDS = constant("kBootUnlockMs") / 1000
RESTART_DELAY_SECONDS = constant("kRestartDelayMs") / 1000
WIFI_SAVED_RESTART_DELAY_SECONDS = constant("kWifiSavedRestartDelayMs") / 1000
MAX_SHOW_BLOCKS = constant("kMaxShowBlocks")
DEFAULT_WIFI_SAVER = constant("kDefaultWifiSaver")
WIFI_IDLE_OFF_MINUTES = constant("kWifiIdleOffMs") // 60000  # "Save batteries" switches WiFi off after this
# Buttons mode goes back to the show after this long without a press or heartbeat
BUTTONS_IDLE_SECONDS = float(os.environ.get("MOCK_IDLE_SECONDS") or constant("kButtonsIdleTimeoutMs") / 1000)
BUTTONS_HEARTBEAT_SECONDS = constant("kButtonsHeartbeatMs") / 1000  # how often an open button page says so

# If show.cpp can't be read: a short show
FALLBACK_SHOW = [
    {"type": "effect", "id": "minecraft"}, {"type": "effect", "id": "pumpkin"}, {"type": "effect", "id": "witch"},
    {"type": "effect", "id": "blink"}, {"type": "words", "text": "Happy Halloween!"},
]

C_STRING = r'(?:u8)?"((?:[^"\\]|\\.)*)"'
# id, label, emoji, group, phrase, function, inSurprise
EFFECT_ENTRY = re.compile(
    r"\{\s*" + C_STRING + r"\s*,\s*" + C_STRING + r"\s*,\s*" + C_STRING +
    r"\s*,\s*EffectGroup::(\w+)\s*,\s*(nullptr|" + C_STRING + r")\s*,\s*(\w+)\s*,\s*(true|false)\s*\}"
)
# A block of the default show in show.cpp: {ShowBlock::Kind::Effect, "creeper"} or {ShowBlock::Kind::Words, "..."}
SHOW_ENTRY = re.compile(r"\{\s*(?:ShowBlock::)?Kind::(Effect|Words)\s*,\s*" + C_STRING + r"\s*\}")
GROUP_NAME = re.compile(r'case\s+EffectGroup::(\w+)\s*:\s*return\s+"([^"]*)"')
# groupTitle()'s Minecraft heading, as built without local/credits.h (the "by" credit)
MINECRAFT_TITLE = re.compile(r'#else\s+#define MINECRAFT_TITLE\s+"([^"]*)"')


def c_unescape(text):
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), text)


def load_effects():
    """The effects table from registry.cpp, in the same order as the firmware."""
    try:
        source = REGISTRY.read_text(encoding="utf-8")
    except OSError as e:
        sys.exit(f"Can't read {REGISTRY.relative_to(ROOT)} ({e.strerror})")
    names = dict(GROUP_NAME.findall(source))
    effects = []
    for match in EFFECT_ENTRY.finditer(source):
        effect_id, label, emoji, group, phrase_raw, phrase, function, in_surprise = match.groups()
        effects.append({
            "id": c_unescape(effect_id),
            "label": c_unescape(label),
            "emoji": c_unescape(emoji),
            "group": names.get(group, group),
            "phrase": None if phrase_raw == "nullptr" else c_unescape(phrase),
            "inSurprise": in_surprise == "true",
            # canBeInShow(): everything but effects that never end (the surprise show)
            "inShow": function != "surpriseShow",
        })
    if not effects:
        sys.exit(f"Couldn't read the effects table in {REGISTRY.relative_to(ROOT)} (has its layout changed?)")
    return effects


def load_group_titles():
    """groupTitle(): section headings that differ from the group's name."""
    match = MINECRAFT_TITLE.search(REGISTRY.read_text(encoding="utf-8"))
    return {"Minecraft": match.group(1)} if match else {}


GROUP_TITLES = load_group_titles()


def load_default_show(by_id):
    """defaultShow() from show.cpp, or a short show if it isn't there (yet)."""
    try:
        source = SHOW_SOURCE.read_text(encoding="utf-8")
    except OSError:
        print(f"  ({SHOW_SOURCE.relative_to(ROOT)} isn't there: using a short default show)")
        return [dict(b) for b in FALLBACK_SHOW]
    blocks = []
    for kind, value in SHOW_ENTRY.findall(source):
        value = c_unescape(value)
        if kind == "Words":
            blocks.append({"type": "words", "text": value})
        elif value in by_id:
            blocks.append({"type": "effect", "id": value})
        else:
            print(f"  (the default show in {SHOW_SOURCE.relative_to(ROOT)} has an unknown effect: {value})")
    if not blocks:
        sys.exit(f"Couldn't read the default show in {SHOW_SOURCE.relative_to(ROOT)} (has its layout changed?)")
    return blocks


# --- The firmware's text rules (src/core/text.h), roughly ---

PLAIN_SWAPS = dict(
    {c: "'" for c in "‘’‚‛′"}, **{c: '"' for c in "“”„‟″"}, **{c: "-" for c in "‐‑‒–—―−"}, **{"…": "..."},
    **{c: " " for c in "\u00a0\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u202f\u205f\u3000"})


# Accented Latin letters lose their accents (U+00C0 to U+017F), like kLatinFold in text.cpp
LATIN_FOLD_SPECIAL = {"Æ": "AE", "æ": "ae", "Ð": "D", "ð": "d", "Ø": "O", "ø": "o", "Þ": "Th", "þ": "th",
                      "ß": "ss", "×": "", "÷": "", "Đ": "D", "đ": "d", "Ħ": "H", "ħ": "h", "ı": "i", "Ĳ": "IJ",
                      "ĳ": "ij", "ĸ": "k", "Ŀ": "L", "ŀ": "l", "Ł": "L", "ł": "l", "ŉ": "n", "Ŋ": "N", "ŋ": "n",
                      "Œ": "OE", "œ": "oe", "Ŧ": "T", "ŧ": "t", "ſ": "s"}


def fold_accent(c):
    if c in LATIN_FOLD_SPECIAL:
        return LATIN_FOLD_SPECIAL[c]
    if 0xC0 <= ord(c) <= 0x17F:
        return unicodedata.normalize("NFD", c)[0]
    return None


def plain(text, drop_other=True):
    """text::plain(): smart punctuation made plain, accents folded, control characters to spaces, the
    rest of non-ASCII dropped (or kept, to spot it), trimmed."""
    out = []
    for c in text:
        folded = fold_accent(c)
        if folded is not None:
            out.append(folded)
        elif c in PLAIN_SWAPS:
            out.append(PLAIN_SWAPS[c])
        elif ord(c) < 32 or 127 <= ord(c) < 160:
            out.append(" ")
        elif ord(c) < 127 or not drop_other:
            out.append(c)
    return "".join(out).strip()


def utf8_size(text):
    """Length in UTF-8 bytes, as the firmware counts (String::length(), strlen()). Bytes that weren't valid
    UTF-8 in the request come back as they arrived (see json_body())."""
    try:
        return len(text.encode("utf-8", "surrogateescape"))
    except UnicodeEncodeError:  # a lone surrogate from a \ud800 escape
        return len(text.encode("utf-8", "surrogatepass"))


def has_unshowable(text):
    """settings.cpp hasUnshowable(): would text::plain() drop any character (emoji, ×, zero-width
    spaces...)? Each character is checked on its own between two plain ones, so trimming can't hide one."""
    return any(len(plain("|" + c + "|")) <= 2 for c in text)


def valid_wifi_password(password):
    """text::validWifiPassword(): empty, 8-63 bytes, or 64 hex digits."""
    size = utf8_size(password)
    if size == 0:
        return True
    if MIN_WIFI_PASSWORD <= size < MAX_WIFI_PASSWORD:
        return True
    return size == MAX_WIFI_PASSWORD and re.fullmatch(r"[0-9a-fA-F]+", password) is not None


class ApiError(Exception):
    def __init__(self, status, message, **extra):
        super().__init__(message)
        self.status = status
        self.extra = extra


def read_text(body, key, what, min_length, max_length, required):
    """web::readText() (web_utils.cpp): a text field from a JSON body, same checks and messages. Lengths are
    in UTF-8 bytes. Returns the text, or None if it's missing (or null) and not required."""
    value = body.get(key)
    if value is None:
        if required:
            raise ApiError(400, f"Please fill in the {what}")
        return None
    if not isinstance(value, str):
        raise ApiError(400, f"The {what} should be text")
    if "\0" in value:
        raise ApiError(400, f"There's a strange character in the {what}")
    size = utf8_size(value)
    if size < min_length:
        raise ApiError(400, f"Please fill in the {what}" if min_length <= 1 else
                       f"Please use at least {min_length} characters for the {what}")
    if size > max_length:
        raise ApiError(400, f"Too long: the {what} can have up to {max_length} characters")
    return value


def boot_needed():
    return ApiError(403, BOOT_PRESS_MESSAGE, needsBoot=True)


class Pumpkin:
    """Everything the pretend pumpkin knows. One lock guards it all."""

    MODES = ("home", "setup", "lost", "wrong", "off")

    def __init__(self, mode):
        self.lock = threading.RLock()
        self.effects = load_effects()
        self.by_id = {e["id"]: e for e in self.effects}
        self.version = FIRMWARE["FIRMWARE_VERSION"] + "-mock"
        self.started = time.time()
        self.name = "Jack's Pumpkin"  # saved (named already, except in setup mode)
        self.brightness = 2
        self.eye_gap = 8
        self.mouth_led = True
        self.wifi_saver = DEFAULT_WIFI_SAVER
        self.wifi_ssid = ""
        self.wifi_password = ""
        self.mode = "home"
        self.set_mode(mode)
        self.default_show = load_default_show(self.by_id)  # defaultShow()
        self.show = [dict(b) for b in self.default_show]   # the saved show (in memory)
        self.show_is_default = True
        self.player_mode = "show"     # player::Mode: "show" or "buttons"
        self.buttons_at = 0.0         # the last press or heartbeat in buttons mode
        self.player = {"mode": "show", "effectId": "", "label": "", "block": -1}
        self.generation = 0           # bumped by every request, so old playback threads stop
        self.scan_started = None      # when the fake scan began
        self.wifi_test = None         # the last WiFi check: {ssid, started, status, rssi, reason} (no password)
        self.restart_at = None        # a restart is due then
        self.down_until = 0.0         # "restarting" until then
        self.boot_pressed_at = None   # the last short BOOT press
        self.update_finished = False  # a firmware upload worked: refuse more until the restart
        self.clients = []             # one queue per server-sent events connection
        with self.lock:
            self.start_show()
        threading.Thread(target=self.watch_buttons_mode, daemon=True).start()

    # --- Modes: home (on home WiFi), setup (never set up), lost (out of reach), wrong (turned away),
    # off (WiFi switched off to save batteries) ---

    def set_mode(self, mode):
        with self.lock:
            if mode not in self.MODES:
                return
            self.mode = mode
            if mode == "setup":
                # A fresh board, as posted: no WiFi and not named yet
                self.wifi_ssid, self.wifi_password = "", ""
                self.name = DEFAULT_NAME
            elif not self.wifi_ssid:
                self.wifi_ssid, self.wifi_password = "Haunted House 2.4G", ""

    def network_mode(self):
        return {"home": "connected", "off": "off"}.get(self.mode, "setup")

    def needs_setup(self):
        return self.mode == "setup"

    def out_of_reach(self):
        """network::homeWifiOutOfReach()"""
        return self.mode == "lost"

    def strangers_may_be_connected(self):
        """web::strangersMayBeConnected(): setup WiFi with home WiFi saved"""
        with self.lock:
            return self.network_mode() == "setup" and bool(self.wifi_ssid)

    def portal_path(self):
        """Where phones' sign-in sheets go: the buttons when the home WiFi is out of reach"""
        return "/" if self.out_of_reach() else "/setup"

    def setup_reason(self):
        if self.mode == "lost":
            return f'Couldn\'t find "{self.wifi_ssid}" - is it a 2.4 GHz network, in range, with the router on?'
        if self.mode == "wrong":
            return f'Couldn\'t join "{self.wifi_ssid}" - wrong password?'
        return ""

    # --- The BOOT button ---

    def press_boot(self, pressed=True):
        with self.lock:
            self.boot_pressed_at = time.time() if pressed else None
        print(f"  (BOOT {'pressed: risky actions unlocked for ' + str(int(BOOT_UNLOCK_SECONDS)) + ' s' if pressed else 'let go'})")

    def boot_unlocked(self):
        with self.lock:
            return self.boot_pressed_at is not None and time.time() - self.boot_pressed_at < BOOT_UNLOCK_SECONDS

    def status(self):
        with self.lock:
            home = self.mode == "home"
            return {
                "name": self.name,
                "defaultName": DEFAULT_NAME,
                "buttonsHeartbeatMs": int(BUTTONS_HEARTBEAT_SECONDS * 1000),
                "version": self.version,
                "uptime": int(time.time() - self.started),
                "freeHeap": 180000 + random.randint(-4000, 4000),
                "mode": self.network_mode(),
                "hasSavedWifi": bool(self.wifi_ssid),
                "homeSsid": self.wifi_ssid,
                "ip": "192.168.1.23" if home else "" if self.mode == "off" else "192.168.4.1",
                "hostname": HOSTNAME if home else "",
                "rssi": -58 if home else 0,
                "setupSsid": SETUP_SSID,
                "setupReason": self.setup_reason(),
                "homeWifiOutOfReach": self.out_of_reach(),
                "wifiNeedsBoot": self.strangers_may_be_connected(),
                "player": dict(self.player),
            }

    def settings(self):
        return {
            "name": self.name,
            "brightness": self.brightness,
            "mouthLed": self.mouth_led,
            "eyeGap": self.eye_gap,
            "wifiSaver": self.wifi_saver,
        }

    # --- Server-sent events ---

    def broadcast(self, event, data):
        with self.lock:
            for client in list(self.clients):
                client.put((event, data))

    def set_player(self, mode, effect_id="", label="", block=-1, generation=None):
        """What the player is doing (player::Status). A playback thread passes its generation, so one that
        has just been replaced can't overwrite the new one's news."""
        with self.lock:
            if generation is not None and generation != self.generation:
                return
            self.player = {"mode": mode, "effectId": effect_id, "label": label, "block": block}
            self.broadcast("status", json.dumps(self.player))

    def say(self, words):
        if words:
            self.broadcast("say", words)

    # --- Playing things (the firmware's player, roughly) ---

    def start(self, run):
        with self.lock:
            self.generation += 1
            generation = self.generation
        threading.Thread(target=run, args=(generation,), daemon=True).start()

    def current(self, generation):
        with self.lock:
            return generation == self.generation

    def sleep(self, seconds, generation):
        end = time.time() + seconds
        while time.time() < end:
            if not self.current(generation):
                return False
            time.sleep(0.1)
        return self.current(generation)

    def block_info(self, block):
        """(effectId, label, seconds) for a show block"""
        if block["type"] == "words":
            return "words", block["text"], 1 + 0.12 * len(block["text"])
        effect = self.by_id[block["id"]]
        return effect["id"], effect["label"], EFFECT_SECONDS

    def start_show(self):
        """player::startShow(): the saved show from the top, round and round. Quietly: the show doesn't
        talk. A newly saved show takes over at the next block (by position). Call with the lock held."""
        if self.player_mode != "show":
            print("  (show mode)")
        self.player_mode = "show"

        def run(generation):
            index = 0
            while True:
                with self.lock:
                    if index >= len(self.show):
                        index = 0
                    effect_id, label, seconds = self.block_info(self.show[index])
                self.set_player("show", effect_id, label, index, generation)
                if not self.sleep(seconds, generation):
                    return
                index += 1
        self.start(run)

    def pressed(self):
        """A press or heartbeat: buttons mode, and its idle timer starts again. True if it was in buttons
        mode already. Call with the lock held."""
        already = self.player_mode == "buttons"
        if not already:
            print(f"  (buttons mode: back to the show after {BUTTONS_IDLE_SECONDS:g} s without a press or heartbeat)")
        self.player_mode = "buttons"
        self.buttons_at = time.time()
        return already

    def enter_buttons(self):
        """player::enterButtons(): pause the show (eyes idle), or "still here" if already in buttons mode."""
        with self.lock:
            quiet = time.time() - self.buttons_at
            if self.player_mode == "buttons" and quiet > BUTTONS_HEARTBEAT_SECONDS + 5:
                print(f"  (buttons heartbeat {quiet:.0f} s after the last press or heartbeat: pages send one "
                      f"every {BUTTONS_HEARTBEAT_SECONDS:g} s while they're on screen)")
            if self.pressed():
                return  # carry on with whatever's playing
        self.start(lambda generation: self.set_player("buttons", generation=generation))

    def watch_buttons_mode(self):
        """Buttons mode times out back to the show (the phone went to sleep, or was put away)"""
        while True:
            time.sleep(0.5)
            with self.lock:
                if self.player_mode == "buttons" and time.time() - self.buttons_at > BUTTONS_IDLE_SECONDS:
                    print(f"  (no press or heartbeat for {BUTTONS_IDLE_SECONDS:g} s: back to the show)")
                    self.start_show()

    def play(self, effect):
        with self.lock:
            self.pressed()

        def run(generation):
            self.set_player("buttons", effect["id"], effect["label"], generation=generation)
            self.say(effect["phrase"])
            if effect["id"] == "surprise":
                # Picks effects until another button is pressed (inSurprise, from the registry)
                choices = [e for e in self.effects if e["inSurprise"]]
                while self.sleep(EFFECT_SECONDS, generation):
                    pick = random.choice(choices)
                    self.say(pick["phrase"])
                return
            if self.sleep(EFFECT_SECONDS, generation):
                self.set_player("buttons", generation=generation)  # done: idle eyes
        self.start(run)

    def scroll_words(self, words):
        with self.lock:
            self.pressed()

        def run(generation):
            self.set_player("buttons", "words", words, generation=generation)
            self.say(words)
            if self.sleep(1 + 0.12 * len(words), generation):
                self.set_player("buttons", generation=generation)
        self.start(run)

    def save_show(self, blocks, is_default):
        """show_store::save() / reset(), then player::showChanged(): a playing show carries on from the next
        block of the new list"""
        with self.lock:
            self.show = blocks
            self.show_is_default = is_default

    def restart_soon(self, delay, new_mode=None):
        """web::restartSoon(): after `delay` seconds the pumpkin goes away for BOOT_SECONDS (the API
        answers 503 and event streams close), then comes back in new_mode with any rename applied."""
        with self.lock:
            if self.restart_at is not None:
                return  # only the first one counts, as on the board
            self.restart_at = time.time() + delay
        print(f"  (restarting in {delay:g} s{', into ' + new_mode + ' mode' if new_mode else ''})")
        timer = threading.Timer(delay, self.power_cycle, args=(new_mode,))
        timer.daemon = True
        timer.start()

    def power_cycle(self, new_mode):
        with self.lock:
            self.generation += 1
            self.restart_at = None
            self.down_until = time.time() + BOOT_SECONDS
            self.started = self.down_until
            self.player_mode = "buttons"  # so start_show() below says "show mode"
            self.boot_pressed_at = None
            self.update_finished = False
            self.wifi_test = None
            if new_mode:
                self.mode = new_mode
            self.broadcast(None, None)  # tells each event stream to hang up
            self.start_show()  # what it does at switch-on
        print(f"  (pretending to restart for {BOOT_SECONDS} s)")

    def restarting(self):
        with self.lock:
            return time.time() < self.down_until

    # --- The setup page's WiFi check (network::startWifiTest() and wifiTest()) ---

    def start_wifi_test(self, ssid, password):
        """Works out now how the check will end (the password isn't kept), and says so after
        WIFI_TEST_SECONDS. False if one is running."""
        with self.lock:
            if self.wifi_test and self.wifi_test["status"] == "running" and not self.wifi_test_done():
                return False
            seen = [n for n in FAKE_NETWORKS if n["ssid"] == ssid]
            if not seen:
                outcome = ("failed", 0, f"Can't find {ssid}: is it a 2.4 GHz network, and in range?")
            elif seen[0]["secure"] and (not password or "wrong" in password.lower()):
                outcome = ("failed", 0, "That password didn't work. Check it (capital letters too), or if the "
                                        "signal is weak, move the pumpkin closer to the router.")
            elif "slow" in (password or "").lower():
                outcome = ("unsure", 0, f"{ssid} didn't answer in time, so the pumpkin couldn't tell whether the "
                                        "password works. Save it anyway, and the pumpkin will try it when it restarts.")
            else:
                outcome = ("passed", max(n["rssi"] for n in seen) + random.randint(-3, 3), "")
            self.wifi_test = {"ssid": ssid, "started": time.time(), "status": "running", "rssi": 0, "reason": "",
                              "outcome": outcome}
        print(f'  (checking "{ssid}": {outcome[0]} in {WIFI_TEST_SECONDS} s'
              f'{", phone off the setup WiFi for " + format(WIFI_DROP_SECONDS, "g") + " s" if WIFI_DROP_SECONDS else ""})')
        return True

    def wifi_test_done(self):
        return time.time() - self.wifi_test["started"] >= WIFI_TEST_SECONDS

    def wifi_test_result(self):
        with self.lock:
            test = self.wifi_test
            if test is None:
                return {"status": "idle", "ssid": "", "rssi": 0, "reason": ""}
            if test["status"] == "running" and self.wifi_test_done():
                test["status"], test["rssi"], test["reason"] = test["outcome"]
                test["finished"] = time.time()
            if test["status"] != "running" and time.time() - test["finished"] >= WIFI_TEST_KEEP_SECONDS:
                return {"status": "idle", "ssid": "", "rssi": 0, "reason": ""}
            return {k: test[k] for k in ("status", "ssid", "rssi", "reason")}

    def phone_off_setup_wifi(self):
        """MOCK_WIFI_DROP_SECONDS: the phone is off the setup WiFi for a while after a check starts"""
        with self.lock:
            return (WIFI_DROP_SECONDS > 0 and self.wifi_test is not None and self.network_mode() == "setup"
                    and time.time() - self.wifi_test["started"] < WIFI_DROP_SECONDS)


class Handler(BaseHTTPRequestHandler):
    server_version = "PumpkinMock/1.0"
    pumpkin = None  # set in main()

    # --- Plumbing ---

    def log_message(self, fmt, *args):
        if self.path.startswith(("/api/", "/events", "/mock/")):
            sys.stderr.write("%s %s\n" % (time.strftime("%H:%M:%S"), fmt % args))

    def send_json(self, status, data):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def redirect(self, location):
        self.send_response(HTTPStatus.FOUND)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def send_file(self, path):
        data = path.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", CONTENT_TYPES.get(path.suffix, "application/octet-stream"))
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def read_body(self, limit):
        length = int(self.headers.get("Content-Length") or 0)
        if length > limit:
            self.rfile.read(length)  # drain it, as the firmware does
            raise ApiError(413, "That's too much to send")
        return self.rfile.read(length) if length else b""

    def drain(self):
        """Read and drop the body of a request that's refused before its handler runs"""
        length = int(self.headers.get("Content-Length") or 0)
        if 0 < length <= MAX_UPLOAD:
            self.rfile.read(length)

    def json_body(self, limit=MAX_JSON_BODY):
        """web::onJsonPost(): the body as a JSON object ({} if there's none), or a 400 as the firmware gives"""
        raw = self.read_body(limit)
        if not raw:
            return {}
        content_type = self.headers.get("Content-Type", "")
        if content_type.startswith(("application/x-www-form-urlencoded", "multipart/form-data")):
            # The web server library reads form bodies itself, so the handler never sees them
            raise ApiError(400, "The pumpkin couldn't read that request (it expects JSON)")
        try:
            # ArduinoJson doesn't check UTF-8: bytes that aren't valid UTF-8 go through as they are (and
            # text::plain() drops them later). surrogateescape keeps them, and plain() drops them too.
            body = json.loads(raw.decode("utf-8", "surrogateescape"))
        except ValueError:
            raise ApiError(400, "That request didn't make sense (bad JSON)")
        if not isinstance(body, dict):
            raise ApiError(400, "That request didn't make sense (bad JSON)")
        return body

    def our_host(self):
        """Was this meant for the pumpkin, or for a website the setup WiFi's DNS would send here?"""
        host = (self.headers.get("Host") or "").lower()
        host = host[1:host.find("]")] if host.startswith("[") else host.split(":")[0]
        return host in ("", "localhost", "127.0.0.1", "::1", "192.168.4.1") or host.endswith(".local")

    def portal_url(self):
        host = self.headers.get("Host") if self.our_host() else f"localhost:{self.server.server_port}"
        return f"http://{host}{self.pumpkin.portal_path()}"

    # --- Requests ---

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    # Not used by the pages, but the firmware answers them: 404 (JSON for /api/ paths), as for any other
    # address it doesn't have
    def do_PUT(self):
        self.route("PUT")

    def do_DELETE(self):
        self.route("DELETE")

    def do_PATCH(self):
        self.route("PATCH")

    def send_not_found_page(self):
        body = b'<!doctype html><p style="font:20px system-ui">Nothing here. <a href="/">Back to the pumpkin</a></p>'
        self.send_response(HTTPStatus.NOT_FOUND)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def route(self, method):
        url = urlsplit(self.path)
        query = parse_qs(url.query)
        pumpkin = self.pumpkin
        if "mode" in query:
            pumpkin.set_mode(query["mode"][0])
            print(f"  (now in {pumpkin.mode} mode)")
        if "boot" in query:
            pumpkin.press_boot(query["boot"][0] != "0")
        path = url.path

        # Captive portal, as in setup mode on the board (any method)
        if pumpkin.network_mode() == "setup" and (path in PROBE_PATHS or not self.our_host()):
            self.drain()
            self.redirect(self.portal_url())
            return

        try:
            if path == "/mock/boot" and method == "POST":
                self.read_body(MAX_JSON_BODY)
                pumpkin.press_boot()
                self.send_json(200, {"ok": True, "message": f"BOOT pressed: unlocked for {int(BOOT_UNLOCK_SECONDS)} s"})
                return
            if path.startswith("/api/") or (path == "/events" and method == "GET"):
                if pumpkin.phone_off_setup_wifi():
                    self.close_connection = True  # no answer, as if the phone had left the setup WiFi
                    return
                if pumpkin.restarting():
                    if path == "/events":
                        self.close_connection = True  # like a board that's off: the browser retries
                        return
                    self.drain()
                    raise ApiError(503, "Restarting")
                # As on the board: an address it doesn't have is a 404 before anything else is checked,
                # then every POST needs the header
                handler = API.get((method, path))
                if handler is None:
                    self.drain()
                    raise ApiError(404, f"There's no {method} {path}")
                if method == "POST" and self.headers.get("X-Pumpkin") != "1":
                    self.drain()
                    raise ApiError(403, "Missing X-Pumpkin header")
                handler(self, query)
                return
            if method == "GET":
                self.serve_page(path, query)
                return
            # Any other method on a page's address (in setup mode too: only GETs are sent to the portal)
            self.drain()
            self.send_not_found_page()
        except ApiError as e:
            self.send_json(e.status, dict({"error": str(e)}, **e.extra))
        except (BrokenPipeError, ConnectionResetError):
            pass

    def serve_page(self, path, query):
        pages = {"/": "index.html", "/setup": "setup.html", "/settings": "settings.html", "/show": "show.html"}
        if path == "/" and self.pumpkin.needs_setup() and "play" not in query:
            self.redirect("/setup")
            return
        name = pages.get(path, path.lstrip("/"))
        target = (WEB / name).resolve()
        if WEB.resolve() in target.parents and target.is_file():
            self.send_file(target)
        elif self.pumpkin.network_mode() == "setup":
            self.redirect(self.portal_url())
        else:
            self.send_not_found_page()

    # --- API (same paths, bodies and replies as src/web/*.cpp) ---

    def api_effects(self, query):
        effects = [{k: e[k] for k in ("id", "label", "emoji", "group", "inShow")} for e in self.pumpkin.effects]
        self.send_json(200, {"effects": effects, "groupTitles": GROUP_TITLES})

    def api_status(self, query):
        self.send_json(200, self.pumpkin.status())

    def api_play(self, query):
        effect_id = read_text(self.json_body(), "id", "effect", 1, MAX_EFFECT_ID, True)
        effect = self.pumpkin.by_id.get(effect_id)
        if effect is None:
            raise ApiError(404, "The pumpkin doesn't know an effect called " + effect_id)
        self.pumpkin.play(effect)
        self.send_json(200, {"ok": True})

    def api_words(self, query):
        # Up to 4 bytes a letter before tidying (emoji), then up to MAX_WORDS letters after
        words = read_text(self.json_body(), "words", "words", 1, MAX_WORDS * UTF8_BYTES_PER_CHAR, True)
        words = plain(words)
        if not words:
            raise ApiError(400, "Type some words first (the eyes can show letters, numbers and symbols)")
        if len(words) > MAX_WORDS:
            raise ApiError(400, f"That's too long: up to {MAX_WORDS} letters")
        self.pumpkin.scroll_words(words)
        self.send_json(200, {"ok": True})

    def api_mode(self, query):
        mode = self.json_body().get("mode")
        if mode not in ("show", "buttons"):
            raise ApiError(400, 'The mode should be "show" or "buttons"')
        pumpkin = self.pumpkin
        if mode == "buttons":
            pumpkin.enter_buttons()
        else:
            with pumpkin.lock:
                if pumpkin.player_mode != "show":
                    pumpkin.start_show()  # does nothing if the show is playing already
        self.send_json(200, {"ok": True, "mode": mode})

    # --- The show creator ---

    def api_get_show(self, query):
        pumpkin = self.pumpkin
        with pumpkin.lock:
            reply = {"blocks": [dict(b) for b in pumpkin.show], "isDefault": pumpkin.show_is_default}
        reply["limits"] = {"maxBlocks": MAX_SHOW_BLOCKS, "maxWordsLength": MAX_WORDS}
        self.send_json(200, reply)

    @staticmethod
    def read_block(block, number):
        """show_handlers.cpp readBlocks(), for one block (same messages): (kind, value) or an ApiError
        naming the block (1-based). Only the shape: check_block() checks the rest."""
        def fail(reason):
            return ApiError(400, f"Block {number}: {reason}")
        kind = block.get("type") if isinstance(block, dict) else None
        if kind == "effect":
            effect_id = block.get("id")
            if not isinstance(effect_id, str):
                raise fail("which effect?")
            if "\0" in effect_id:
                raise fail("there's a strange character in the effect's name")
            return kind, effect_id
        if kind == "words":
            text = block.get("text")
            if not isinstance(text, str):
                raise fail("the words should be text")
            if "\0" in text:
                raise fail("there's a strange character in the words")
            return kind, text
        raise fail("should be an effect or some words")

    def check_block(self, kind, value, number):
        """show.cpp checkBlock() (same messages): the block tidied, or an ApiError naming it (1-based)"""
        def fail(reason):
            return ApiError(400, f"Block {number}: {reason}")
        if kind == "effect":
            effect = self.pumpkin.by_id.get(value)
            if effect is None:
                shown = value.encode("utf-8", "surrogateescape")[:LONGEST_ID_SHOWN].decode("utf-8", "ignore")
                raise fail(f'no effect called "{shown}"')
            if not effect["inShow"]:
                raise fail(effect["label"] + " never ends, so it can't go in a show")
            return {"type": "effect", "id": value}
        text = plain(value)[:MAX_WORDS].strip()
        if not text:
            raise fail("no words to scroll (the eyes can't show emoji)")
        return {"type": "words", "text": text}

    def api_post_show(self, query):
        # The firmware's order: the BOOT gate on the doorstep, then readBlocks() reads every block's shape,
        # then checkShow() counts them and checks each one
        body = self.json_body(MAX_SHOW_BODY)
        self.doorstep_gate()
        blocks = body.get("blocks")
        if not isinstance(blocks, list):
            raise ApiError(400, "The show should be a list of blocks")
        read = [self.read_block(block, number) for number, block in enumerate(blocks, 1)]
        if not read:
            raise ApiError(400, "The show needs at least one block")
        if len(read) > MAX_SHOW_BLOCKS:
            raise ApiError(400, f"That show is too long (up to {MAX_SHOW_BLOCKS} blocks)")
        tidied = [self.check_block(kind, value, number) for number, (kind, value) in enumerate(read, 1)]
        self.pumpkin.save_show(tidied, False)
        print(f"  (show saved: {len(tidied)} blocks)")
        self.send_json(200, {"ok": True, "blocks": tidied})

    def api_reset_show(self, query):
        self.json_body()
        self.doorstep_gate()
        blocks = [dict(b) for b in self.pumpkin.default_show]
        self.pumpkin.save_show(blocks, True)
        print("  (back to the original show)")
        self.send_json(200, {"ok": True, "blocks": [dict(b) for b in blocks]})

    def api_get_settings(self, query):
        reply = self.pumpkin.settings()
        reply["limits"] = {
            "maxBrightness": MAX_BRIGHTNESS, "maxEyeGap": MAX_EYE_GAP, "maxNameLength": MAX_NAME,
            "maxWordsLength": MAX_WORDS, "maxSsidBytes": MAX_SSID_BYTES,
            "minWifiPassword": MIN_WIFI_PASSWORD, "maxWifiPassword": MAX_WIFI_PASSWORD,
            "wifiIdleOffMinutes": WIFI_IDLE_OFF_MINUTES,
        }
        self.send_json(200, reply)

    @staticmethod
    def check_name(name):
        """settings.cpp checkName() (same messages). Returns the tidied name."""
        if has_unshowable(name):
            raise ApiError(400, "Please use letters, numbers and punctuation in the name (the eyes can't show emoji)")
        name = plain(name)
        if not name:
            raise ApiError(400, "Please give the pumpkin a name")
        if len(name) > MAX_NAME:
            raise ApiError(400, f"That name is too long (up to {MAX_NAME} characters)")
        return name

    @staticmethod
    def read_new_name(body):
        """api_handlers.cpp readNewName(): an optional name, only checked as text here (check_name() tidies it)"""
        return read_text(body, "name", "pumpkin's name", 0, MAX_JSON_BODY, False)

    @staticmethod
    def check_int(body, key, what, top):
        """web::readInt() (same messages). ArduinoJson's is<int>() is false for numbers that don't fit a
        32-bit int, so those aren't whole numbers either."""
        value = body.get(key)
        if value is None:
            return None
        if not isinstance(value, int) or isinstance(value, bool) or not -2**31 <= value < 2**31:
            raise ApiError(400, f"The {what} should be a whole number")
        if not 0 <= value <= top:
            raise ApiError(400, f"The {what} should be from 0 to {top}")
        return value

    def doorstep_gate(self):
        """Settings and show changes need a BOOT press while strangers may be on the setup WiFi"""
        if self.pumpkin.strangers_may_be_connected() and not self.pumpkin.boot_unlocked():
            raise boot_needed()

    def api_post_settings(self, query):
        # Same order as the firmware: the BOOT gate on the doorstep, the request's shape, then settings::save()
        body = self.json_body()
        self.doorstep_gate()
        pumpkin = self.pumpkin
        new_name = self.read_new_name(body)
        brightness = self.check_int(body, "brightness", "brightness", MAX_BRIGHTNESS)
        eye_gap = self.check_int(body, "eyeGap", "gap between the eyes", MAX_EYE_GAP)
        mouth = body.get("mouthLed")
        if mouth is not None and not isinstance(mouth, bool):
            raise ApiError(400, "The mouth light setting should be on or off")
        saver = body.get("wifiSaver")
        if saver is not None and not isinstance(saver, bool):
            raise ApiError(400, "The save batteries setting should be on or off")
        with pumpkin.lock:
            if new_name is not None:
                pumpkin.name = self.check_name(new_name)
            if brightness is not None:
                pumpkin.brightness = brightness
            if eye_gap is not None:
                pumpkin.eye_gap = eye_gap
            if mouth is not None:
                pumpkin.mouth_led = mouth
            if saver is not None:
                pumpkin.wifi_saver = saver
                print(f"  (save batteries {'on: the board switches WiFi off after ' + str(WIFI_IDLE_OFF_MINUTES) + ' minutes without requests' if saver else 'off'})")
            settings = pumpkin.settings()
        self.send_json(200, {"ok": True, "message": "Saved", "settings": settings})

    def api_restart(self, query):
        self.json_body()
        self.send_json(200, {"ok": True, "message": "The pumpkin is restarting. It'll be back in about 20 seconds."})
        self.pumpkin.restart_soon(RESTART_DELAY_SECONDS)

    def api_wifi_scan(self, query):
        pumpkin = self.pumpkin
        if pumpkin.network_mode() == "off":
            raise ApiError(503, "The pumpkin couldn't look for WiFi just now. Try again in a few seconds.")
        with pumpkin.lock:
            if pumpkin.scan_started is None:
                pumpkin.scan_started = time.time()
                self.send_json(202, {"scanning": True})
                return
            if time.time() - pumpkin.scan_started < 1.5:
                self.send_json(202, {"scanning": True})
                return
            pumpkin.scan_started = None
        # Like network::scan(): strongest first, one entry per name
        best = {}
        for net in FAKE_NETWORKS:
            net = dict(net, rssi=net["rssi"] + random.randint(-3, 3))
            if net["ssid"] not in best or net["rssi"] > best[net["ssid"]]["rssi"]:
                best[net["ssid"]] = net
        self.send_json(200, {"networks": sorted(best.values(), key=lambda n: -n["rssi"])})

    @staticmethod
    def check_wifi(body):
        """The WiFi name and password from a request, read as wifi_handlers.cpp does (same messages; lengths
        in bytes). No password = an open network."""
        ssid = read_text(body, "ssid", "WiFi name", 1, MAX_SSID_BYTES, True)
        password = read_text(body, "password", "WiFi password", 0, MAX_WIFI_PASSWORD, False) or ""
        return ssid, password

    @staticmethod
    def check_wifi_password(password):
        """settings.cpp checkWifi()'s password rule (same messages)"""
        if not valid_wifi_password(password):
            raise ApiError(400, f"WiFi passwords have at least {MIN_WIFI_PASSWORD} characters (leave it empty if "
                                "the WiFi has no password)" if utf8_size(password) < MIN_WIFI_PASSWORD else
                                f"That WiFi password is too long ({MAX_WIFI_PASSWORD - 1} characters at most)")

    def api_wifi_test_start(self, query):
        # Same order as the firmware: the request, then home WiFi (skipped), the BOOT gate, a check running
        body = self.json_body()
        ssid, password = self.check_wifi(body)
        self.check_wifi_password(password)
        # The pumpkin's name, if sent, is checked now (wifi_handlers.cpp), so a bad one is said before the wait
        name = self.read_new_name(body)
        if name:
            try:
                self.check_name(name)
            except ApiError as e:
                raise ApiError(e.status, str(e), field="name")
        pumpkin = self.pumpkin
        if pumpkin.network_mode() != "setup":
            self.send_json(200, {"skipped": True, "reason": "The pumpkin is on its home WiFi, and checking "
                                 "another would cut it off. It saves without a check."})
            return
        if pumpkin.strangers_may_be_connected() and not pumpkin.boot_unlocked():
            raise boot_needed()
        if not pumpkin.start_wifi_test(ssid, password):
            raise ApiError(409, "A check is already running")
        self.send_json(202, {"testing": True})

    def api_wifi_test_result(self, query):
        self.send_json(200, self.pumpkin.wifi_test_result())

    def api_wifi(self, query):
        # Same order as the firmware: the request's shape, then the BOOT gate, then settings::save()
        body = self.json_body()
        ssid, password = self.check_wifi(body)
        new_name = self.read_new_name(body)
        pumpkin = self.pumpkin
        with pumpkin.lock:
            if pumpkin.strangers_may_be_connected() and not pumpkin.boot_unlocked():
                raise boot_needed()
            name = self.check_name(new_name) if new_name is not None else pumpkin.name
            self.check_wifi_password(password)
            pumpkin.name = name
            pumpkin.wifi_ssid, pumpkin.wifi_password = ssid, password
        self.send_json(200, {
            "ok": True, "ssid": ssid, "name": name, "address": f"http://{HOSTNAME}.local",
            "setupSsid": SETUP_SSID, "message": f"Saved. The pumpkin is restarting and joining {ssid}.",
        })
        # Longer than usual: the phone shows the "what next" page until the setup WiFi goes
        pumpkin.restart_soon(WIFI_SAVED_RESTART_DELAY_SECONDS, "home")

    def api_wifi_forget(self, query):
        self.json_body()
        pumpkin = self.pumpkin
        with pumpkin.lock:
            if pumpkin.strangers_may_be_connected() and not pumpkin.boot_unlocked():
                raise boot_needed()
            pumpkin.wifi_ssid, pumpkin.wifi_password = "", ""
        self.send_json(200, {
            "ok": True, "setupSsid": SETUP_SSID,
            "message": f"Done. The pumpkin is restarting and will make its own WiFi called \"{SETUP_SSID}\" "
                       f"(password: {SETUP_PASSWORD}). Join it to choose a new home WiFi.",
        })
        pumpkin.restart_soon(RESTART_DELAY_SECONDS, "setup")

    def api_update(self, query):
        content_type = self.headers.get("Content-Type", "")
        match = re.search(r"boundary=(?:\"([^\"]+)\"|([^;]+))", content_type)
        body = self.read_body(MAX_UPLOAD)
        pumpkin = self.pumpkin
        if pumpkin.update_finished:
            raise ApiError(400, "The pumpkin already has the new firmware and is restarting. Wait for it to come back.")
        if not pumpkin.boot_unlocked():
            raise boot_needed()
        if not content_type.startswith("multipart/form-data") or not match:
            raise ApiError(400, "No firmware file arrived. Choose the file ending -ota.bin and try again.")
        boundary = ("--" + (match.group(1) or match.group(2)).strip()).encode()
        data = None
        for part in body.split(boundary):
            head, sep, content = part.partition(b"\r\n\r\n")
            if sep and b"filename=" in head:
                data = content[:-2] if content.endswith(b"\r\n") else content
                break
        if data is None:
            raise ApiError(400, "No firmware file arrived. Choose the file ending -ota.bin and try again.")
        # Same checks as update_handler.cpp: app image header, ESP32-C3 chip id, app description
        if len(data) < 36:
            raise ApiError(400, "That file is too small to be pumpkin firmware.")
        if data[0] != 0xE9:
            raise ApiError(400, "That isn't a firmware file. Choose the file ending -ota.bin.")
        if int.from_bytes(data[12:14], "little") != 0x0005:
            raise ApiError(400, "That firmware is for a different kind of board, not the pumpkin's ESP32-C3.")
        if int.from_bytes(data[32:36], "little") != 0xABCD5432:
            raise ApiError(400, "That's the full image for new boards (-full.bin). Choose the file ending -ota.bin instead.")
        print(f"  (firmware upload accepted: {len(data)} bytes)")
        pumpkin.update_finished = True
        self.send_json(200, {"ok": True, "message": "Updated! The pumpkin is restarting with the new firmware."})
        pumpkin.restart_soon(RESTART_DELAY_SECONDS)

    def api_events(self, query):
        pumpkin = self.pumpkin
        client = queue.Queue()
        with pumpkin.lock:
            pumpkin.clients.append(client)
            hello = json.dumps(pumpkin.player)
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        try:
            self.wfile.write(f"retry: 3000\nevent: status\ndata: {hello}\n\n".encode())
            self.wfile.flush()
            while True:
                try:
                    event, data = client.get(timeout=25)
                except queue.Empty:
                    event, data = "ping", ""
                if event is None:
                    break  # "restarting"
                lines = "".join(f"data: {line}\n" for line in str(data).split("\n"))
                self.wfile.write(f"event: {event}\n{lines}\n".encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            with pumpkin.lock:
                pumpkin.clients.remove(client)
        self.close_connection = True


API = {
    ("GET", "/api/effects"): Handler.api_effects,
    ("GET", "/api/status"): Handler.api_status,
    ("GET", "/api/settings"): Handler.api_get_settings,
    ("GET", "/api/wifi/scan"): Handler.api_wifi_scan,
    ("GET", "/api/wifi/test"): Handler.api_wifi_test_result,
    ("GET", "/events"): Handler.api_events,
    ("POST", "/api/play"): Handler.api_play,
    ("POST", "/api/words"): Handler.api_words,
    ("GET", "/api/show"): Handler.api_get_show,
    ("POST", "/api/show"): Handler.api_post_show,
    ("POST", "/api/show/reset"): Handler.api_reset_show,
    ("POST", "/api/mode"): Handler.api_mode,
    ("POST", "/api/settings"): Handler.api_post_settings,
    ("POST", "/api/restart"): Handler.api_restart,
    ("POST", "/api/wifi/test"): Handler.api_wifi_test_start,
    ("POST", "/api/wifi"): Handler.api_wifi,
    ("POST", "/api/wifi/forget"): Handler.api_wifi_forget,
    ("POST", "/api/update"): Handler.api_update,
}


def main():
    parser = argparse.ArgumentParser(description="Pretend pumpkin for working on the web pages.")
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", "8080")))
    parser.add_argument("--host", default="127.0.0.1", help="0.0.0.0 to try it from a phone on the same WiFi")
    parser.add_argument("--mode", choices=Pumpkin.MODES, default=os.environ.get("PUMPKIN_MODE", "home"))
    args = parser.parse_args()

    Handler.pumpkin = Pumpkin(args.mode)
    try:
        server = ThreadingHTTPServer((args.host, args.port), Handler)
    except OSError as e:
        sys.exit(f"Can't use port {args.port} ({e.strerror}). Something else may be using it: try --port 8081")
    server.daemon_threads = True
    # server_port: the one it got, if --port 0 asked for any free port (tools/api_check.py reads this line)
    print(f"Pretend pumpkin ({args.mode} mode, {len(Handler.pumpkin.effects)} effects) on "
          f"http://localhost:{server.server_port}", flush=True)
    print("Add ?mode=home|setup|lost|wrong|off to any address to switch, ?boot=1 to press BOOT. Ctrl-C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
