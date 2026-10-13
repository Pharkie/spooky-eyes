"""The API conformance cases: what the firmware's web API should answer, request by request.

tools/api_check.py sends each case to the mock server (tools/mock_server.py), to a real board, or to both,
and checks the replies. The firmware is the reference: when the mock and a board disagree, the mock is
wrong unless a case's note says otherwise.

Each Case says:
    modes     which network modes it applies to: home (on home WiFi), setup (its own WiFi, never set up),
              lost (own WiFi, home WiFi out of reach: the doorstep), wrong (own WiFi, home WiFi turned it
              away) and off (WiFi switched off to save batteries: the mock only, a board can't answer then).
              These are the mock's --mode names; on a board the mode is read from GET /api/status.
    method, path, headers, body
              body: a dict or list is sent as JSON the way the pages send it (JSON.stringify: compact,
              UTF-8, not \\u-escaped), bytes are sent as they are, None sends no body. A callable gets the
              run's context (see api_check.py) and returns one of those. POSTs get "X-Pumpkin: 1" and,
              with a JSON body, "Content-Type: application/json"; a header set to None here is left out.
    status    the expected HTTP status
    shape     the reply's JSON shape: a dict lists keys that must be there (more are fine) with their
              shapes; [shape] is a list whose items all have that shape; a tuple is "any of these";
              str/int/bool/float/dict/list/NULL are JSON types (int never matches true/false)
    expect    exact values: {"dotted.path": value or Check}
    error     the expected "error" text. A different text is only a warning: the status and shape are
              what the pages rely on
    location  a regex the Location header must match (redirects)
    content_type   what the Content-Type must start with
    forbid    text that must not appear anywhere in the reply (a WiFi password, say)
    poll      repeat the request until poll.until(status, json) is true (a WiFi scan, a WiFi check)
    boot      needs the BOOT button pressed first (the mock: /mock/boot; a board: the serial "unlock"
              command, with --serial). These run after the others, as a press can't be undone.
    needs     opt-in flags it needs anywhere (--allow-restart, --allow-wifi-save, --allow-forget): it really
              restarts, saves WiFi or forgets it. These run last.
    board_needs    opt-in flags it needs on a board only: only settings::save() stands between the request
              and a WiFi save (or the BOOT gate and a forget), so a firmware bug there would change the
              board's WiFi. The mock runs them anyway.
    targets   "mock", "board" or both

status, shape, error, expect and location can differ by mode: by_mode(home=..., setup=..., default=...).

Limits come from src/core/constants.h, effects and phrases from src/effects/registry.cpp (read the same
way as the mock server reads them), so the cases follow the firmware when it changes. Every WiFi name,
password and pumpkin name here is invented.
"""

import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Dict, FrozenSet, Optional, Tuple

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mock_server as source  # noqa: E402  (only its readers of the firmware's source are used)

# --- What the firmware's source says ---

FIRMWARE = source.FIRMWARE
VERSION = FIRMWARE["FIRMWARE_VERSION"]
MAX_NAME = source.MAX_NAME
MAX_WORDS = source.MAX_WORDS
MAX_BRIGHTNESS = source.MAX_BRIGHTNESS
MAX_EYE_GAP = source.MAX_EYE_GAP
MAX_SSID = source.MAX_SSID_BYTES
MIN_PASSWORD = source.MIN_WIFI_PASSWORD
MAX_PASSWORD = source.MAX_WIFI_PASSWORD
MAX_BLOCKS = source.MAX_SHOW_BLOCKS
MAX_BODY = source.MAX_JSON_BODY          # web_utils.h kMaxJsonBodyBytes
MAX_SHOW_BODY = source.MAX_SHOW_BODY     # web_utils.h kMaxShowBodyBytes
MAX_ID = source.MAX_EFFECT_ID            # api_handlers.cpp kMaxEffectIdLength
MAX_WORDS_BYTES = MAX_WORDS * source.UTF8_BYTES_PER_CHAR
SETUP_SSID = source.SETUP_SSID
DEFAULT_NAME = source.DEFAULT_NAME

EFFECTS = source.load_effects()
EFFECT_IDS = [e["id"] for e in EFFECTS]
BY_ID = {e["id"]: e for e in EFFECTS}
DEFAULT_SHOW = source.load_default_show(BY_ID)
# An effect that has a phrase, for the talking-pumpkin check (the bat squeaks)
SAY_EFFECT = "bat" if BY_ID.get("bat", {}).get("phrase") else next(e["id"] for e in EFFECTS if e["phrase"])
NEVER_ENDS = next(e for e in EFFECTS if not e["inShow"])  # the surprise show
PLAYABLE = "blink"  # short, harmless, and in every show

MODES = ("home", "setup", "lost", "wrong", "off")
ALL = MODES
SETUP_WIFI = ("setup", "lost", "wrong")    # the pumpkin's own WiFi: captive portal
DOORSTEP = ("lost", "wrong")               # its own WiFi with home WiFi saved: strangers may be on it
NOT_SETUP_WIFI = ("home", "off")
SAVES_NOT_GATED = ("home", "setup", "off")  # where WiFi changes don't need a BOOT press

# Opt-in flags
RESTART = "restart"
WIFI_SAVE = "wifi-save"
FORGET = "forget"

# Text with something nasty in it. Invented names only (public repo).
EMOJI = "\U0001F383"   # 🎃, 4 bytes in UTF-8
E_ACUTE = "\u00e9"     # é, 2 bytes
POUND = "\u00a3"       # £, 2 bytes
EURO = "\u20ac"        # €, 3 bytes
NUL = "\u0000"
TEST_SSID = "Pumpkin Test Net"     # invented: no WiFi should have this name
TEST_PASSWORD = "not-a-real-password"
HEX64 = "0123456789abcdefABCDEF0123456789abcdef0123456789ABCDEF0123456789"
NULL = type(None)


# --- Building blocks for the table ---

class ByMode:
    """A value that depends on the network mode: by_mode(home=..., default=...)"""

    def __init__(self, values):
        self.values = values

    def get(self, mode):
        if mode in self.values:
            return self.values[mode]
        if "default" in self.values:
            return self.values["default"]
        raise KeyError(f"no value for mode {mode}")


def by_mode(**values):
    return ByMode(values)


def for_mode(value, mode):
    return value.get(mode) if isinstance(value, ByMode) else value


class Check:
    """A test for one value in a reply, with words for the report"""

    def __init__(self, test, words):
        self.test = test
        self.words = words

    def __call__(self, value):
        try:
            return bool(self.test(value))
        except Exception:  # a wrong type, say: just doesn't match
            return False

    def __repr__(self):
        return self.words


NON_EMPTY = Check(lambda v: len(v) > 0, "not empty")


def one_of(*values):
    return Check(lambda v: v in values, "one of " + ", ".join(repr(v) for v in values))


def length(n):
    return Check(lambda v: len(v) == n, f"{n} long")


def all_values(kind):
    return Check(lambda v: all(isinstance(x, kind) for x in v.values()), f"all {kind.__name__}")


class Skip(Exception):
    """Raised by a case's body function when the case can't run here (the runner reports it as skipped)"""


@dataclass
class Poll:
    """Repeat the request until until(status, json) is true, or give up after timeout seconds"""
    until: Callable[[int, Any], bool]
    timeout: float = 30
    interval: float = 1


@dataclass
class Case:
    name: str
    method: str
    path: str
    body: Any = None
    headers: Optional[Dict[str, Optional[str]]] = None
    modes: Tuple[str, ...] = ALL
    status: Any = 200
    shape: Any = None
    expect: Any = None
    error: Any = None
    location: Any = None
    content_type: Optional[str] = None
    forbid: Tuple[str, ...] = ()
    poll: Optional[Poll] = None
    boot: bool = False
    needs: FrozenSet[str] = frozenset()
    board_needs: FrozenSet[str] = frozenset()
    targets: Tuple[str, ...] = ("mock", "board")
    kind: str = "http"     # "http", or "sse" for the server-sent events check
    after: Optional[str] = None  # "restart": wait for the pumpkin to come back
    note: str = ""


def encode_json(data):
    """JSON as a page's JSON.stringify() sends it: compact, UTF-8, nothing \\u-escaped"""
    return json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8", "surrogatepass")


def padded_json(data, size, key="pad"):
    """JSON of exactly `size` bytes: `data` plus a "pad" field to make up the size (ignored by the API)"""
    missing = size - len(encode_json(dict(data, **{key: ""})))
    assert missing >= 0, f"{data} is already over {size} bytes"
    body = encode_json(dict(data, **{key: "x" * missing}))
    assert len(body) == size
    return body


def letters(n, text="Spooky pumpkin "):
    """n plain letters"""
    return (text * (n // len(text) + 1))[:n]


def words_block(text):
    return {"type": "words", "text": text}


def effect_block(effect_id):
    return {"type": "effect", "id": effect_id}


# Shapes of replies
ERROR = {"error": str}
BOOT_ERROR = {"error": str, "needsBoot": bool}
OK = {"ok": bool}
PLAYER = {"mode": str, "effectId": str, "label": str, "block": int}
STATUS = {
    "name": str, "defaultName": str, "buttonsHeartbeatMs": int, "version": str, "uptime": int, "freeHeap": int,
    "mode": str, "hasSavedWifi": bool, "homeSsid": str, "ip": str, "hostname": str, "rssi": int,
    "setupSsid": str, "setupReason": str, "homeWifiOutOfReach": bool, "wifiNeedsBoot": bool, "player": PLAYER,
}
SETTINGS = {"name": str, "brightness": int, "mouthLed": bool, "eyeGap": int, "wifiSaver": bool}
LIMITS = {
    "maxBrightness": int, "maxEyeGap": int, "maxNameLength": int, "maxWordsLength": int, "maxSsidBytes": int,
    "minWifiPassword": int, "maxWifiPassword": int, "wifiIdleOffMinutes": int,
}
SAVED = {"ok": bool, "message": str, "settings": SETTINGS}
BLOCK = ({"type": str, "id": str}, {"type": str, "text": str})
SHOW = {"blocks": [BLOCK], "isDefault": bool, "limits": {"maxBlocks": int, "maxWordsLength": int}}
SHOW_SAVED = {"ok": bool, "blocks": [BLOCK]}
WIFI_TEST = {"status": str, "ssid": str, "rssi": int, "reason": str}
SKIPPED = {"skipped": bool, "reason": str}

# Error texts used more than once (the firmware's words)
BOOT_PRESS = "Press the small BOOT button on the pumpkin's board, then try again"
NO_HEADER = "Missing X-Pumpkin header"
BAD_JSON = "That request didn't make sense (bad JSON)"
NOT_JSON = "The pumpkin couldn't read that request (it expects JSON)"
TOO_MUCH = "That's too much to send"
NAME_LETTERS = "Please use letters, numbers and punctuation in the name (the eyes can't show emoji)"
NAME_EMPTY = "Please give the pumpkin a name"
NAME_TOO_LONG = f"That name is too long (up to {MAX_NAME} characters)"
WORDS_EMPTY = "Type some words first (the eyes can show letters, numbers and symbols)"
WORDS_TOO_LONG = f"That's too long: up to {MAX_WORDS} letters"
PASSWORD_SHORT = (f"WiFi passwords have at least {MIN_PASSWORD} characters "
                  "(leave it empty if the WiFi has no password)")
PASSWORD_LONG_HEX = f"That WiFi password is too long ({MAX_PASSWORD - 1} characters at most)"
MODE_WORDS = 'The mode should be "show" or "buttons"'


def strange(what):
    return f"There's a strange character in the {what}"


def too_long(what, limit):
    return f"Too long: the {what} can have up to {limit} characters"


def no_route(method, path):
    return f"There's no {method} {path}"


# The captive portal sends phones to the buttons on the doorstep (lost), otherwise to the setup page
TO_BUTTONS = r"^http://[^/]+/$"
TO_SETUP = r"^http://[^/]+/setup$"
PORTAL = by_mode(lost=TO_BUTTONS, default=TO_SETUP)
# Where an address the pumpkin doesn't have goes: the portal on its own WiFi, otherwise a 404 page
NOT_HERE_STATUS = by_mode(home=404, off=404, default=302)
NOT_HERE_LOCATION = by_mode(home=None, off=None, lost=TO_BUTTONS, default=TO_SETUP)

# A WiFi check that would pass the request checks: skipped on home WiFi, needs BOOT on the doorstep. Not
# sent in setup mode, where it would really start a check (one at a time: see "wifi check: runs").
PASSES_CHECKS = dict(
    modes=("home", "lost", "wrong", "off"),
    status=by_mode(home=200, off=200, default=403),
    shape=by_mode(home=SKIPPED, off=SKIPPED, default=BOOT_ERROR),
    expect=by_mode(home={"skipped": True}, off={"skipped": True}, default={"needsBoot": True}),
    error=by_mode(home=None, off=None, default=BOOT_PRESS),
)


def wifi_check(name, ssid=TEST_SSID, password=TEST_PASSWORD, note="", **fields):
    body = {"ssid": ssid}
    if password is not ...:
        body["password"] = password
    return Case(f"wifi check: {name}", "POST", "/api/wifi/test", body, note=note,
                forbid=(password,) if isinstance(password, str) and len(password) >= MIN_PASSWORD else (),
                **fields)


def wifi_check_refused(name, body, error):
    """Refused by the request checks, the same in every mode (they come before the home-WiFi skip and the
    BOOT gate)"""
    return Case(f"wifi check: {name}", "POST", "/api/wifi/test", body, status=400, shape=ERROR, error=error)


def settings_case(name, body, status=200, error=None, expect=None, **fields):
    shape = SAVED if status == 200 else ERROR
    return Case(f"settings: {name}", "POST", "/api/settings", body, status=status, shape=shape, error=error,
                expect=expect, **fields)


def show_refused(name, blocks, error, **fields):
    return Case(f"show: {name}", "POST", "/api/show", {"blocks": blocks}, status=400, shape=ERROR, error=error,
                **fields)


def needs_header(path, body=None, **fields):
    return Case(f"no X-Pumpkin header: POST {path}", "POST", path, {} if body is None else body,
                headers={"X-Pumpkin": None}, status=403, shape=ERROR, error=NO_HEADER, **fields)


A_SHOW = [effect_block("creeper"), words_block("Hello from the API check")]
LONG_SHOW = [words_block(letters(MAX_WORDS, "Happy Halloween! ")) for _ in range(MAX_BLOCKS)]
GARBAGE_FILE = (
    b"--pumpkinboundary\r\n"
    b'Content-Disposition: form-data; name="firmware"; filename="not-firmware-ota.bin"\r\n'
    b"Content-Type: application/octet-stream\r\n\r\n" + b"This is not firmware, just words. " * 4 + b"\r\n"
    b"--pumpkinboundary--\r\n"
)
TINY_FILE = (
    b"--pumpkinboundary\r\n"
    b'Content-Disposition: form-data; name="firmware"; filename="tiny-ota.bin"\r\n'
    b"Content-Type: application/octet-stream\r\n\r\n" + b"\xe9tiny" + b"\r\n"
    b"--pumpkinboundary--\r\n"
)
MULTIPART = {"Content-Type": "multipart/form-data; boundary=pumpkinboundary"}


def home_wifi_body(ctx):
    """The WiFi to save for "wifi save": the board's own (--wifi-ssid/--wifi-password), so it comes back"""
    if not ctx.wifi_ssid:
        raise Skip("needs --wifi-ssid and --wifi-password (the board's own WiFi, so it comes back)")
    return {"ssid": ctx.wifi_ssid, "password": ctx.wifi_password}


def status_values(mode):
    common = {"setupSsid": SETUP_SSID, "defaultName": DEFAULT_NAME,
              "buttonsHeartbeatMs": FIRMWARE["kButtonsHeartbeatMs"]}
    values = {
        "home": {"mode": "connected", "hasSavedWifi": True, "homeWifiOutOfReach": False, "wifiNeedsBoot": False,
                 "ip": NON_EMPTY},
        "setup": {"mode": "setup", "hasSavedWifi": False, "homeWifiOutOfReach": False, "wifiNeedsBoot": False,
                  "setupReason": "", "ip": NON_EMPTY},
        "lost": {"mode": "setup", "hasSavedWifi": True, "homeWifiOutOfReach": True, "wifiNeedsBoot": True,
                 "setupReason": NON_EMPTY, "ip": NON_EMPTY},
        "wrong": {"mode": "setup", "hasSavedWifi": True, "homeWifiOutOfReach": False, "wifiNeedsBoot": True,
                  "setupReason": NON_EMPTY, "ip": NON_EMPTY},
        "off": {"mode": "off", "ip": ""},
    }[mode]
    return dict(common, **values)


# --- The cases, in the order they run ---

CASES = [
    # --- What the pages read ---
    Case("status", "GET", "/api/status", shape=STATUS, content_type="application/json",
         expect=ByMode({m: status_values(m) for m in MODES})),
    Case("effects", "GET", "/api/effects", shape={"effects": [{"id": str, "label": str, "emoji": str, "group": str,
                                                               "inShow": bool}], "groupTitles": dict},
         expect={"effects": Check(lambda v: [e["id"] for e in v] == EFFECT_IDS,
                                  f"the {len(EFFECT_IDS)} effects in registry.cpp, in order"),
                 "groupTitles": all_values(str)}),
    Case("settings", "GET", "/api/settings", shape=dict(SETTINGS, limits=LIMITS),
         expect={"limits.maxBrightness": MAX_BRIGHTNESS, "limits.maxEyeGap": MAX_EYE_GAP,
                 "limits.maxNameLength": MAX_NAME, "limits.maxWordsLength": MAX_WORDS,
                 "limits.maxSsidBytes": MAX_SSID, "limits.minWifiPassword": MIN_PASSWORD,
                 "limits.maxWifiPassword": MAX_PASSWORD,
                 "limits.wifiIdleOffMinutes": FIRMWARE["kWifiIdleOffMs"] // 60000}),
    Case("show", "GET", "/api/show", shape=SHOW,
         expect={"limits.maxBlocks": MAX_BLOCKS, "limits.maxWordsLength": MAX_WORDS}),
    Case("wifi check result", "GET", "/api/wifi/test", shape=WIFI_TEST,
         expect={"status": one_of("idle", "running", "passed", "failed")}),
    Case("wifi scan", "GET", "/api/wifi/scan", modes=("home", "setup", "lost", "wrong"),
         poll=Poll(lambda status, data: status != 202, timeout=25),
         shape={"networks": [{"ssid": str, "rssi": int, "secure": bool, "channel": int}]},
         note="202 {scanning} while it looks, then the networks"),
    Case("wifi scan: WiFi off", "GET", "/api/wifi/scan", modes=("off",), status=503, shape=ERROR,
         error="The pumpkin couldn't look for WiFi just now. Try again in a few seconds."),

    # --- Pages ---
    Case("page /", "GET", "/", status=by_mode(setup=302, default=200),
         content_type=by_mode(setup=None, default="text/html"), location=by_mode(setup="^/setup$", default=None),
         note="a pumpkin that's never been set up sends phones to /setup"),
    Case("page /?play=1", "GET", "/?play=1", content_type="text/html"),
    Case("page /setup", "GET", "/setup", content_type="text/html"),
    Case("page /settings", "GET", "/settings", content_type="text/html"),
    Case("page /show", "GET", "/show", content_type="text/html"),
    Case("page /common.js", "GET", "/common.js", content_type="application/javascript"),
    Case("page that isn't there", "GET", "/no-such-page", status=NOT_HERE_STATUS,
         content_type=by_mode(home="text/html", off="text/html", default=None), location=NOT_HERE_LOCATION),

    # --- Captive portal (the pumpkin's own WiFi) ---
    Case("sign-in check: Android", "GET", "/generate_204", status=NOT_HERE_STATUS, location=NOT_HERE_LOCATION),
    Case("sign-in check: Apple", "GET", "/hotspot-detect.html", status=NOT_HERE_STATUS,
         location=NOT_HERE_LOCATION),
    Case("another website's page", "GET", "/", headers={"Host": "example.com"}, modes=SETUP_WIFI, status=302,
         location=PORTAL),
    Case("another website's API call", "POST", "/api/play", {"id": PLAYABLE}, headers={"Host": "example.com"},
         modes=SETUP_WIFI, status=302, location=PORTAL, note="any method, not just GET"),
    Case("another website's name on home WiFi", "GET", "/api/status", headers={"Host": "example.com"},
         modes=NOT_SETUP_WIFI, shape=STATUS, note="only the setup WiFi redirects"),

    # --- Plumbing every POST goes through ---
    needs_header("/api/play", {"id": PLAYABLE}),
    Case("X-Pumpkin: 0", "POST", "/api/play", {"id": PLAYABLE}, headers={"X-Pumpkin": "0"}, status=403,
         shape=ERROR, error=NO_HEADER),
    needs_header("/api/settings", {"brightness": 1}),
    needs_header("/api/show", {"blocks": A_SHOW}),
    needs_header("/api/wifi/test", {"ssid": TEST_SSID, "password": TEST_PASSWORD}),
    needs_header("/api/wifi", note="empty body: refused even if the header check failed"),
    needs_header("/api/wifi/forget", board_needs=frozenset({FORGET})),
    needs_header("/api/restart", board_needs=frozenset({RESTART})),
    Case("no X-Pumpkin header: POST /api/update", "POST", "/api/update", GARBAGE_FILE,
         headers=dict(MULTIPART, **{"X-Pumpkin": None}), status=403, shape=ERROR, error=NO_HEADER),
    Case("no X-Pumpkin header and too big", "POST", "/api/play", padded_json({"id": PLAYABLE}, 2000),
         headers={"X-Pumpkin": None}, status=403, shape=ERROR, error=NO_HEADER, note="the header comes first"),
    Case("bad JSON", "POST", "/api/play", b'{"id": "blink"', status=400, shape=ERROR, error=BAD_JSON),
    Case("JSON that isn't an object", "POST", "/api/play", b'["blink"]', status=400, shape=ERROR, error=BAD_JSON),
    Case("JSON null", "POST", "/api/play", b"null", status=400, shape=ERROR, error=BAD_JSON),
    Case("a form, not JSON", "POST", "/api/play", b"id=blink",
         headers={"Content-Type": "application/x-www-form-urlencoded"}, status=400, shape=ERROR, error=NOT_JSON),
    Case("JSON without a Content-Type", "POST", "/api/play", encode_json({"id": PLAYABLE}),
         headers={"Content-Type": None}, shape=OK),
    Case("body of the largest size", "POST", "/api/play", padded_json({"id": PLAYABLE}, MAX_BODY), shape=OK,
         expect={"ok": True}, note=f"{MAX_BODY} bytes is allowed"),
    Case("body one byte too big", "POST", "/api/play", padded_json({"id": PLAYABLE}, MAX_BODY + 1), status=413,
         shape=ERROR, error=TOO_MUCH),
    Case("show one byte too big", "POST", "/api/show", padded_json({"blocks": A_SHOW}, MAX_SHOW_BODY + 1),
         status=413, shape=ERROR, error=TOO_MUCH),
    Case("empty body", "POST", "/api/play", b"", headers={"Content-Type": None}, status=400, shape=ERROR,
         error="Please fill in the effect", note="an empty body is an empty object"),
    Case("unknown API address", "GET", "/api/no-such-thing", status=404, shape=ERROR,
         content_type="application/json", error=no_route("GET", "/api/no-such-thing")),
    Case("unknown API address (POST)", "POST", "/api/no-such-thing", {}, status=404, shape=ERROR,
         error=no_route("POST", "/api/no-such-thing")),
    Case("unknown API address, no header", "POST", "/api/no-such-thing", {}, headers={"X-Pumpkin": None},
         status=404, shape=ERROR, error=no_route("POST", "/api/no-such-thing"),
         note="a 404 before the header check"),
    Case("below a POST address", "POST", "/api/show/extra", {"blocks": A_SHOW}, status=404, shape=ERROR,
         error=no_route("POST", "/api/show/extra"),
         note="the library would send it to /api/show: onJsonPost() turns it away"),
    Case("GET of a POST address", "GET", "/api/play", status=404, shape=ERROR, error=no_route("GET", "/api/play")),
    Case("PUT", "PUT", "/api/settings", {"brightness": 1}, status=404, shape=ERROR,
         error=no_route("PUT", "/api/settings")),
    Case("DELETE", "DELETE", "/api/show", status=404, shape=ERROR, error=no_route("DELETE", "/api/show")),
    Case("POST to a page", "POST", "/settings", {}, status=404, content_type="text/html",
         note="only GETs go to the captive portal"),

    # --- Buttons: POST /api/play ---
    Case("play", "POST", "/api/play", {"id": PLAYABLE}, shape=OK, expect={"ok": True}),
    Case("play: unknown effect", "POST", "/api/play", {"id": "no-such-effect"}, status=404, shape=ERROR,
         error="The pumpkin doesn't know an effect called no-such-effect"),
    Case("play: empty id", "POST", "/api/play", {"id": ""}, status=400, shape=ERROR,
         error="Please fill in the effect"),
    Case("play: no id", "POST", "/api/play", {}, status=400, shape=ERROR, error="Please fill in the effect"),
    Case("play: id a number", "POST", "/api/play", {"id": 5}, status=400, shape=ERROR,
         error="The effect should be text"),
    Case(f"play: {MAX_ID}-character id", "POST", "/api/play", {"id": letters(MAX_ID, "x")}, status=404,
         shape=ERROR, error="The pumpkin doesn't know an effect called " + letters(MAX_ID, "x")),
    Case(f"play: {MAX_ID + 1}-character id", "POST", "/api/play", {"id": letters(MAX_ID + 1, "x")}, status=400,
         shape=ERROR, error=too_long("effect", MAX_ID)),
    Case("play: NUL in the id", "POST", "/api/play", {"id": PLAYABLE + NUL + "x"}, status=400, shape=ERROR,
         error=strange("effect"), note="kept as a C string it would play blink"),
    Case("play: emoji id", "POST", "/api/play", {"id": EMOJI}, status=404, shape=ERROR,
         error="The pumpkin doesn't know an effect called " + EMOJI),

    # --- Your own words: POST /api/words ---
    Case("words", "POST", "/api/words", {"words": "Happy Halloween!"}, shape=OK),
    Case("words: symbols, phone punctuation, accents", "POST", "/api/words",
         {"words": '100% "spooky" \\o/ & <b> Cr\u00e8me \u201cbr\u00fbl\u00e9e\u201d \u2013 it\u2019s\u2026\ttab'},
         shape=OK),
    Case("words: only emoji", "POST", "/api/words", {"words": EMOJI + "\U0001F47B\U0001F987"}, status=400,
         shape=ERROR, error=WORDS_EMPTY),
    Case("words: only spaces", "POST", "/api/words", {"words": "    "}, status=400, shape=ERROR, error=WORDS_EMPTY),
    Case("words: empty", "POST", "/api/words", {"words": ""}, status=400, shape=ERROR,
         error="Please fill in the words"),
    Case("words: a number", "POST", "/api/words", {"words": 42}, status=400, shape=ERROR,
         error="The words should be text"),
    Case("words: NUL", "POST", "/api/words", {"words": "Hello" + NUL + "there"}, status=400, shape=ERROR,
         error=strange("words")),
    Case(f"words: {MAX_WORDS} letters", "POST", "/api/words", {"words": letters(MAX_WORDS)}, shape=OK),
    Case(f"words: {MAX_WORDS + 1} letters", "POST", "/api/words", {"words": letters(MAX_WORDS + 1)}, status=400,
         shape=ERROR, error=WORDS_TOO_LONG),
    Case(f"words: {MAX_WORDS} accented letters", "POST", "/api/words", {"words": E_ACUTE * MAX_WORDS}, shape=OK,
         note="400 bytes, 200 letters once tidied"),
    Case(f"words: {MAX_WORDS + 1} accented letters", "POST", "/api/words", {"words": E_ACUTE * (MAX_WORDS + 1)},
         status=400, shape=ERROR, error=WORDS_TOO_LONG),
    Case("words: Boo and emoji, just under the byte cap", "POST", "/api/words",
         {"words": "Boo" + EMOJI * ((MAX_WORDS_BYTES - 3) // 4)}, shape=OK,
         note=f"{3 + 4 * ((MAX_WORDS_BYTES - 3) // 4)} bytes: the emoji are dropped"),
    Case(f"words: {MAX_WORDS + 1} emoji and Boo", "POST", "/api/words", {"words": EMOJI * (MAX_WORDS + 1) + "Boo"},
         status=400, shape=ERROR, error=too_long("words", MAX_WORDS_BYTES),
         note=f"{4 * (MAX_WORDS + 1) + 3} bytes, over the {MAX_WORDS_BYTES}-byte cap before tidying"),
    Case("words: a byte that isn't UTF-8", "POST", "/api/words", b'{"words":"Hi \xff there"}', shape=OK,
         note="dropped by text::plain(), like an emoji"),

    # --- Show or buttons: POST /api/mode ---
    Case("mode: buttons", "POST", "/api/mode", {"mode": "buttons"}, shape={"ok": bool, "mode": str},
         expect={"mode": "buttons"}),
    Case("mode: show", "POST", "/api/mode", {"mode": "show"}, shape={"ok": bool, "mode": str},
         expect={"mode": "show"}),
    Case("mode: party", "POST", "/api/mode", {"mode": "party"}, status=400, shape=ERROR, error=MODE_WORDS),
    Case("mode: a number", "POST", "/api/mode", {"mode": 1}, status=400, shape=ERROR, error=MODE_WORDS),

    # --- Settings: POST /api/settings (a board's settings are put back afterwards) ---
    settings_case("nothing", {}, expect={"ok": True, "message": "Saved"}),
    settings_case("name", {"name": "Jack's Pumpkin"}, expect={"settings.name": "Jack's Pumpkin"}),
    settings_case("name with phone punctuation", {"name": "  Jack\u2019s \u201cGreat\u201d Pumpkin  "},
                  expect={"settings.name": 'Jack\'s "Great" Pumpkin'}),
    settings_case("name with accents", {"name": "Cr\u00e8me Br\u00fbl\u00e9e Se\u00f1or"},
                  expect={"settings.name": "Creme Brulee Senor"}),
    settings_case("name with symbols", {"name": '50% "Spooky" \\o/ & <3'},
                  expect={"settings.name": '50% "Spooky" \\o/ & <3'}),
    settings_case("name with a tab", {"name": "Tabby\tPumpkin"}, expect={"settings.name": "Tabby Pumpkin"}),
    settings_case("name with an emoji", {"name": "Jack " + EMOJI}, 400, NAME_LETTERS),
    settings_case("name with x", {"name": "Pumpkin \u00d7 2"}, 400, NAME_LETTERS,
                  note="x (U+00D7) has no plain letter"),
    settings_case("name with NUL", {"name": "Jack" + NUL + "s"}, 400, strange("pumpkin's name")),
    settings_case("name empty", {"name": ""}, 400, NAME_EMPTY),
    settings_case(f"name {MAX_NAME} letters", {"name": letters(MAX_NAME, "Pumpkin-")},
                  expect={"settings.name": letters(MAX_NAME, "Pumpkin-")}),
    settings_case(f"name {MAX_NAME + 1} letters", {"name": letters(MAX_NAME + 1, "Pumpkin-")}, 400, NAME_TOO_LONG),
    settings_case(f"name {MAX_NAME} letters and spaces round it", {"name": "  " + letters(MAX_NAME, "Pumpkin-") + "  "},
                  expect={"settings.name": letters(MAX_NAME, "Pumpkin-")}, note="trimmed before it's counted"),
    settings_case(f"name {MAX_NAME} accented letters", {"name": E_ACUTE * MAX_NAME},
                  expect={"settings.name": "e" * MAX_NAME}),
    settings_case(f"name {MAX_NAME + 1} accented letters", {"name": E_ACUTE * (MAX_NAME + 1)}, 400, NAME_TOO_LONG),
    settings_case("name nearly filling the body", {"name": letters(MAX_BODY - 20)}, 400, NAME_TOO_LONG),
    settings_case("name a number", {"name": 7}, 400, "The pumpkin's name should be text"),
    settings_case(f"brightness {MAX_BRIGHTNESS}", {"brightness": MAX_BRIGHTNESS},
                  expect={"settings.brightness": MAX_BRIGHTNESS}),
    settings_case(f"brightness {MAX_BRIGHTNESS + 1}", {"brightness": MAX_BRIGHTNESS + 1}, 400,
                  f"The brightness should be from 0 to {MAX_BRIGHTNESS}"),
    settings_case("brightness as text", {"brightness": "5"}, 400, "The brightness should be a whole number"),
    settings_case("brightness 2.5", {"brightness": 2.5}, 400, "The brightness should be a whole number"),
    settings_case("brightness 3 billion", {"brightness": 3000000000}, 400, "The brightness should be a whole number",
                  note="too big for an int"),
    settings_case(f"eye gap {MAX_EYE_GAP + 1}", {"eyeGap": MAX_EYE_GAP + 1}, 400,
                  f"The gap between the eyes should be from 0 to {MAX_EYE_GAP}"),
    settings_case("mouth LED as text", {"mouthLed": "yes"}, 400, "The mouth light setting should be on or off"),
    settings_case("save batteries as 1", {"wifiSaver": 1}, 400, "The save batteries setting should be on or off"),
    settings_case("everything at once", {"brightness": 1, "eyeGap": MAX_EYE_GAP, "mouthLed": False,
                                         "wifiSaver": False},
                  expect={"settings.brightness": 1, "settings.eyeGap": MAX_EYE_GAP, "settings.mouthLed": False,
                          "settings.wifiSaver": False}),

    # --- The show creator: /api/show (a board's show is put back afterwards) ---
    Case("show: save", "POST", "/api/show", {"blocks": A_SHOW}, shape=SHOW_SAVED, expect={"blocks": A_SHOW}),
    Case("show: read back", "GET", "/api/show", shape=SHOW, expect={"blocks": A_SHOW, "isDefault": False}),
    Case("show: words tidied", "POST", "/api/show",
         {"blocks": [words_block("  Cr\u00e8me \u201cbr\u00fbl\u00e9e\u201d \u2013 tr\u00e8s bien\u2026  ")]},
         shape=SHOW_SAVED, expect={"blocks.0.text": 'Creme "brulee" - tres bien...'}),
    Case(f"show: words cut to {MAX_WORDS}", "POST", "/api/show",
         {"blocks": [words_block(letters(MAX_WORDS + 50, "Boo!"))]},
         shape=SHOW_SAVED, expect={"blocks.0.text": length(MAX_WORDS)}),
    Case(f"show: {MAX_BLOCKS} blocks of {MAX_WORDS} letters", "POST", "/api/show", {"blocks": LONG_SHOW},
         shape=SHOW_SAVED, expect={"blocks": length(MAX_BLOCKS)},
         note=f"{len(encode_json({'blocks': LONG_SHOW}))} bytes: the biggest show the page can make"),
    show_refused(f"{MAX_BLOCKS + 1} blocks", [effect_block(PLAYABLE)] * (MAX_BLOCKS + 1),
                 f"That show is too long (up to {MAX_BLOCKS} blocks)"),
    show_refused("no blocks", [], "The show needs at least one block"),
    Case("show: no list", "POST", "/api/show", {}, status=400, shape=ERROR,
         error="The show should be a list of blocks"),
    show_refused("unknown effect", [effect_block("no-such-effect")], 'Block 1: no effect called "no-such-effect"'),
    show_refused("long unknown id", [effect_block(letters(40, "y"))],
                 f'Block 1: no effect called "{letters(source.LONGEST_ID_SHOWN, "y")}"'),
    show_refused("empty id", [effect_block("")], 'Block 1: no effect called ""'),
    show_refused("effect with no id", [{"type": "effect"}], "Block 1: which effect?"),
    show_refused("an effect that never ends", [effect_block(NEVER_ENDS["id"])],
                 f"Block 1: {NEVER_ENDS['label']} never ends, so it can't go in a show"),
    show_refused("NUL in an id", [effect_block("creeper" + NUL)],
                 "Block 1: there's a strange character in the effect's name"),
    show_refused("words a number", [effect_block(PLAYABLE), {"type": "words", "text": 12}],
                 "Block 2: the words should be text"),
    show_refused("only emoji", [words_block(EMOJI * 3)], "Block 1: no words to scroll (the eyes can't show emoji)"),
    show_refused("NUL in the words", [words_block("Hello" + NUL + "World")],
                 "Block 1: there's a strange character in the words"),
    show_refused("unknown type", [effect_block(PLAYABLE), {"type": "song", "id": PLAYABLE}],
                 "Block 2: should be an effect or some words"),
    show_refused("block not an object", ["creeper"], "Block 1: should be an effect or some words"),
    Case("show: reset", "POST", "/api/show/reset", {}, shape=SHOW_SAVED, expect={"blocks": DEFAULT_SHOW}),
    Case("show: back to the original", "GET", "/api/show", shape=SHOW,
         expect={"blocks": DEFAULT_SHOW, "isDefault": True}),

    # --- The setup page's WiFi check: POST /api/wifi/test ---
    wifi_check_refused("no WiFi name", {"password": TEST_PASSWORD}, "Please fill in the WiFi name"),
    wifi_check_refused("WiFi name a number", {"ssid": 24, "password": TEST_PASSWORD}, "The WiFi name should be text"),
    wifi_check_refused("NUL in the WiFi name", {"ssid": "Pumpkin" + NUL + "Net", "password": TEST_PASSWORD},
                       strange("WiFi name")),
    wifi_check_refused(f"WiFi name {MAX_SSID + 1} bytes", {"ssid": letters(MAX_SSID + 1, "Net"),
                                                           "password": TEST_PASSWORD},
                       too_long("WiFi name", MAX_SSID)),
    wifi_check_refused(f"WiFi name of 11 euro signs ({len(EURO.encode()) * 11} bytes)",
                       {"ssid": EURO * 11, "password": TEST_PASSWORD}, too_long("WiFi name", MAX_SSID)),
    wifi_check_refused(f"password {MIN_PASSWORD - 1} characters", {"ssid": TEST_SSID, "password": "1234567"},
                       PASSWORD_SHORT),
    wifi_check_refused(f"password {MAX_PASSWORD} characters, not hex",
                       {"ssid": TEST_SSID, "password": letters(MAX_PASSWORD, "spooky!")}, PASSWORD_LONG_HEX),
    wifi_check_refused(f"password {MAX_PASSWORD + 1} characters",
                       {"ssid": TEST_SSID, "password": letters(MAX_PASSWORD + 1, "spooky!")},
                       too_long("WiFi password", MAX_PASSWORD)),
    wifi_check_refused("password of 40 pound signs (80 bytes)", {"ssid": TEST_SSID, "password": POUND * 40},
                       too_long("WiFi password", MAX_PASSWORD)),
    wifi_check_refused("NUL in the password", {"ssid": TEST_SSID, "password": "spooky" + NUL + "eyes123"},
                       strange("WiFi password")),
    Case("wifi check: pumpkin name with emoji", "POST", "/api/wifi/test",
         {"ssid": TEST_SSID, "password": TEST_PASSWORD, "name": "Jack " + EMOJI}, status=400,
         shape={"error": str, "field": str}, expect={"field": "name"}, error=NAME_LETTERS,
         note="the name is checked before the wait, not after"),
    wifi_check(f"password {MIN_PASSWORD} characters", password="12345678", **PASSES_CHECKS),
    wifi_check(f"password {MAX_PASSWORD - 1} characters", password=letters(MAX_PASSWORD - 1, "spooky!"),
               **PASSES_CHECKS),
    wifi_check(f"password {MAX_PASSWORD} hex digits", password=HEX64, **PASSES_CHECKS),
    wifi_check("password with symbols", password='pa%"&\\' + POUND + "ss word", **PASSES_CHECKS),
    wifi_check("password of 4 pound signs (8 bytes)", password=POUND * 4, note="lengths are bytes",
               **PASSES_CHECKS),
    wifi_check(f"WiFi name {MAX_SSID} bytes of accents", ssid=E_ACUTE * (MAX_SSID // 2), **PASSES_CHECKS),
    wifi_check("WiFi name with emoji and spaces", ssid=" Caf\u00e9 \U0001F33B Test ", **PASSES_CHECKS),
    wifi_check("open WiFi (no password)", password=..., **PASSES_CHECKS),
    wifi_check("runs", password=TEST_PASSWORD, modes=("setup",), status=202, shape={"testing": bool},
               expect={"testing": True}, note="a real check of an invented WiFi: it should fail"),
    wifi_check("while one is running", password=TEST_PASSWORD, modes=("setup",), status=409, shape=ERROR,
               error="A check is already running", targets=("mock",),
               note="mock only: on a board the phone can drop off the setup WiFi while the radio checks"),
    Case("wifi check: result", "GET", "/api/wifi/test", modes=("setup",),
         poll=Poll(lambda status, data: status == 200 and data.get("status") != "running", timeout=45),
         shape=WIFI_TEST, expect={"status": "failed", "ssid": TEST_SSID, "reason": NON_EMPTY},
         forbid=(TEST_PASSWORD,), note="the invented WiFi isn't there"),

    # --- Saving the home WiFi: POST /api/wifi. Only refusals here: saving restarts the pumpkin. ---
    Case("wifi save: no WiFi name", "POST", "/api/wifi", {"password": TEST_PASSWORD}, status=400, shape=ERROR,
         error="Please fill in the WiFi name", forbid=(TEST_PASSWORD,)),
    Case(f"wifi save: WiFi name {MAX_SSID + 1} bytes", "POST", "/api/wifi",
         {"ssid": letters(MAX_SSID + 1, "Net"), "password": TEST_PASSWORD}, status=400, shape=ERROR,
         error=too_long("WiFi name", MAX_SSID)),
    Case(f"wifi save: password {MAX_PASSWORD + 1} characters", "POST", "/api/wifi",
         {"ssid": TEST_SSID, "password": letters(MAX_PASSWORD + 1, "spooky!")}, status=400, shape=ERROR,
         error=too_long("WiFi password", MAX_PASSWORD)),
    Case("wifi save: NUL in the password", "POST", "/api/wifi",
         {"ssid": TEST_SSID, "password": "spooky" + NUL + "eyes123"}, status=400, shape=ERROR,
         error=strange("WiFi password")),
    Case(f"wifi save: password {MIN_PASSWORD - 1} characters", "POST", "/api/wifi",
         {"ssid": TEST_SSID, "password": "1234567"}, board_needs=frozenset({WIFI_SAVE}),
         status=by_mode(lost=403, wrong=403, default=400), shape=by_mode(lost=BOOT_ERROR, wrong=BOOT_ERROR,
                                                                         default=ERROR),
         error=by_mode(lost=BOOT_PRESS, wrong=BOOT_PRESS, default=PASSWORD_SHORT),
         note="settings::save() turns it away (after the BOOT gate on the doorstep)"),
    Case("wifi save: name with an emoji", "POST", "/api/wifi",
         {"ssid": TEST_SSID, "password": TEST_PASSWORD, "name": "Boo " + EMOJI}, board_needs=frozenset({WIFI_SAVE}),
         status=by_mode(lost=403, wrong=403, default=400), shape=by_mode(lost=BOOT_ERROR, wrong=BOOT_ERROR,
                                                                         default=ERROR),
         error=by_mode(lost=BOOT_PRESS, wrong=BOOT_PRESS, default=NAME_LETTERS), forbid=(TEST_PASSWORD,)),
    Case("forget WiFi on the doorstep", "POST", "/api/wifi/forget", {}, modes=DOORSTEP, status=403,
         shape=BOOT_ERROR, expect={"needsBoot": True}, error=BOOT_PRESS, board_needs=frozenset({FORGET}),
         note="strangers on the setup WiFi can't make it forget"),

    # --- Firmware updates: POST /api/update (never a real one) ---
    Case("update without BOOT", "POST", "/api/update", GARBAGE_FILE, headers=MULTIPART, status=403,
         shape=BOOT_ERROR, expect={"needsBoot": True}, error=BOOT_PRESS),
    Case("update without BOOT or a file", "POST", "/api/update", b"", headers={"Content-Type": None}, status=403,
         shape=BOOT_ERROR, expect={"needsBoot": True}, error=BOOT_PRESS),

    # --- The talking pumpkin: server-sent events ---
    Case("events: status, then say", "GET", "/events", kind="sse", body={"id": SAY_EFFECT},
         expect={"say": BY_ID[SAY_EFFECT]["phrase"]},
         note=f"opens /events, expects a status event, plays {SAY_EFFECT} and expects its phrase"),

    # --- With the BOOT button pressed (run after everything above) ---
    Case("update: not firmware", "POST", "/api/update", GARBAGE_FILE, headers=MULTIPART, boot=True, status=400,
         shape=ERROR, error="That isn't a firmware file. Choose the file ending -ota.bin."),
    Case("update: too small", "POST", "/api/update", TINY_FILE, headers=MULTIPART, boot=True, status=400,
         shape=ERROR, error="That file is too small to be pumpkin firmware."),
    Case("update: not a form", "POST", "/api/update", b"\xe9 not a form", boot=True,
         headers={"Content-Type": "application/octet-stream"}, status=400, shape=ERROR,
         error="No firmware file arrived. Choose the file ending -ota.bin and try again."),
    wifi_check("on the doorstep with BOOT", password=TEST_PASSWORD, modes=DOORSTEP, boot=True, status=202,
               shape={"testing": bool}),
    Case("wifi check on the doorstep: result", "GET", "/api/wifi/test", modes=DOORSTEP, boot=True,
         poll=Poll(lambda status, data: status == 200 and data.get("status") != "running", timeout=45),
         shape=WIFI_TEST, expect={"status": "failed", "ssid": TEST_SSID}, forbid=(TEST_PASSWORD,)),

    # --- Opt-in: really restarting, saving WiFi, forgetting it (run last) ---
    Case("restart", "POST", "/api/restart", {}, needs=frozenset({RESTART}), shape={"ok": bool, "message": str},
         after="restart"),
    Case("wifi save", "POST", "/api/wifi", home_wifi_body, modes=("home", "setup"), needs=frozenset({WIFI_SAVE}),
         shape={"ok": bool, "ssid": str, "name": str, "address": str, "setupSsid": str, "message": str},
         after="restart",
         note="saves --wifi-ssid/--wifi-password on a board (use its own WiFi), a made-up one on the mock"),
    Case("forget WiFi", "POST", "/api/wifi/forget", {}, modes=SAVES_NOT_GATED, needs=frozenset({FORGET}),
         shape={"ok": bool, "setupSsid": str, "message": str}, expect={"setupSsid": SETUP_SSID}, after="restart",
         note="a board comes back on its own setup WiFi"),
]


def check_table():
    """Catch mistakes in the table itself"""
    names = [case.name for case in CASES]
    duplicates = {n for n in names if names.count(n) > 1}
    assert not duplicates, f"cases with the same name: {sorted(duplicates)}"
    for case in CASES:
        assert set(case.modes) <= set(MODES), f"{case.name}: unknown mode in {case.modes}"
        for mode in case.modes:
            for value in (case.status, case.shape, case.error, case.expect, case.location, case.content_type):
                for_mode(value, mode)  # every mode it runs in has a value


check_table()
