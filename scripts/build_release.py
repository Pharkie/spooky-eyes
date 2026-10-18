#!/usr/bin/env python3
"""Build the release images for the ESP32-C3 SuperMini.

    python3 scripts/build_release.py

Writes to release/ (gitignored). A build in a checkout with local/credits.h includes that private credit:
keep those files private. `--public` builds without it (-DPUBLIC_BUILD), into release/public/, and checks
the images are clean: those are the ones to attach to a GitHub release.
  spooky-pumpkin-eyes-<version>-full.bin  for a new board: bootloader + partitions + app, flashed at 0x0
  spooky-pumpkin-eyes-<version>-ota.bin   to update a running pumpkin from its Settings page

Flash a new board over USB, erasing it first (clears test WiFi details and old settings):
  esptool --chip esp32c3 write-flash -e 0x0 release/spooky-pumpkin-eyes-<version>-full.bin    (esptool v5)
  esptool.py --chip esp32c3 write_flash -e 0x0 release/spooky-pumpkin-eyes-<version>-full.bin (esptool v4)
or in Chrome with Espressif's web flasher (https://espressif.github.io/esptool-js/): Erase Flash, then
program the file at address 0x0.
"""

import json
import os
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
    public = "--public" in sys.argv[1:]
    version = firmware_version()
    if public:
        # Its own build folder, so the flag doesn't make the next ordinary build start from scratch
        os.environ["PLATFORMIO_BUILD_FLAGS"] = "-DPUBLIC_BUILD"
        os.environ["PLATFORMIO_BUILD_DIR"] = os.environ.get("PLATFORMIO_BUILD_DIR", str(ROOT / ".pio" / "build")) + "-public"
    # The pages first: in a fresh checkout, creating src/generated/ during the build changes PlatformIO's
    # project checksum, and the metadata call below would then clean the build away
    run([sys.executable, "scripts/embed_web.py"])
    run(["pio", "run", "-e", ENV])
    images, app = flash_layout()

    out = ROOT / "release" / "public" if public else ROOT / "release"
    out.mkdir(parents=True, exist_ok=True)
    full = out / f"spooky-pumpkin-eyes-{version}-full.bin"
    ota = out / f"spooky-pumpkin-eyes-{version}-ota.bin"

    merge = ["pio", "pkg", "exec", "-p", "tool-esptoolpy", "--",
             "esptool.py", "--chip", "esp32c3", "merge_bin", "-o", str(full),
             "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "4MB"]
    for offset, path in images:
        merge += [offset, str(path)]
    run(merge)
    shutil.copyfile(app, ota)

    credited = b"Eyes Edition, by " in full.read_bytes()
    if public and credited:
        full.unlink(); ota.unlink()
        sys.exit("The public build still contains the private credit: not written")
    print()
    print("Includes local/credits.h:", "yes (keep these files private)" if credited else "no (fine to publish)")
    print(f"New board:  esptool --chip esp32c3 write-flash -e 0x0 {full.relative_to(ROOT)}")
    print(f"Update:     upload {ota.relative_to(ROOT)} on the pumpkin's Settings page")


if __name__ == "__main__":
    main()
