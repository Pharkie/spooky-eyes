#!/usr/bin/env python3
"""Build the release images for the ESP32-C3 SuperMini.

    python3 scripts/build_release.py

Writes to release/ (gitignored: attach the files to a GitHub release):
  spooky-pumpkin-eyes-<version>-full.bin  for a new board: bootloader + partitions + app, flashed at 0x0
  spooky-pumpkin-eyes-<version>-ota.bin   to update a running pumpkin from its Settings page

Flash a new board over USB, erasing it first (clears test WiFi details and old settings):
  esptool --chip esp32c3 write-flash -e 0x0 release/spooky-pumpkin-eyes-<version>-full.bin    (esptool v5)
  esptool.py --chip esp32c3 write_flash -e 0x0 release/spooky-pumpkin-eyes-<version>-full.bin (esptool v4)
or in Chrome with Espressif's web flasher (https://espressif.github.io/esptool-js/): Erase Flash, then
program the file at address 0x0.
"""

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENV = "supermini"


def firmware_version():
    text = (ROOT / "src/core/constants.h").read_text()
    match = re.search(r'#define FIRMWARE_VERSION "([^"]+)"', text)
    if not match:
        sys.exit("Can't find FIRMWARE_VERSION in src/core/constants.h")
    return match.group(1)


def run(cmd, capture=False):
    print("$", " ".join(str(c) for c in cmd))
    result = subprocess.run(cmd, cwd=ROOT, check=True, text=True, capture_output=capture)
    return result.stdout


def flash_layout():
    """What `pio run -t upload` would write, straight from PlatformIO: [(offset, file)] including the app"""
    metadata = json.loads(run(["pio", "project", "metadata", "-e", ENV, "--json-output"], capture=True))[ENV]
    images = [(image["offset"], Path(image["path"])) for image in metadata["extra"]["flash_images"]]
    app = Path(metadata["prog_path"]).with_name("firmware.bin")
    images.append((metadata["extra"]["application_offset"], app))
    for _, path in images:
        if not path.exists():
            sys.exit(f"Missing {path}")
    return images, app


def main():
    version = firmware_version()
    run(["pio", "run", "-e", ENV])
    images, app = flash_layout()

    out = ROOT / "release"
    out.mkdir(exist_ok=True)
    full = out / f"spooky-pumpkin-eyes-{version}-full.bin"
    ota = out / f"spooky-pumpkin-eyes-{version}-ota.bin"

    merge = ["pio", "pkg", "exec", "-p", "tool-esptoolpy", "--",
             "esptool.py", "--chip", "esp32c3", "merge_bin", "-o", str(full),
             "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "4MB"]
    for offset, path in images:
        merge += [offset, str(path)]
    run(merge)
    shutil.copyfile(app, ota)

    print()
    print(f"New board:  esptool --chip esp32c3 write-flash -e 0x0 {full.relative_to(ROOT)}")
    print(f"Update:     upload {ota.relative_to(ROOT)} on the pumpkin's Settings page")


if __name__ == "__main__":
    main()
