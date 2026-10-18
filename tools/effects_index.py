#!/usr/bin/env python3
"""Draw every effect as an animated GIF and write docs/effects.md, the effects index.

    python3 tools/effects_index.py          # needs Pillow (pip install pillow) and a C++ compiler

Builds tools/record_effects.cpp against the portable effects code (no board needed), records each effect in
the table the way the tests do, at a new pumpkin's settings, and draws the recordings as the two eyes and
the mouth LED: docs/effects/<id>.gif. Run it again whenever an effect or the table changes.
"""
import pathlib
import random
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / ".pio" / "build" / "record_effects"
OUT_DIR = ROOT / "docs" / "effects"
INDEX = ROOT / "docs" / "effects.md"

MAX_MS = 12000           # the surprise show never ends: show this much of it
DOT, GAP, PAD, EYEGAP = 9, 2, 12, 22   # pixels
EYE = 8 * DOT + 7 * GAP
W = PAD * 2 + EYE * 2 + EYEGAP
H = PAD * 2 + EYE + 28   # room for the mouth LED below
BG, OFF, ON = (14, 11, 10), (38, 30, 26), (255, 88, 20)
MOUTH = [(255, 40, 40), (40, 220, 60), (60, 90, 255), (255, 200, 30), (200, 60, 255), (40, 220, 220)]


def record():
    """Build and run the recorder: (effects as dicts in table order, folder of recordings)."""
    BUILD.mkdir(parents=True, exist_ok=True)
    rec = BUILD / "rec"
    rec.mkdir(exist_ok=True)
    exe = BUILD / "record_effects"
    sources = [str(ROOT / "tools" / "record_effects.cpp"), str(ROOT / "src" / "core" / "text.cpp")]
    sources += [str(p) for p in sorted((ROOT / "src" / "display").glob("*.cpp"))]
    sources += [str(p) for p in sorted((ROOT / "src" / "effects").glob("*.cpp"))]
    subprocess.run(["c++", "-std=gnu++17", "-O1", "-I" + str(ROOT / "src"), "-DPUMPKIN_NATIVE", "-DPUBLIC_BUILD",
                    *sources, "-o", str(exe)], check=True)
    table = subprocess.run([str(exe), str(rec)], check=True, capture_output=True, text=True).stdout
    effects = []
    for line in table.splitlines():
        id_, label, emoji, group, title, phrase, surprise = line.split("\t")
        effects.append({"id": id_, "label": label, "emoji": emoji, "group": group, "title": title,
                        "phrase": phrase, "surprise": surprise == "1"})
    return effects, rec


def eye_rows(hexs):
    return [int(hexs[i:i + 2], 16) for i in range(0, 16, 2)]


def frame_image(left, right, bright, mouth):
    k = 0.35 + 0.65 * (bright / 15)
    on = tuple(int(c * k) for c in ON)
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    g = ImageDraw.Draw(glow)
    for ei, rows in enumerate((left, right)):
        x0 = PAD + ei * (EYE + EYEGAP)
        for r, byte in enumerate(rows):
            for c in range(8):
                if (byte >> (7 - c)) & 1:
                    x = x0 + c * (DOT + GAP)
                    y = PAD + r * (DOT + GAP)
                    g.ellipse([x - 2, y - 2, x + DOT + 2, y + DOT + 2], fill=on)
    glow = glow.filter(ImageFilter.GaussianBlur(5))
    img = Image.new("RGB", (W, H), BG)
    img.paste(Image.blend(img, glow, 0.55), (0, 0))
    d = ImageDraw.Draw(img)
    for ei, rows in enumerate((left, right)):
        x0 = PAD + ei * (EYE + EYEGAP)
        for r, byte in enumerate(rows):
            for c in range(8):
                lit = (byte >> (7 - c)) & 1
                x = x0 + c * (DOT + GAP)
                y = PAD + r * (DOT + GAP)
                d.ellipse([x, y, x + DOT, y + DOT], fill=on if lit else OFF)
    mx, my = W // 2, PAD + EYE + 15
    d.ellipse([mx - 5, my - 5, mx + 5, my + 5], fill=mouth)
    return img


def render(recording, out):
    """One recording (event lines) to an animated GIF. Returns (frames, seconds)."""
    rng = random.Random(7)
    shown = []            # (left, right, bright, mouth, ms): each picture and how long it stayed
    left = right = [0] * 8
    bright, mouth, total = 15, MOUTH[0], 0
    for line in recording.read_text().splitlines():
        kind, _, rest = line.partition(" ")
        if kind == "F":
            l, r = rest.split()
            left, right = eye_rows(l), eye_rows(r)
            shown.append([left, right, bright, mouth, 0])
        elif kind == "W":
            ms = int(rest)
            total += ms
            if not shown:
                shown.append([left, right, bright, mouth, 0])
            shown[-1][4] += ms
        elif kind == "B":
            bright = int(rest)
        elif kind == "M":
            mouth = rng.choice(MOUTH)
        if total > MAX_MS:
            break
    if not shown:
        shown.append([left, right, bright, mouth, 500])
    shown[-1][4] += 700   # a pause before it loops
    # GIF frames last at least 40 ms: pictures shown for less are dropped and their time carried on, so
    # scrolling keeps its real speed (fewer frames, not slower ones)
    frames, durations, carry = [], [], 0
    for i, (l, r, b, m, ms) in enumerate(shown):
        carry += ms
        if carry < 40 and i < len(shown) - 1:
            continue
        img = frame_image(l, r, b, m)
        if frames and img.tobytes() == frames[-1].tobytes():
            durations[-1] += carry
        else:
            frames.append(img)
            durations.append(max(40, carry))
        carry = 0
    durations = [min(d, 1500) for d in durations]
    pal = [f.convert("P", palette=Image.ADAPTIVE, colors=32) for f in frames]
    pal[0].save(out, save_all=True, append_images=pal[1:], duration=durations, loop=0, optimize=True, disposal=1)
    return len(pal), sum(durations) / 1000


def write_index(effects):
    groups = []
    for fx in effects:
        if not groups or groups[-1][0] != fx["title"]:
            groups.append((fx["title"], []))
        groups[-1][1].append(fx)
    not_surprise = [fx["label"] for fx in effects if not fx["surprise"]]
    lines = [
        "# The effects",
        "",
        "Every button on the pumpkin's page, drawn from a recording of the firmware at a new pumpkin's settings",
        "(brightness 2, eye gap 8, named \"Jack's Pumpkin\"). The talking pumpkin says the phrase as the effect",
        "starts. **Surprise** picks one at random, again and again, from all of these except "
        + " and ".join([", ".join(not_surprise[:-1]), not_surprise[-1]] if len(not_surprise) > 1 else not_surprise) + ".",
        "",
        "There's also a box on the page for scrolling your own words across the eyes.",
        "",
        "Made by `python3 tools/effects_index.py` from the table in [src/effects/registry.cpp](../src/effects/registry.cpp);",
        "run it again whenever an effect changes.",
        "",
    ]
    for title, members in groups:
        lines += [f"## {title}", "", "| | Effect | The pumpkin says |", "|---|---|---|"]
        for fx in members:
            phrase = f"\"{fx['phrase']}\"" if fx["phrase"] else ""
            lines.append(f"| <img src=\"effects/{fx['id']}.gif\" width=\"{W}\" alt=\"{fx['label']}\"> "
                         f"| {fx['emoji']} **{fx['label']}** | {phrase} |")
        lines.append("")
    INDEX.write_text("\n".join(lines))


def main():
    effects, rec = record()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for stale in OUT_DIR.glob("*.gif"):
        if stale.stem not in {fx["id"] for fx in effects}:
            stale.unlink()
    total = 0
    for fx in effects:
        out = OUT_DIR / f"{fx['id']}.gif"
        n, secs = render(rec / f"{fx['id']}.txt", out)
        size = out.stat().st_size
        total += size
        print(f"{fx['id']:<10} {n:4d} frames {secs:5.1f} s {size // 1024:4d} KB")
    write_index(effects)
    print(f"{len(effects)} effects, {total // 1024} KB of GIFs, {W}x{H}; wrote {INDEX.relative_to(ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
