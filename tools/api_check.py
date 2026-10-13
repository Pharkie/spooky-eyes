#!/usr/bin/env python3
"""Check the pumpkin's web API against the cases in test/api/cases.py: on the mock server, a real board,
or both side by side.

    python3 tools/api_check.py --mock                       the mock, in every mode (home setup lost wrong off)
    python3 tools/api_check.py --mock --mode setup,lost     some modes
    python3 tools/api_check.py --board http://pumpkin.local the board on its home WiFi
    python3 tools/api_check.py --board http://192.168.4.1   a board on its own setup WiFi (join it first)
    python3 tools/api_check.py --board URL --compare        the board, then the mock in the board's mode,
                                                            side by side
    --only REGEX     just the cases whose names match          --list   list the cases and stop
    -q               only print problems                       -v       show the replies of failures
    --serial [PORT]  press BOOT with the serial "unlock" command, for the cases that need it (a board on USB;
                     needs pyserial: PlatformIO's Python has it)

Opt-in (never done otherwise): --allow-restart (POST /api/restart), --allow-forget (forget the home WiFi),
--allow-wifi-save (save WiFi details, which restarts; on a board give its own WiFi with --wifi-ssid and
--wifi-password, or PUMPKIN_WIFI_SSID / PUMPKIN_WIFI_PASSWORD, so it comes back). On a board these also let
through the few refusals where only settings::save() stands between the request and a real change.

A board's settings, saved show and show/buttons mode are read first and put back afterwards. Don't press
BOOT in the 5 minutes before a board run: the cases expect it not to be pressed (except with --serial).

The mode on a board comes from GET /api/status. Each mock run starts tools/mock_server.py on a free port.
Exits 1 if any reply has the wrong status, shape or values (a different error text is only a warning),
2 if the pumpkin can't be reached. Python standard library only (3.9+).
"""

import argparse
import http.client
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "test" / "api"))
sys.path.insert(0, str(ROOT / "tools"))
import cases as table  # noqa: E402

MOCK_SERVER = ROOT / "tools" / "mock_server.py"
FLAG_OPTIONS = {table.RESTART: "--allow-restart", table.WIFI_SAVE: "--allow-wifi-save", table.FORGET: "--allow-forget"}
NAME_WIDTH = 46


# --- Talking to a pumpkin ---

@dataclass
class Reply:
    status: int
    headers: Dict[str, str]
    raw: bytes
    data: Any = None  # the JSON, if it was JSON

    @property
    def error(self):
        return self.data.get("error") if isinstance(self.data, dict) else None


class Unreachable(Exception):
    pass


class Target:
    """A pumpkin at a base URL: the mock or a board"""

    def __init__(self, kind, url, label):
        self.kind = kind  # "mock" or "board"
        self.label = label
        parts = urlsplit(url if "://" in url else "http://" + url)
        if parts.scheme != "http":
            sys.exit(f"Only http:// addresses: {url}")
        self.host = parts.hostname
        self.port = parts.port or 80
        self.host_header = parts.netloc
        self.url = f"http://{parts.netloc}"
        self._address = None

    def address(self):
        """The IP address, looked up once (pumpkin.local is slow to look up every time)"""
        if self._address is None:
            try:
                self._address = socket.getaddrinfo(self.host, self.port, socket.AF_INET, socket.SOCK_STREAM)[0][4][0]
            except socket.gaierror as e:
                raise Unreachable(f"can't find {self.host} ({e.strerror})")
        return self._address

    def request(self, method, path, body=None, headers=None, timeout=10):
        """One HTTP request on a new connection, redirects not followed. headers: name -> value, or None to
        leave a default header out."""
        sent = {"Host": self.host_header}
        if body is not None:
            sent["Content-Type"] = "application/json"
        sent.update(headers or {})
        connection = http.client.HTTPConnection(self.address(), self.port, timeout=timeout)
        try:
            connection.putrequest(method, path, skip_host=True, skip_accept_encoding=True)
            for name, value in sent.items():
                if value is not None:
                    connection.putheader(name, value)
            if body is not None:
                connection.putheader("Content-Length", str(len(body)))
            connection.endheaders(body)
            response = connection.getresponse()
            raw = response.read()
            reply = Reply(response.status, {k.lower(): v for k, v in response.getheaders()}, raw)
        except (OSError, http.client.HTTPException) as e:
            raise Unreachable(f"{method} {path}: {e or type(e).__name__}")
        finally:
            connection.close()
        try:
            reply.data = json.loads(raw.decode("utf-8", "replace")) if raw else None
        except ValueError:
            reply.data = None
        return reply

    def get_json(self, path):
        reply = self.request("GET", path)
        if reply.status != 200 or not isinstance(reply.data, dict):
            raise Unreachable(f"GET {path} answered {reply.status}")
        return reply.data

    def post_json(self, path, data):
        return self.request("POST", path, table.encode_json(data), {"X-Pumpkin": "1"})


def detect_mode(status):
    """The mock's mode name for what /api/status says"""
    mode = status.get("mode")
    if mode == "connected":
        return "home"
    if mode == "off":
        return "off"
    if mode == "setup":
        if not status.get("hasSavedWifi"):
            return "setup"
        return "lost" if status.get("homeWifiOutOfReach") else "wrong"
    raise Unreachable(f'the pumpkin is still sorting out its WiFi (mode "{mode}"): try again in a moment')


# --- Checking a reply ---

def type_name(value):
    if value is None:
        return "null"
    return {bool: "true/false", int: "a whole number", float: "a number", str: "text", list: "a list",
            dict: "an object"}.get(type(value), type(value).__name__)


def shape_problems(value, shape, where="reply"):
    """What's wrong with value's shape (see cases.py), as a list of short sentences"""
    if isinstance(shape, tuple):
        if any(not shape_problems(value, option, where) for option in shape):
            return []
        return [f"{where} doesn't have any of the expected shapes"]
    if isinstance(shape, dict):
        if not isinstance(value, dict):
            return [f"{where} should be an object, not {type_name(value)}"]
        problems = []
        for key, inner in shape.items():
            if key not in value:
                problems.append(f'{where} has no "{key}"')
            else:
                problems += shape_problems(value[key], inner, f"{where}.{key}" if where != "reply" else key)
        return problems
    if isinstance(shape, list):
        if not isinstance(value, list):
            return [f"{where} should be a list, not {type_name(value)}"]
        problems = []
        if shape:
            for i, item in enumerate(value):
                problems += shape_problems(item, shape[0], f"{where}.{i}")
                if problems:
                    break  # one bad item says enough
        return problems
    if shape is int:
        ok = isinstance(value, int) and not isinstance(value, bool)
    elif shape is float:
        ok = isinstance(value, (int, float)) and not isinstance(value, bool)
    else:
        ok = isinstance(value, shape)
    return [] if ok else [f"{where} should be {type_name(shape())}, not {type_name(value)}"]


MISSING = object()


def value_at(data, path):
    for part in path.split("."):
        if isinstance(data, list) and part.isdigit() and int(part) < len(data):
            data = data[int(part)]
        elif isinstance(data, dict) and part in data:
            data = data[part]
        else:
            return MISSING
    return data


def short(value, limit=60):
    text = json.dumps(value, ensure_ascii=False) if not isinstance(value, table.Check) else repr(value)
    return text if len(text) <= limit else text[:limit - 3] + "..."


def strings_in(data):
    if isinstance(data, str):
        yield data
    elif isinstance(data, dict):
        for k, v in data.items():
            yield k
            yield from strings_in(v)
    elif isinstance(data, list):
        for v in data:
            yield from strings_in(v)


@dataclass
class Result:
    case: Any
    mode: str
    target: str
    status: Optional[int] = None
    problems: List[str] = field(default_factory=list)  # failures
    warnings: List[str] = field(default_factory=list)
    skipped: Optional[str] = None
    error_text: Optional[str] = None
    reply: Optional[Reply] = None

    @property
    def verdict(self):
        if self.skipped:
            return "SKIP"
        if self.problems:
            return "FAIL"
        return "WARN" if self.warnings else "PASS"


def check_reply(case, mode, reply, result):
    result.error_text = reply.error
    want_status = table.for_mode(case.status, mode)
    if reply.status != want_status:
        # The shape and values are for the expected status, so they'd only repeat this
        said = f' "{reply.error}"' if reply.error else ""
        result.problems.append(f"expected {want_status}" + (f", it said{said}" if said else ""))
        return
    shape = table.for_mode(case.shape, mode)
    if shape is not None:
        if reply.data is None:
            result.problems.append("the reply isn't JSON")
        else:
            result.problems += shape_problems(reply.data, shape)
    for path, want in (table.for_mode(case.expect, mode) or {}).items():
        got = value_at(reply.data, path)
        if got is MISSING:
            if shape is None or not result.problems:
                result.problems.append(f"no {path} in the reply")
        elif isinstance(want, table.Check):
            if not want(got):
                result.problems.append(f"{path} is {short(got)}, expected {want!r}")
        elif got != want:
            result.problems.append(f"{path} is {short(got)}, expected {short(want)}")
    content_type = table.for_mode(case.content_type, mode)
    if content_type and not reply.headers.get("content-type", "").startswith(content_type):
        result.problems.append(f"Content-Type {reply.headers.get('content-type')!r}, expected {content_type}")
    location = table.for_mode(case.location, mode)
    if location and not re.search(location, reply.headers.get("location", "")):
        result.problems.append(f"Location {reply.headers.get('location')!r}, expected /{location}/")
    for secret in case.forbid:
        if secret in reply.raw.decode("utf-8", "replace") or any(secret in s for s in strings_in(reply.data)):
            result.problems.append("the reply gives away the WiFi password")
    want_error = table.for_mode(case.error, mode)
    if want_error is not None and reply.error != want_error:
        result.warnings.append(f'said "{reply.error}", expected "{want_error}"')


# --- Running cases ---

@dataclass
class Context:
    """What a run knows besides the cases (cases' body functions get this)"""
    target_kind: str
    flags: frozenset
    wifi_ssid: str = ""
    wifi_password: str = ""
    serial: Optional[str] = None
    verbose: bool = False


def request_body(case, ctx):
    body = case.body(ctx) if callable(case.body) else case.body
    if body is None:
        return None if case.method == "GET" or case.method == "DELETE" else b""
    if isinstance(body, (dict, list)):
        return table.encode_json(body)
    return body


def headers_for(case):
    headers = {}
    if case.method != "GET":
        headers["X-Pumpkin"] = "1"
    headers.update(case.headers or {})
    return headers


def send_with_retries(target, method, path, body, headers, attempts=3, timeout=10):
    """A request, tried again if the pumpkin can't be reached (a phone on the setup WiFi drops off for a
    few seconds while the pumpkin checks a WiFi)"""
    for attempt in range(attempts):
        try:
            return target.request(method, path, body, headers, timeout=timeout)
        except Unreachable:
            if attempt == attempts - 1:
                raise
            time.sleep(2)


def run_http(target, case, mode, ctx, result):
    body = request_body(case, ctx)
    headers = headers_for(case)
    if case.poll is None:
        reply = send_with_retries(target, case.method, case.path, body, headers)
    else:
        deadline = time.time() + case.poll.timeout
        reply = None
        while True:
            try:
                reply = target.request(case.method, case.path, body, headers, timeout=5)
                if case.poll.until(reply.status, reply.data):
                    break
            except Unreachable:
                pass  # a dropped connection while it checks: ask again
            if time.time() > deadline:
                if reply is None:
                    raise Unreachable(f"no answer in {case.poll.timeout:g} s")
                result.problems.append(f"still not done after {case.poll.timeout:g} s")
                break
            time.sleep(case.poll.interval)
    result.reply = reply
    result.status = reply.status
    check_reply(case, mode, reply, result)


def read_events(sock, deadline):
    """Server-sent events from an open /events response: yields (event, data) until the deadline"""
    buffer = b""
    event, data = "message", []
    while time.time() < deadline:
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            line = line.rstrip(b"\r").decode("utf-8", "replace")
            if line == "":
                if data:
                    yield event, "\n".join(data)
                event, data = "message", []
            elif line.startswith("event:"):
                event = line[6:].strip()
            elif line.startswith("data:"):
                data.append(line[5:][1:] if line[5:].startswith(" ") else line[5:])
        sock.settimeout(max(0.1, min(1.0, deadline - time.time())))
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            return
        buffer += chunk


def run_sse(target, case, mode, ctx, result):
    """Open /events, expect a status event, play an effect, expect it to say its phrase"""
    phrase = case.expect["say"]
    try:
        sock = socket.create_connection((target.address(), target.port), timeout=10)
    except OSError as e:
        raise Unreachable(f"/events: {e}")
    try:
        sock.sendall(f"GET {case.path} HTTP/1.1\r\nHost: {target.host_header}\r\nAccept: text/event-stream\r\n"
                     "Cache-Control: no-cache\r\n\r\n".encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = sock.recv(4096)
            if not chunk:
                raise Unreachable("/events closed before its headers")
            head += chunk
        head, rest = head.split(b"\r\n\r\n", 1)
        lines = head.decode("latin-1").split("\r\n")
        result.status = int(lines[0].split()[1])
        headers = {k.strip().lower(): v.strip() for k, _, v in (h.partition(":") for h in lines[1:])}
        if result.status != 200:
            result.problems.append(f"status {result.status}, expected 200")
            return
        if not headers.get("content-type", "").startswith("text/event-stream"):
            result.problems.append(f"Content-Type {headers.get('content-type')!r}, expected text/event-stream")

        # Put back what came with the headers, then read events
        class Prefixed:
            def __init__(self, sock, first):
                self.sock, self.first = sock, first

            def recv(self, n):
                if self.first:
                    data, self.first = self.first, b""
                    return data
                return self.sock.recv(n)

            def settimeout(self, t):
                self.sock.settimeout(t)

        events = read_events(Prefixed(sock, rest), time.time() + 15)
        first = next((e for e in events if e[0] != "ping"), None)
        if first is None:
            result.problems.append("no event within 15 s of connecting")
            return
        if first[0] != "status":
            result.problems.append(f'the first event is "{first[0]}", expected "status"')
        else:
            try:
                result.problems += shape_problems(json.loads(first[1]), table.PLAYER, "status event")
            except ValueError:
                result.problems.append("the status event isn't JSON")

        play = target.request("POST", "/api/play", table.encode_json(case.body), {"X-Pumpkin": "1"})
        if play.status != 200:
            result.problems.append(f"playing {case.body['id']} answered {play.status}")
            return
        said = []
        for name, data in events:
            if name == "say":
                said.append(data)
                if data == phrase:
                    break
        else:
            result.problems.append(f'no "say" event "{phrase}" within 15 s' +
                                   (f" (said {said})" if said else ""))
    except OSError as e:
        raise Unreachable(f"/events: {e}")
    finally:
        sock.close()


def run_case(target, case, mode, ctx):
    result = Result(case, mode, target.kind)
    try:
        if case.kind == "sse":
            run_sse(target, case, mode, ctx, result)
        else:
            run_http(target, case, mode, ctx, result)
    except table.Skip as e:
        result.skipped = str(e)
    except Unreachable as e:
        result.problems.append(f"no answer: {e}")
    return result


def skip_reason(case, mode, ctx):
    """Why a case doesn't run here, or None"""
    if ctx.target_kind not in case.targets:
        return f"{' and '.join(case.targets)} only"
    missing = set(case.needs) | (set(case.board_needs) if ctx.target_kind == "board" else set())
    missing -= ctx.flags
    if missing:
        return "needs " + " ".join(sorted(FLAG_OPTIONS[f] for f in missing))
    if case.boot and ctx.target_kind == "board" and not ctx.serial:
        return "needs --serial (presses BOOT)"
    return None


def plan(mode, ctx, only):
    """The cases for this mode, in running order: ordinary, then BOOT ones, then opt-in ones"""
    chosen = [c for c in table.CASES if mode in c.modes and (not only or re.search(only, c.name))]
    ordinary = [c for c in chosen if not c.boot and not c.needs]
    boot = [c for c in chosen if c.boot and not c.needs]
    risky = [c for c in chosen if c.needs]
    return ordinary + boot + risky


# --- Putting a board back as it was ---

def snapshot(target):
    settings = target.get_json("/api/settings")
    show = target.get_json("/api/show")
    status = target.get_json("/api/status")
    return {
        "settings": {k: settings[k] for k in ("name", "brightness", "mouthLed", "eyeGap", "wifiSaver")},
        "show": show["blocks"], "isDefault": show["isDefault"], "mode": status["player"]["mode"],
    }


def restore(target, saved):
    """Put the settings, show and player mode back. Returns a list of what went wrong."""
    problems = []
    try:
        reply = target.post_json("/api/settings", saved["settings"])
        if reply.status != 200:
            problems.append(f"settings: {reply.status} {reply.error}")
        if saved["isDefault"]:
            reply = target.post_json("/api/show/reset", {})
        else:
            reply = target.post_json("/api/show", {"blocks": saved["show"]})
        if reply.status != 200:
            problems.append(f"show: {reply.status} {reply.error}")
        target.post_json("/api/mode", {"mode": saved["mode"]})
        after = snapshot(target)
        if after["settings"] != saved["settings"]:
            problems.append(f"settings are {after['settings']}, were {saved['settings']}")
        if after["show"] != saved["show"] or after["isDefault"] != saved["isDefault"]:
            problems.append("the show isn't the same as before")
    except Unreachable as e:
        problems.append(str(e))
    return problems


def wait_for_return(target, uptime_before, wait=90):
    """After a restart: wait for the pumpkin to go away (or its uptime to go down) and answer again.
    Returns its status, or None."""
    deadline = time.time() + wait
    went_away = False
    time.sleep(1)
    while time.time() < deadline:
        try:
            reply = target.request("GET", "/api/status", timeout=3)
            if reply.status == 200 and isinstance(reply.data, dict):
                if went_away or reply.data.get("uptime", 1e9) < uptime_before:
                    return reply.data
            else:
                went_away = True  # the mock says 503 while it's "restarting"
        except Unreachable:
            went_away = True
        time.sleep(1)
    return None


def press_boot(target, ctx):
    """Unlock the BOOT-gated actions: the mock's /mock/boot, or a board's serial "unlock" command"""
    if target.kind == "mock":
        target.request("POST", "/mock/boot", b"{}", {"X-Pumpkin": "1"})
        return
    try:
        import pumpkin_serial
    except ImportError as e:
        raise Unreachable(f"--serial needs pyserial ({e}): run this with PlatformIO's Python")
    with pumpkin_serial.PumpkinSerial(None if ctx.serial == "auto" else ctx.serial) as board:
        board.send("unlock")
        board.expect(r"\[BUTTON\] Short press", timeout=3)


def release_boot(target):
    if target.kind == "mock":
        target.request("GET", "/api/status?boot=0")


# --- Output ---

def describe(result):
    case = result.case
    request = f"{case.method} {case.path}"
    if result.skipped:
        return result.skipped
    status = "" if result.status is None else f" {result.status}"
    details = "; ".join(result.problems + result.warnings)
    return f"{request}{status}" + (f"  {details}" if details else "")


def print_result(result, quiet, verbose):
    if quiet and result.verdict in ("PASS", "SKIP"):
        return
    name = result.case.name
    name = name if len(name) <= NAME_WIDTH else name[:NAME_WIDTH - 3] + "..."
    print(f"  {result.verdict}  {name:<{NAME_WIDTH}}  {describe(result)}", flush=True)
    if verbose and result.problems and result.reply is not None:
        print(f"        reply: {result.reply.raw[:400].decode('utf-8', 'replace')}")


def run_target(target, mode, ctx, args, show_lines=True):
    """Every case for this mode on one pumpkin. Returns the results."""
    cases = plan(mode, ctx, args.only)
    saved = snapshot(target)
    results = []
    boot_pressed = False
    try:
        for case in cases:
            reason = skip_reason(case, mode, ctx)
            if reason:
                result = Result(case, mode, target.kind, skipped=reason)
            else:
                if case.boot and not boot_pressed:
                    press_boot(target, ctx)
                    boot_pressed = True
                if case.needs and boot_pressed:
                    release_boot(target)
                    boot_pressed = False
                uptime = target.get_json("/api/status").get("uptime", 0) if case.after else 0
                result = run_case(target, case, mode, ctx)
                if case.after == "restart" and result.verdict != "FAIL":
                    if wait_for_return(target, uptime) is None:
                        result.warnings.append("didn't come back at this address within 90 s"
                                               + (" (a board that forgot its WiFi is on its setup WiFi)"
                                                  if case.needs == {table.FORGET} else ""))
            results.append(result)
            if show_lines:
                print_result(result, args.quiet, args.verbose)
    finally:
        if boot_pressed:
            release_boot(target)
        problems = restore(target, saved)
        if problems:
            print(f"  WARN  couldn't put everything back: {'; '.join(problems)}")
        elif show_lines and not args.quiet:
            print("        (settings, show and player mode put back as they were)")
    return results


def summary_line(label, results):
    counts = {v: sum(r.verdict == v for r in results) for v in ("PASS", "WARN", "FAIL", "SKIP")}
    return (f"{label}: {counts['PASS'] + counts['WARN']} passed ({counts['WARN']} with warnings), "
            f"{counts['FAIL']} failed, {counts['SKIP']} skipped")


# --- The mock server ---

class MockServer:
    """tools/mock_server.py in one mode, on a free port"""

    def __init__(self, mode, script=MOCK_SERVER):
        self.mode = mode
        self.output = []
        env = {k: v for k, v in os.environ.items() if k not in ("MOCK_WIFI_DROP_SECONDS", "MOCK_IDLE_SECONDS",
                                                                "PUMPKIN_MODE", "PORT")}
        self.process = subprocess.Popen(
            [sys.executable, "-u", str(script), "--port", "0", "--mode", mode],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", env=env)
        port = None
        deadline = time.time() + 15
        for line in self.process.stdout:
            self.output.append(line.rstrip())
            match = re.search(r"on http://localhost:(\d+)", line)
            if match:
                port = int(match.group(1))
                break
            if time.time() > deadline:
                break
        if port is None:
            self.stop()
            sys.exit("The mock server didn't start:\n" + "\n".join(self.output[-20:]))
        # Keep reading its output, so its pipe never fills up
        threading.Thread(target=self._drain, daemon=True).start()
        self.url = f"http://127.0.0.1:{port}"

    def _drain(self):
        for line in self.process.stdout:
            self.output.append(line.rstrip())

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(5)
            except subprocess.TimeoutExpired:
                self.process.kill()


def run_mock(mode, ctx, args, show_lines=True):
    server = MockServer(mode, args.mock_server)
    try:
        target = Target("mock", server.url, f"mock ({mode})")
        seen = detect_mode(target.get_json("/api/status"))
        if seen != mode:
            sys.exit(f"The mock started in {mode} mode but says it's in {seen} mode")
        if show_lines:
            print(f"\nMock pumpkin, {mode} mode ({server.url})")
        return run_target(target, mode, ctx, args, show_lines)
    finally:
        server.stop()


# --- Side by side ---

def compare(board_results, mock_results, quiet):
    """Print board and mock replies case by case. Returns how many differ in status or pass/fail."""
    mock_by_name = {r.case.name: r for r in mock_results}
    differences = 0
    print(f"\n  {'':4}  {'case':<{NAME_WIDTH}}  {'board':<22}  mock")
    for board in board_results:
        mock = mock_by_name.get(board.case.name)
        if mock is None:
            continue

        def cell(r):
            if r.skipped:
                return "skipped"
            return f"{r.verdict} {r.status if r.status is not None else '-'}"

        same_status = board.skipped or mock.skipped or board.status == mock.status
        same_verdict = board.skipped or mock.skipped or (board.verdict == "FAIL") == (mock.verdict == "FAIL")
        mark = "SAME" if same_status and same_verdict else "DIFF"
        texts_differ = (not board.skipped and not mock.skipped and board.error_text != mock.error_text)
        if mark == "DIFF":
            differences += 1
        elif texts_differ:
            mark = "TEXT"
        if quiet and mark == "SAME":
            continue
        name = board.case.name if len(board.case.name) <= NAME_WIDTH else board.case.name[:NAME_WIDTH - 3] + "..."
        print(f"  {mark}  {name:<{NAME_WIDTH}}  {cell(board):<22}  {cell(mock)}")
        if mark != "SAME" and (board.error_text or mock.error_text):
            print(f"        board: {board.error_text!r}\n        mock:  {mock.error_text!r}")
    if quiet and differences == 0:
        print("  (the board and the mock agree on every case they both ran)")
    return differences


# --- Main ---

def list_cases():
    for case in table.CASES:
        status = case.status if isinstance(case.status, int) else "by mode"
        extra = []
        if case.boot:
            extra.append("BOOT")
        extra += [FLAG_OPTIONS[f] for f in sorted(case.needs)]
        extra += [FLAG_OPTIONS[f] + " on a board" for f in sorted(case.board_needs)]
        if case.targets != ("mock", "board"):
            extra.append("/".join(case.targets) + " only")
        modes = "all" if set(case.modes) == set(table.MODES) else ",".join(case.modes)
        print(f"{case.name:<{NAME_WIDTH}}  {case.method:<6} {case.path:<20} {status!s:<8} {modes:<24} "
              f"{' '.join(extra)}")
    print(f"\n{len(table.CASES)} cases")


def main():
    parser = argparse.ArgumentParser(description="Check the pumpkin's API against test/api/cases.py.",
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    where = parser.add_mutually_exclusive_group()
    where.add_argument("--mock", action="store_true", help="the mock server (the default)")
    where.add_argument("--board", metavar="URL", help="a board, e.g. http://pumpkin.local or http://192.168.4.1")
    parser.add_argument("--compare", action="store_true", help="with --board: the mock too, side by side")
    parser.add_argument("--mode", help="mock modes, comma-separated (default: all): " + ",".join(table.MODES))
    parser.add_argument("--only", metavar="REGEX", help="only the cases whose names match")
    parser.add_argument("--list", action="store_true", help="list the cases and stop")
    parser.add_argument("-q", "--quiet", action="store_true", help="only print problems and the summary")
    parser.add_argument("-v", "--verbose", action="store_true", help="show the replies of failed cases")
    parser.add_argument("--allow-restart", action="store_true", help="POST /api/restart")
    parser.add_argument("--allow-wifi-save", action="store_true", help="save WiFi details (restarts)")
    parser.add_argument("--allow-forget", action="store_true", help="forget the home WiFi (restarts)")
    parser.add_argument("--wifi-ssid", default=os.environ.get("PUMPKIN_WIFI_SSID", ""),
                        help="with --allow-wifi-save on a board: its own WiFi, so it comes back")
    parser.add_argument("--wifi-password", default=os.environ.get("PUMPKIN_WIFI_PASSWORD", ""))
    parser.add_argument("--serial", nargs="?", const="auto", metavar="PORT",
                        help="press BOOT over USB serial for the cases that need it (board only)")
    parser.add_argument("--mock-server", type=Path, default=MOCK_SERVER, metavar="SCRIPT",
                        help="another copy of mock_server.py (an older one, say: does the check catch it?). "
                             "It needs --port 0 to print the port it got.")
    args = parser.parse_args()

    if args.list:
        list_cases()
        return 0
    if args.compare and not args.board:
        parser.error("--compare needs --board URL")

    flags = frozenset(f for f, on in ((table.RESTART, args.allow_restart), (table.WIFI_SAVE, args.allow_wifi_save),
                                      (table.FORGET, args.allow_forget)) if on)
    all_results = []
    failed = False
    try:
        if args.board:
            target = Target("board", args.board, "board")
            status = target.get_json("/api/status")
            mode = detect_mode(status)
            ctx = Context("board", flags, args.wifi_ssid, args.wifi_password, args.serial, args.verbose)
            print(f'Board at {target.url}: "{status.get("name")}", firmware {status.get("version")}, {mode} mode'
                  f' (WiFi "{status.get("mode")}"), up {status.get("uptime")} s')
            board_results = run_target(target, mode, ctx, args, show_lines=not args.compare)
            all_results += board_results
            if args.compare:
                mock_ctx = Context("mock", flags, table.source.FAKE_NETWORKS[0]["ssid"], "spooky-test-pass",
                                   None, args.verbose)
                mock_results = run_mock(mode, mock_ctx, args, show_lines=False)
                all_results += mock_results
                if compare(board_results, mock_results, args.quiet):
                    failed = True
            print("\n" + summary_line(f"board ({mode})", board_results))
            if args.compare:
                print(summary_line(f"mock ({mode})", mock_results))
        else:
            modes = args.mode.split(",") if args.mode else list(table.MODES)
            unknown = set(modes) - set(table.MODES)
            if unknown:
                parser.error(f"unknown mode(s): {', '.join(sorted(unknown))}")
            ctx = Context("mock", flags, table.source.FAKE_NETWORKS[0]["ssid"], "spooky-test-pass", None,
                          args.verbose)
            summaries = []
            for mode in modes:
                results = run_mock(mode, ctx, args)
                all_results += results
                summaries.append(summary_line(f"mock {mode}", results))
            print("\n" + "\n".join(summaries))
    except Unreachable as e:
        print(f"\nCan't reach the pumpkin: {e}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nStopped.", file=sys.stderr)
        return 1

    failures = [r for r in all_results if r.verdict == "FAIL"]
    if failures or failed:
        print(f"\n{len(failures)} case(s) failed" + (" and the board and mock differ" if failed else ""))
        return 1
    print("\nAll good.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
