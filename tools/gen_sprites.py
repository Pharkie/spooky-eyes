#!/usr/bin/env python3
"""One-off generator: turns the MicroPython pictures, fonts and animations into C++.

    python3 tools/gen_sprites.py

Reads legacy/pico-micropython/matrix_fonts.py (pure data, so it's exec'd into a dict) and
legacy/pico-micropython/eyes_ani.json, and writes:

    src/display/sprites.h, src/display/sprites.cpp      every picture in `eyes` and `shapes`
    src/display/fonts.h, src/display/fonts.cpp          textFont1, witch, bat, pacman
    src/effects/animations.h, src/effects/animations.cpp  eyes_ani.json, and runAnimation()

It was run once for the move to C++. The C++ files are the originals now and get edited by hand,
so don't run this again unless you mean to throw those edits away. It's kept to show where the
pictures came from.
"""

import json
import keyword
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LEGACY = ROOT / 'legacy' / 'pico-micropython'
FONTS_PY = LEGACY / 'matrix_fonts.py'
ANIMS_JSON = LEGACY / 'eyes_ani.json'

GENERATED = 'Generated once from the MicroPython originals by tools/gen_sprites.py; edit by hand from now on.'

# Python dict name -> C++ font name, in the order they're written
FONTS = (('textFont1', 'textFont'), ('witch', 'witchFont'), ('bat', 'batFont'), ('pacman', 'pacmanFont'))

# Used instead of the comment above the dict (textFont1's is just the file's description line)
FONT_NOTES = {
    'textFont1': ['Letters, numbers and symbols for scrolling words.',
                  "There's no '$' (it scrolls as a gap), and ';' is a copy of '?'."],
}

# Notes on animations that don't do quite what their names say. Ported exactly as the pumpkin played them.
ANIM_NOTES = {
    'stareAndBlink': [
        'The last two steps dim both eyes to brightness 0 (the dimmest, not off) for half a second, then set',
        'brightness 2 (capped at maxBright()). So with maxBright() above 2 the eyes stay at 2 afterwards.',
        '"br" meant right-eye brightness in the GurgleApps original; the MAX7219 version sets both eyes.',
    ],
    'ghosts1': ['Not used by any effect. Its brightness 15 is capped at maxBright().'],
    'downRightABit': ['Not used by any effect.'],
    'winkLeft': [
        "Winks the eye on your RIGHT as you look at the pumpkin (the pumpkin's own left eye): the JSON blinks",
        '"l", and the old show_char() put "l" on the right.',
    ],
    'winkRight': [
        'Winks the eye on your LEFT, but step 7 had "l" and "r" swapped in eyes_ani.json, so for 20 ms the',
        'winking eye pops open and the other eye half-shuts. Kept, as that is how the pumpkin played it.',
    ],
}

PICTURE_DICTS = (('eyes', 'Eyes'), ('shapes', 'Shapes'))


def cpp_name(python_name):
    """all_off -> allOff (every other name is already camelCase)"""
    name = re.sub(r'_(\w)', lambda m: m.group(1).upper(), python_name)
    if not re.fullmatch(r'[A-Za-z_]\w*', name) or keyword.iskeyword(name):
        sys.exit(f"Can't make a C++ name from {python_name!r}")
    return name


def hex_rows(rows):
    return '{' + ', '.join(f'0x{row:02x}' for row in rows) + '}'


def char_literal(ch):
    if ch in ("'", '\\'):
        return "'\\" + ch + "'"
    if not ' ' <= ch <= '~':
        sys.exit(f'Font character {ch!r} is not plain ASCII')
    return "'" + ch + "'"


def read_comments(source):
    """Comments from matrix_fonts.py: {(dict, key): [lines]} for the line(s) above each entry,
    and {dict: [lines]} for the lines above each dict"""
    entry_comments, dict_comments = {}, {}
    current, pending, seen = None, [], {}
    for line in source.splitlines():
        stripped = line.strip()
        start = re.match(r'(\w+)\s*=\s*\{', stripped)
        if start:
            current = start.group(1)
            dict_comments[current] = pending
            pending = []
            seen[current] = []
        elif stripped.startswith('#'):
            pending.append(stripped.lstrip('#').strip())
        elif current:
            key = re.match(r"'((?:\\.|[^'\\])*)'\s*:", stripped)
            if key:
                name = key.group(1)
                seen[current].append(name)
                if pending:
                    entry_comments[(current, name)] = pending
                    pending = []
            if stripped.startswith('}'):
                current = None
    for dict_name, keys in seen.items():
        if len(keys) != len(set(keys)):
            dupes = sorted({k for k in keys if keys.count(k) > 1})
            print(f'Warning: {dict_name} has duplicate keys {dupes}; Python keeps the last one', file=sys.stderr)
    return entry_comments, dict_comments


def padded(name, rows):
    """Pictures shorter than 8 rows get blank rows at the bottom"""
    if len(rows) > 8 or any(not 0 <= r <= 0xff for r in rows):
        sys.exit(f'Picture {name!r} is not 8 rows of bytes')
    return list(rows) + [0] * (8 - len(rows))


def comment_block(lines, indent):
    return ''.join(f'{indent}// {text}\n' for text in lines)


PADDING_NOTE = [
    'Pictures shorter than 8 rows (spider has 4, slime has 5) are padded with blank rows at the bottom.',
    "That looks the same everywhere they're used: lower() pads with blank rows anyway, and spider_drop()",
    'drew blank rows below the spider.',
]


def write_sprites(data, entry_comments, out_h, out_cpp):
    h = ['#pragma once',
         "// Every picture from the pumpkin, one 8x8 Frame each: the eyes and shapes from matrix_fonts.py.",
         '// ' + GENERATED,
         '//',
         '// Names are the old Python names, except all_off and all_on, which became allOff and allOn.']
    h += ['// ' + line for line in PADDING_NOTE]
    h += ['', '#include "display/frame.h"', '']
    cpp = ["// Every picture from the pumpkin: the eyes and shapes from matrix_fonts.py.",
           '// ' + GENERATED,
           '',
           '#include "display/sprites.h"',
           '']
    for dict_name, namespace in PICTURE_DICTS:
        h += [f'namespace {namespace}', '{']
        cpp += [f'namespace {namespace}', '{']
        for name, rows in data[dict_name].items():
            notes = entry_comments.get((dict_name, name), [])
            cname = cpp_name(name)
            if notes:
                h.append(comment_block(notes, '    ').rstrip('\n'))
                cpp.append(comment_block(notes, '    ').rstrip('\n'))
            renamed = f"  // '{name}'" if cname != name else ''
            short = f'  // {len(rows)} rows, padded' if len(rows) < 8 else ''
            h.append(f'    extern const Frame {cname};{renamed}')
            cpp.append(f'    const Frame {cname} = {hex_rows(padded(name, rows))};{renamed}{short}')
        h += ['}', '']
        cpp += ['}', '']
    out_h.write_text('\n'.join(h).rstrip('\n') + '\n')
    out_cpp.write_text('\n'.join(cpp).rstrip('\n') + '\n')


def write_fonts(data, dict_comments, out_h, out_cpp):
    h = ['#pragma once',
         "// The fonts: textFont for words, and the sprite fonts, where each letter is one picture, so",
         '// scrolling a "message" like " 12 " flies the picture across the eyes. Missing characters are blank.',
         '// ' + GENERATED,
         '',
         '#include "display/font.h"',
         '']
    cpp = ['// The fonts from matrix_fonts.py.',
           '// ' + GENERATED,
           '',
           '#include "display/fonts.h"',
           '',
           '#include <iterator>',
           '',
           'namespace',
           '{']
    for py_name, c_name in FONTS:
        glyphs = data[py_name]
        notes = FONT_NOTES.get(py_name) or dict_comments.get(py_name, [])
        if notes:
            h.append(comment_block(notes, '').rstrip('\n'))
            cpp.append(comment_block(notes, '    ').rstrip('\n'))
        h.append(f'extern const Font {c_name};')
        array = 'k' + c_name[0].upper() + c_name[1:] + 'Glyphs'
        cpp.append(f'    const Glyph {array}[] = {{')
        for ch, rows in glyphs.items():
            if len(rows) != 8:
                sys.exit(f'{py_name}[{ch!r}] is not 8 rows')
            cpp.append(f'        {{{char_literal(ch)}, {hex_rows(rows)}}},')
        cpp += ['    };', '']
    cpp[-1:] = ['}', '']
    for py_name, c_name in FONTS:
        array = 'k' + c_name[0].upper() + c_name[1:] + 'Glyphs'
        cpp.append(f'const Font {c_name} = {{{array}, std::size({array})}};')
    out_h.write_text('\n'.join(h) + '\n')
    out_cpp.write_text('\n'.join(cpp) + '\n')


def ms(seconds):
    """time.sleep(seconds) -> milliseconds, rounded the way Python's round() does"""
    value = round(seconds * 1000)
    if not 0 <= value <= 0xffff:
        sys.exit(f'Delay {seconds} is out of range')
    return value


def write_animations(anims, eyes, out_h, out_cpp):
    names = list(anims)
    for name in names:
        cpp_name(name)

    h = ['#pragma once',
         "// The eye animations from eyes_ani.json, and the player that runs them (the old anim_runner).",
         '// ' + GENERATED,
         '//',
         '// The old anim_runner called show_char(font[l], font[r]), and show_char put its first picture on the',
         '// RIGHT eye as you look at the pumpkin. So JSON "l" pictures were shown on your right. The steps',
         '// here are written as you look at the pumpkin (left = your left), and show exactly what it showed.',
         '',
         '#include <cstddef>',
         '#include <cstdint>',
         '#include "display/frame.h"',
         '#include "effects/engine.h"',
         '',
         '// One step: show a picture on each eye (or keep what is showing), maybe change brightness, then pause',
         'struct AnimationStep',
         '{',
         '    const Frame *left;  // your left eye (JSON "r"); nullptr for both = keep the pictures',
         '    const Frame *right; // your right eye (JSON "l")',
         '    int8_t brightness;  // JSON "br", capped at maxBright() when it plays (-1 = leave it alone)',
         '    uint16_t delayMs;   // JSON "d", in milliseconds',
         '};',
         '',
         'struct Animation',
         '{',
         '    const char *name; // as in eyes_ani.json, e.g. "stareAndBlink"',
         '    const AnimationStep *steps;',
         '    size_t count;',
         '};',
         '',
         'namespace Animations',
         '{']
    for name in names:
        h.append(f'    extern const Animation {name};')
    h += ['}',
          '',
          '// Play an animation once: a new mouth colour each step, as the old anim_runner did',
          'void runAnimation(Engine &engine, const Animation &animation);']

    cpp = ["// The eye animations from eyes_ani.json, and the player that runs them.",
           '// ' + GENERATED,
           '',
           '#include "effects/animations.h"',
           '',
           '#include <algorithm>',
           '#include <iterator>',
           '#include "display/sprites.h"',
           '',
           'namespace',
           '{',
           '    constexpr int8_t kSameBrightness = -1;',
           '']
    for name in names:
        array = 'k' + name[0].upper() + name[1:]
        if name in ANIM_NOTES:
            cpp.append(comment_block(ANIM_NOTES[name], '    ').rstrip('\n'))
        cpp.append(f'    const AnimationStep {array}[] = {{')
        for step in anims[name]:
            extra = set(step) - {'l', 'r', 'br', 'd'}
            if extra:
                sys.exit(f'{name}: unknown keys {sorted(extra)}')
            if 'd' not in step:
                sys.exit(f'{name}: every step needs a "d" delay')
            jl, jr = step.get('l'), step.get('r')
            for pic in (jl, jr):
                if pic is not None and pic not in eyes:
                    sys.exit(f'{name}: no picture called {pic!r}')
            if jl is not None and jr is not None:
                # JSON "r" is your left eye, JSON "l" is your right eye
                left, right = f'&Eyes::{cpp_name(jr)}', f'&Eyes::{cpp_name(jl)}'
            else:
                # anim_runner only showed pictures when both were given
                left = right = 'nullptr'
            br = step.get('br')
            brightness = 'kSameBrightness' if br is None else str(int(br))
            cpp.append(f'        {{{left}, {right}, {brightness}, {ms(step["d"])}}},')
        cpp += ['    };', '']
    cpp += ['}', '', 'namespace Animations', '{']
    for name in names:
        array = 'k' + name[0].upper() + name[1:]
        cpp.append(f'    const Animation {name} = {{"{name}", {array}, std::size({array})}};')
    cpp += ['}',
            '',
            'void runAnimation(Engine &engine, const Animation &animation)',
            '{',
            '    for (size_t i = 0; i < animation.count; i++)',
            '    {',
            '        const AnimationStep &step = animation.steps[i];',
            '        engine.mouthRandomColour();',
            '        if (step.left && step.right)',
            '            engine.show(*step.left, *step.right);',
            '        if (step.brightness != kSameBrightness)',
            '            engine.setBrightness(std::min<uint8_t>(step.brightness, engine.maxBright()));',
            '        engine.wait(step.delayMs);',
            '    }',
            '}']
    out_h.write_text('\n'.join(h) + '\n')
    out_cpp.write_text('\n'.join(cpp) + '\n')


def main():
    source = FONTS_PY.read_text(encoding='utf-8')
    data = {}
    exec(compile(source, str(FONTS_PY), 'exec'), {}, data)
    anims = json.loads(ANIMS_JSON.read_text(encoding='utf-8'))
    entry_comments, dict_comments = read_comments(source)

    display, effects = ROOT / 'src' / 'display', ROOT / 'src' / 'effects'
    write_sprites(data, entry_comments, display / 'sprites.h', display / 'sprites.cpp')
    write_fonts(data, dict_comments, display / 'fonts.h', display / 'fonts.cpp')
    write_animations(anims, data['eyes'], effects / 'animations.h', effects / 'animations.cpp')
    print('Wrote sprites, fonts and animations')


if __name__ == '__main__':
    main()
