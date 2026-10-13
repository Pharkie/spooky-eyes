#!/usr/bin/env python3
"""Talk to a pumpkin over its USB cable: read its log, send serial commands, wait for lines.

    python3 tools/pumpkin_serial.py monitor [--reset]      print the log, timestamped (Ctrl-C to stop)
    python3 tools/pumpkin_serial.py log --reset --seconds 40 --out boot.log    save the boot log
    python3 tools/pumpkin_serial.py status                 the STATUS line, as JSON
    python3 tools/pumpkin_serial.py fs                     the FS line: storage used, whether a show is saved
    python3 tools/pumpkin_serial.py unlock                 the same as a short BOOT press
    python3 tools/pumpkin_serial.py send "play creeper"    any command, then print what comes back
    --port /dev/cu.usbmodemXXXX     which board (default: the only /dev/cu.usbmodem* there is)

Needs pyserial. PlatformIO's Python has it:
    /Library/Frameworks/Python.framework/Versions/3.11/bin/python3 tools/pumpkin_serial.py status

As a library (tools/flash_check.py and tools/api_check.py --serial use it):
    from pumpkin_serial import PumpkinSerial
    with PumpkinSerial(reset=True) as board:      # port opened with DTR and RTS low, then RTS pulsed
        board.expect(r"\\[WEB\\] Web server on port", timeout=45)
        print(board.status()["network"])

The port is opened with DTR and RTS held low, so opening it doesn't reset the board (or send it to the
bootloader); reset() pulses RTS. The board's USB log (HWCDC) only keeps about 256 bytes while nobody is
reading, so open the port before resetting and keep reading: a reader thread does, timestamping and
keeping every line. If the board drops off USB (a restart can do that), it's opened again when it's back.
Nothing here prints a WiFi password: the firmware never sends one.
"""

import argparse
import glob
import json
import re
import sys
import threading
import time

try:
    import serial
except ImportError:
    serial = None

BAUD = 115200
PORT_PATTERNS = ("/dev/cu.usbmodem*", "/dev/ttyACM*")  # macOS, Linux
PLATFORMIO_PYTHON = "/Library/Frameworks/Python.framework/Versions/3.11/bin/python3"


class SerialError(Exception):
    pass


def need_pyserial():
    if serial is None:
        raise SerialError("pyserial isn't installed for this Python. PlatformIO's Python has it: "
                          f"{PLATFORMIO_PYTHON} {' '.join(sys.argv)}")


def find_port():
    """The board's serial port: the only /dev/cu.usbmodem* (or /dev/ttyACM*) there is"""
    ports = sorted(p for pattern in PORT_PATTERNS for p in glob.glob(pattern))
    if not ports:
        raise SerialError("No board found (no /dev/cu.usbmodem*). Is it plugged in with a data cable?")
    if len(ports) > 1:
        raise SerialError(f"More than one board: {', '.join(ports)}. Say which with --port.")
    return ports[0]


class Line:
    def __init__(self, seconds, text):
        self.seconds = seconds  # since the port was opened
        self.text = text

    def __str__(self):
        return f"{self.seconds:8.3f}  {self.text}"


class PumpkinSerial:
    """The board's USB serial port, read continuously by a thread that keeps every line"""

    def __init__(self, port=None, reset=False, echo=False, baud=BAUD):
        need_pyserial()
        self.port = port or find_port()
        self.baud = baud
        self.echo = echo  # print each line as it arrives
        self.lines = []
        self.cursor = 0  # expect() looks at lines from here on
        self._partial = b""
        self._lock = threading.Condition()
        self._closing = False
        self._serial = None
        self._started = time.monotonic()
        self._open()
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()
        if reset:
            self.reset()

    # --- Opening and reading ---

    def _open(self):
        port = serial.serial_for_url(self.port, do_not_open=True)  # a device path, or a URL such as loop://
        port.baudrate = self.baud
        port.timeout = 0.1
        port.dtr = False  # held low from the start: opening the port mustn't reset the board
        port.rts = False
        port.open()
        self._serial = port

    def _reopen(self):
        """The board dropped off USB: open it again once it's back (or give up when closing)"""
        try:
            self._serial.close()
        except Exception:
            pass
        self._add_line("[pumpkin_serial] lost the port; waiting for it to come back")
        while not self._closing:
            time.sleep(0.2)
            try:
                self._open()
                self._add_line("[pumpkin_serial] port open again")
                return True
            except (serial.SerialException, OSError):
                continue
        return False

    def _read(self):
        while not self._closing:
            try:
                chunk = self._serial.read(self._serial.in_waiting or 1)
            except (serial.SerialException, OSError, TypeError, AttributeError):
                if self._closing or not self._reopen():
                    return
                continue
            if chunk:
                self._take(chunk)

    def _take(self, chunk):
        self._partial += chunk
        while b"\n" in self._partial:
            raw, self._partial = self._partial.split(b"\n", 1)
            self._add_line(raw.rstrip(b"\r").decode("utf-8", "replace"))

    def _add_line(self, text):
        line = Line(time.monotonic() - self._started, text)
        with self._lock:
            self.lines.append(line)
            self._lock.notify_all()
        if self.echo:
            print(line, flush=True)

    # --- Using it ---

    def reset(self):
        """Restart the board by pulsing RTS (with DTR low, so it starts the firmware, not the bootloader)"""
        self._serial.rts = True
        time.sleep(0.1)
        self._serial.rts = False

    def send(self, command):
        """Send a serial command. expect() then only looks at lines that come after it."""
        with self._lock:
            self.cursor = len(self.lines)
        self._serial.write((command + "\n").encode("utf-8"))
        self._serial.flush()

    def expect(self, pattern, timeout=5.0, since=None):
        """Wait for a line matching the regex (searched, not anchored), from the cursor on (or from line
        `since`: 0 = everything since the port was opened). Returns the match; its line is .string.
        Raises TimeoutError, quoting the last few lines."""
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout
        with self._lock:
            index = self.cursor if since is None else since
            while True:
                while index < len(self.lines):
                    match = regex.search(self.lines[index].text)
                    index += 1
                    if match:
                        self.cursor = index
                        return match
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    recent = "\n".join(str(line) for line in self.lines[-8:]) or "(nothing at all)"
                    raise TimeoutError(f"No line matching /{pattern}/ within {timeout:g} s. Last lines:\n{recent}")
                self._lock.wait(remaining)

    def wait(self, seconds):
        """Keep reading for this long"""
        time.sleep(seconds)

    def text(self):
        with self._lock:
            return "\n".join(line.text for line in self.lines)

    def save(self, path):
        """Write every line so far, timestamped, to a file"""
        with self._lock:
            lines = list(self.lines)
        with open(path, "w", encoding="utf-8") as f:
            for line in lines:
                f.write(f"{line}\n")

    def _json_reply(self, command, tag, timeout=3.0):
        self.send(command)
        match = self.expect(rf"^{tag} (\{{.*\}})\s*$", timeout)
        return json.loads(match.group(1))

    def status(self):
        """The serial "status" command: network mode, ip, hasSavedWifi, setupReason, player, memory..."""
        return self._json_reply("status", "STATUS")

    def fs(self):
        """The serial "fs" command: LittleFS used/total bytes and whether a show is saved"""
        return self._json_reply("fs", "FS")

    def unlock(self):
        """A short BOOT press (unlocks updates and WiFi changes for 5 minutes). Raises if it didn't happen."""
        self.send("unlock")
        match = self.expect(r"\[BUTTON\] Short press|isn't being watched|Unknown command", 3)
        if "Short press" not in match.string:
            raise SerialError(match.string)

    def close(self):
        self._closing = True
        self._thread.join(1)
        try:
            self._serial.close()
        except Exception:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def main():
    parser = argparse.ArgumentParser(description="Talk to a pumpkin over USB serial.",
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("command", choices=("monitor", "log", "status", "fs", "unlock", "send"))
    parser.add_argument("words", nargs="?", help="for send: the command to send, e.g. \"play creeper\"")
    parser.add_argument("--port", help="e.g. /dev/cu.usbmodem1101 (default: the only one there is)")
    parser.add_argument("--reset", action="store_true", help="restart the board first (pulse RTS)")
    parser.add_argument("--seconds", type=float, default=40, help="log: how long to read (default 40)")
    parser.add_argument("--out", help="log: save it to this file (timestamped)")
    parser.add_argument("--wait", type=float, default=2, help="send: how long to print replies (default 2 s)")
    args = parser.parse_args()

    try:
        echo = args.command in ("monitor", "log", "send")
        with PumpkinSerial(args.port, reset=args.reset, echo=echo) as board:
            print(f"({board.port})", file=sys.stderr)
            if args.command == "monitor":
                while True:
                    time.sleep(1)
            elif args.command == "log":
                board.wait(args.seconds)
                if args.out:
                    board.save(args.out)
                    print(f"({len(board.lines)} lines saved to {args.out})", file=sys.stderr)
            elif args.command == "status":
                print(json.dumps(board.status(), indent=2, ensure_ascii=False))
            elif args.command == "fs":
                print(json.dumps(board.fs(), indent=2))
            elif args.command == "unlock":
                board.unlock()
                print("BOOT pressed: updates and WiFi changes are unlocked for 5 minutes")
            elif args.command == "send":
                if not args.words:
                    parser.error('send needs the command, e.g. send "play creeper"')
                board.send(args.words)
                board.wait(args.wait)
    except (SerialError, TimeoutError) as e:
        sys.exit(str(e))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
