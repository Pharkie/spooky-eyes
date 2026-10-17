#!/usr/bin/env python3
"""Flash a board the way it's posted (erase, then the full image at 0x0) and check its first boot log.

    python3 tools/flash_check.py release/spooky-pumpkin-eyes-<version>-full.bin [--port /dev/cu.usbmodemX]
    python3 tools/flash_check.py --log-file boot.log [--version V]    check a saved log (no board)
    python3 tools/flash_check.py BIN --dry-run                        say what it would do, do nothing
    python3 tools/flash_check.py --self-test                          check the checker on made-up logs

Flashing: esptool --chip esp32c3 --port <port> --after no-reset write-flash -e 0x0 <bin>, then the serial
port is opened (DTR and RTS low), which usually starts the new firmware by itself (RTS is pulsed only if it
didn't), so the log is read from the very first line (the board's USB log keeps only ~256 bytes while nobody's reading). Within --timeout (45 s) the
log must show:
    [BOOT] Spooky Pumpkin Eyes <version>
    [SETTINGS] "<default name>", home WiFi not set up, ... (fresh settings: the erase worked)
    [SHOW] Storage ready ... no saved show
    [PLAYER] Eyes on, <N> effects (N from src/effects/registry.cpp)
    [WIFI] No home WiFi saved yet
    [WIFI] Setup WiFi "<kSetupApName>" is on
    [MDNS] Web address http://<kHostname>.local
    [WEB] Web server on port 80
and never a crash (Guru Meditation, Backtrace, abort()), a brownout, or a second start-up. Each check
prints PASS or FAIL; the exit status is 1 if any failed. --save-log keeps the log.

The version comes from --version, else the file name (spooky-pumpkin-eyes-<version>-full.bin), else
src/core/constants.h. Flashing needs esptool and pyserial (PlatformIO's Python has pyserial:
/Library/Frameworks/Python.framework/Versions/3.11/bin/python3); --log-file, --dry-run and --self-test
need neither.
"""

import argparse
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import mock_server as source  # noqa: E402  (its readers of the firmware's source)

FIRMWARE = source.FIRMWARE
SETTLE_SECONDS = 5  # keep reading this long after the last expected line, to catch a crash just after


def on_off(value):
    return "on" if value else "off"


def expected_lines(version):
    """(what, regex) for each line a freshly erased and flashed board must log"""
    effects = len(source.load_effects())
    settings = (f'[SETTINGS] "{FIRMWARE["kDefaultPumpkinName"]}", home WiFi not set up, '
                f'brightness {FIRMWARE["kDefaultBrightness"]}, eye gap {FIRMWARE["kDefaultEyeGap"]}, '
                f'mouth LED {on_off(FIRMWARE["kDefaultMouthLed"])}, WiFi saver {on_off(FIRMWARE["kDefaultWifiSaver"])}')
    return [
        (f"firmware {version} starts", r"\[BOOT\] Spooky Pumpkin Eyes " + re.escape(version) + r"(\s|$)"),
        ("fresh settings (the erase worked)", re.escape(settings)),
        ("show storage ready, no saved show", r"\[SHOW\] Storage ready\b.*no saved show"),
        (f"eyes on with {effects} effects", rf"\[PLAYER\] Eyes on, {effects} effects\b"),
        ("no home WiFi saved", r"\[WIFI\] No home WiFi saved yet"),
        (f'setup WiFi "{FIRMWARE["kSetupApName"]}" on', r'\[WIFI\] Setup WiFi "' + re.escape(FIRMWARE["kSetupApName"])
         + r'" is on'),
        (f"{FIRMWARE['kHostname']}.local on the setup WiFi",
         r"\[MDNS\] Web address http://" + re.escape(FIRMWARE["kHostname"]) + r"\.local"),
        (f"web server on port {FIRMWARE['kWebServerPort']}", rf"\[WEB\] Web server on port {FIRMWARE['kWebServerPort']}\b"),
    ]


BAD_LINES = [
    ("no crash (Guru Meditation)", r"Guru Meditation"),
    ("no backtrace", r"Backtrace"),
    ("no abort()", r"abort\(\)"),
    ("no brownout", r"(?i)brownout"),
]


def check_log(lines, version):
    """[(PASS/FAIL, what, detail)] for a boot log (a list of lines)"""
    results = []
    for what, pattern in expected_lines(version):
        found = next((line for line in lines if re.search(pattern, line)), None)
        results.append(("PASS" if found else "FAIL", what, found.strip() if found else "not in the log"))
    for what, pattern in BAD_LINES:
        found = next((line for line in lines if re.search(pattern, line)), None)
        results.append(("FAIL" if found else "PASS", what, found.strip() if found else ""))
    # The ROM prints "rst:0x.. (reason)" at every start, and the firmware its [BOOT] line: once each
    resets = [line for line in lines if re.search(r"\brst:0x", line)]
    banners = [line for line in lines if "[BOOT] Spooky Pumpkin Eyes" in line]
    once = len(resets) <= 1 and len(banners) <= 1
    detail = f"{len(resets)} rst: line(s), {len(banners)} [BOOT] line(s)"
    results.append(("PASS" if once else "FAIL", "started once (no restarts)", detail if not once else ""))
    return results


def all_found(lines, version):
    return all(any(re.search(p, line) for line in lines) for _, p in expected_lines(version))


def any_bad(lines):
    return any(re.search(p, line) for line in lines for _, p in BAD_LINES)


def print_results(results):
    for verdict, what, detail in results:
        print(f"  {verdict}  {what}" + (f"  ({detail})" if detail else ""))
    failed = sum(v == "FAIL" for v, _, _ in results)
    print(f"\n{len(results) - failed} passed, {failed} failed")
    return failed == 0


def version_for(bin_path, given):
    if given:
        return given
    if bin_path:
        match = re.match(r"spooky-pumpkin-eyes-(.+)-full\.bin$", Path(bin_path).name)
        if match:
            return match.group(1)
    return FIRMWARE["FIRMWARE_VERSION"]


def esptool_command(port, bin_path):
    """esptool v5 (esptool, write-flash) or v4 (esptool.py, write_flash), staying in the bootloader after"""
    exe = shutil.which("esptool") or shutil.which("esptool.py")
    if exe is None:
        raise SystemExit("esptool isn't installed (pip install esptool, or see README: Preparing pumpkins)")
    try:
        version = subprocess.run([exe, "version"], capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.TimeoutExpired):
        version = ""
    match = re.search(r"v?(\d+)\.\d+", version)
    v4 = match is not None and int(match.group(1)) < 5
    after, write = ("no_reset", "write_flash") if v4 else ("no-reset", "write-flash")
    return [exe, "--chip", "esp32c3", "--port", port, "--after", after, write, "-e", "0x0", str(bin_path)]


def flash_and_read(bin_path, port, version, timeout, save_log):
    import pumpkin_serial  # pyserial, only needed here

    port = port or pumpkin_serial.find_port()
    # One program at a time on the port: a forgotten logger once made esptool fail mid-write (boot loop)
    users = subprocess.run(["lsof", "-t", port], capture_output=True, text=True).stdout.split()
    if users:
        print(f"  FAIL  {port} is open in another program (PIDs {', '.join(users)}): stop it first")
        return False
    command = esptool_command(port, bin_path)
    print("$ " + " ".join(command), flush=True)
    if subprocess.run(command).returncode != 0:
        print("  FAIL  flashing (esptool failed: see above)")
        return False

    print(f"\nReading the first boot from {port} (up to {timeout:g} s)...", flush=True)
    with pumpkin_serial.PumpkinSerial(port, echo=True) as board:
        time.sleep(0.5)
        # Opening the port often starts the new firmware by itself (the C3's USB serial resets it). Only
        # if nothing has started, pulse RTS to leave the bootloader esptool left it in: a second reset
        # would cut the first boot short (while it formats the show's filesystem, say).
        if not any("rst:" in line.text or "[BOOT]" in line.text for line in board.lines):
            board.reset()
        deadline = time.monotonic() + timeout
        done_at = None
        while time.monotonic() < deadline:
            time.sleep(0.25)
            lines = [line.text for line in board.lines]
            if done_at is None and (all_found(lines, version) or any_bad(lines)):
                done_at = time.monotonic()
            if done_at is not None and time.monotonic() - done_at >= SETTLE_SECONDS:
                break
        if save_log:
            board.save(save_log)
            print(f"(log saved to {save_log})")
        lines = [line.text for line in board.lines]
    print()
    return print_results(check_log(lines, version))


# --- Checking the checker ---

def made_up_boot_log(version):
    """A log as a freshly erased board should print, built from the firmware's own log formats. Not a real
    capture: just enough for --self-test."""
    effects = len(source.load_effects())
    return [
        "ESP-ROM:esp32c3-api1-20210207",
        "rst:0x15 (USB_UART_CHIP_RESET),boot:0xd (SPI_FAST_FLASH_BOOT)",
        f"[BOOT] Spooky Pumpkin Eyes {version}",
        f'[SETTINGS] "{FIRMWARE["kDefaultPumpkinName"]}", home WiFi not set up, brightness '
        f'{FIRMWARE["kDefaultBrightness"]}, eye gap {FIRMWARE["kDefaultEyeGap"]}, mouth LED '
        f'{on_off(FIRMWARE["kDefaultMouthLed"])}, WiFi saver {on_off(FIRMWARE["kDefaultWifiSaver"])}',
        "[SHOW] Storage ready: 8192 of 118784 bytes used, no saved show (took 1450 ms)",
        f"[PLAYER] Eyes on, {effects} effects. Type help on serial for test commands.",
        "[WIFI] No home WiFi saved yet",
        f'[WIFI] Setup WiFi "{FIRMWARE["kSetupApName"]}" is on (password "{FIRMWARE["kSetupApPassword"]}"), '
        "page at http://192.168.4.1",
        f"[MDNS] Web address http://{FIRMWARE['kHostname']}.local",
        f"[WEB] Web server on port {FIRMWARE['kWebServerPort']} (6 files)",
        "[PLAYER] Show: 24 blocks (the original)",
    ]


def self_test():
    version = FIRMWARE["FIRMWARE_VERSION"]
    good = made_up_boot_log(version)
    trials = [
        ("a good first boot", good, True),
        ("a crash", good + ["Guru Meditation Error: Core  0 panic'ed (Load access fault)", "Backtrace: 0x4200"],
         False),
        ("a restart loop", good + good[1:3], False),
        ("a brownout", good[:3] + ["[BOOT] Restarted by a brownout: no WiFi until the batteries are changed"], False),
        ("old settings (not erased)", [line.replace("home WiFi not set up", 'home WiFi "Pumpkin Test Net"')
                                       for line in good], False),
        ("a saved show (not erased)", [line.replace("no saved show", "a show is saved") for line in good], False),
        ("another version", [line.replace(version, version + "-other") for line in good], False),
        ("no web server", [line for line in good if "[WEB]" not in line], False),
    ]
    ok = True
    for name, lines, should_pass in trials:
        results = check_log(lines, version)
        passed = all(v == "PASS" for v, _, _ in results)
        right = passed == should_pass
        ok &= right
        print(f"  {'PASS' if right else 'FAIL'}  {name}: the check {'passes' if passed else 'fails'}"
              + ("" if right else f" (it should {'pass' if should_pass else 'fail'})"))
    return ok


def main():
    parser = argparse.ArgumentParser(description="Flash a board (erase + full image) and check its first boot.",
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("bin", nargs="?", help="release/spooky-pumpkin-eyes-<version>-full.bin")
    parser.add_argument("--port", help="e.g. /dev/cu.usbmodem1101 (default: the only one there is)")
    parser.add_argument("--version", help="the firmware version the log should show")
    parser.add_argument("--timeout", type=float, default=45, help="seconds to wait for the boot log (default 45)")
    parser.add_argument("--log-file", help="check this saved log instead of flashing a board")
    parser.add_argument("--save-log", help="keep the boot log in this file")
    parser.add_argument("--dry-run", action="store_true", help="say what it would do; touch nothing")
    parser.add_argument("--self-test", action="store_true", help="check the checker on made-up logs")
    args = parser.parse_args()

    if args.self_test:
        return 0 if self_test() else 1
    version = version_for(args.bin, args.version)
    if args.dry_run:
        if args.bin:
            print("Would run:\n  $ esptool --chip esp32c3 --port " + (args.port or "<the /dev/cu.usbmodem* there is>")
                  + f" --after no-reset write-flash -e 0x0 {args.bin}")
            print("  then open the serial port (DTR and RTS low), pulse RTS, and read the log for up to "
                  f"{args.timeout:g} s, expecting version {version}")
        if not args.log_file:
            return 0
    if args.log_file:
        lines = Path(args.log_file).read_text(encoding="utf-8", errors="replace").splitlines()
        print(f"Checking {args.log_file} ({len(lines)} lines) for firmware {version}")
        return 0 if print_results(check_log(lines, version)) else 1
    if not args.bin:
        parser.error("give the -full.bin to flash, or --log-file, or --self-test")
    if not Path(args.bin).is_file():
        parser.error(f"{args.bin} isn't there (python3 scripts/build_release.py makes it)")
    try:
        return 0 if flash_and_read(args.bin, args.port, version, args.timeout, args.save_log) else 1
    except ImportError as e:
        sys.exit(f"Flashing needs pyserial ({e}). PlatformIO's Python has it: "
                 f"/Library/Frameworks/Python.framework/Versions/3.11/bin/python3 {' '.join(sys.argv)}")
    except Exception as e:  # pumpkin_serial.SerialError and the like: say it plainly
        if type(e).__name__ in ("SerialError", "SerialException"):
            sys.exit(str(e))
        raise


if __name__ == "__main__":
    sys.exit(main())
