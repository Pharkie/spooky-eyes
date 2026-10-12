#!/usr/bin/env python3
"""Record exactly what the MicroPython effects do, as golden files for the C++ tests.

Runs legacy/pico-micropython/main.py under ordinary Python 3 on this computer, with fake hardware:
the eyes, the mouth LED, time.sleep(), print('SAY:...') and the random module are all swapped for
recorders. Then it plays every effect the old button page could play (the %EFFECTS table in
legacy/pico-micropython/tools/pumpkin-buttons.pl) and writes what happened to test/golden/<id>.txt.
`pio test -e native` then plays the C++ effects and checks they do exactly the same thing.

Also recorded: one pass of the default show (default_show.txt: the blocks of defaultShow() in
src/effects/show.cpp, read from that file, each played as the button page played it: its phrase, then
its %EFFECTS code), the first few effects of the never-ending surprise show (surprise.txt), typed words
(words.txt), every animation in eyes_ani.json (anim_*.txt), edge cases for the building blocks
(prim_*.txt), every font character (fonts.txt) and the talking pumpkin's phrase for each button
(phrases.txt). index.txt lists the recordings.

    python3 tools/capture_python_frames.py          rewrite all the golden files
    python3 tools/capture_python_frames.py --check  check the golden files are up to date (no writing)

Only the standard library is needed. The output is the same every run: the random numbers come from
a fixed generator (below), and nothing depends on the clock.

Golden file format: one event per line ('#' lines are comments)

    F <left> <right>   both eyes, as you look at the pumpkin; each eye is 8 rows as 16 hex digits,
                       top row first, most significant bit = leftmost dot
    W <ms>             wait, in milliseconds (time.sleep(s) -> round(s * 1000))
    B <level>          set MAX7219 brightness
    S <words>          the talking pumpkin says something (print('SAY:words'))
    M                  the mouth LED picks a new random colour

The rules the C++ port follows so the two can be compared:

  * Eyes: max7219_eyes.show_char(a, b) puts a on the RIGHT eye as you look at the pumpkin, so it is
    recorded as left=b, right=a. The C++ Engine::show(left, right) is already in viewer order.
  * Random numbers: everything is built on one function r(n) -> 0..n-1, which is engine.random(n)
    in C++:  random.choice(seq) -> seq[r(len(seq))];  random.randint(a, b) -> a + r(b - a + 1);
    random.getrandbits(8) -> r(256);  random.randrange(n) -> r(n).
    r(n) is xorshift32 seeded with 12345, reset before every effect (test/recording_engine.h uses the
    same generator).
  * Mouth: with the RGB LED connected (BOARD = "pico"), anim_runner() and scroll_message() do
    `set_rgb_color(*random.choice(LED_COLOURS))` every step; the C++ calls
    engine.mouthRandomColour(), which uses up r(3) and records "M".
  * Settings: brightness 2, eye gap 8, name "Jack's Pumpkin" (the values main.py has built in).
"""

import argparse
import binascii
import copy
import re
import sys
import types
from pathlib import Path

# Never leave __pycache__ folders in the frozen legacy code
sys.dont_write_bytecode = True

REPO = Path(__file__).resolve().parent.parent
LEGACY = REPO / 'legacy' / 'pico-micropython'
BUTTONS_PL = LEGACY / 'tools' / 'pumpkin-buttons.pl'
SHOW_CPP = REPO / 'src' / 'effects' / 'show.cpp'
GOLDEN = REPO / 'test' / 'golden'

SEED = 12345

# The surprise show never ends, so only its first few effects are recorded. The C++ test stops the
# C++ surprise show after the same number of events and compares that much.
SURPRISE_EFFECTS = 3

# Extra recordings that aren't buttons: (file id, Python code, what the C++ test runs instead).
# test/test_effects/test_main.cpp has the matching C++ call for each id.
EXTRA = [
    # One pass of the default show (the old main() loop as blocks), built from src/effects/show.cpp
    ('default_show', None, 'for (const ShowBlock &block : defaultShow()) playBlock(engine, block)'),
    # "Your own words" from the button page, which padded the words with a space each side.
    # '$' isn't in the font, so it scrolls as a gap.
    ('words', "m.scroll_message(T, ' Hello Jack! $5 ok? ')", 'scrollWords(engine, "Hello Jack! $5 ok?")'),
    # Every animation in eyes_ani.json, including the two no button uses
    ('anim_stareAndBlink', "m.anim_runner(a, 'stareAndBlink', E)", 'runAnimation(engine, Animations::stareAndBlink)'),
    ('anim_downLeftABit', "m.anim_runner(a, 'downLeftABit', E)", 'runAnimation(engine, Animations::downLeftABit)'),
    ('anim_downRightABit', "m.anim_runner(a, 'downRightABit', E)", 'runAnimation(engine, Animations::downRightABit)'),
    ('anim_roll', "m.anim_runner(a, 'roll', E)", 'runAnimation(engine, Animations::roll)'),
    ('anim_growEyes', "m.anim_runner(a, 'growEyes', E)", 'runAnimation(engine, Animations::growEyes)'),
    ('anim_ghosts1', "m.anim_runner(a, 'ghosts1', E)", 'runAnimation(engine, Animations::ghosts1)'),
    ('anim_winkLeft', "m.anim_runner(a, 'winkLeft', E)", 'runAnimation(engine, Animations::winkLeft)'),
    ('anim_winkRight', "m.anim_runner(a, 'winkRight', E)", 'runAnimation(engine, Animations::winkRight)'),
    ('anim_minecraft', "m.anim_runner(a, 'minecraft', E)", 'runAnimation(engine, Animations::minecraft)'),
    # Edge cases for the building blocks in primitives.h
    ('prim_scroll_empty', "m.scroll_message(T, '')", 'scrollMessage(engine, textFont, "")'),
    ('prim_scroll_short', "m.scroll_message(T, 'x;', 0.01)", 'scrollMessage(engine, textFont, "x;", 10)'),
    ('prim_fly_uneven', "m.fly(f.bat, (' a ', ' ab ', 'b'), 0.02, 2)",
     'fly(engine, batFont, {" a ", " ab ", "b"}, 20, 2)'),
    ('prim_slide_back', "m.slide((S['fireball1'], S['fireball2'], S['star3']), 20, -4, left_bg=S['target'], delay=0.01)",
     'slide(engine, {Shapes::fireball1, Shapes::fireball2, Shapes::star3}, 20, -4, Shapes::target, kBlank, 10)'),
    ('prim_slide_still', "m.slide((S['dirt'],), 5, 5, right_bg=S['heart1'])",
     'slide(engine, {Shapes::dirt}, 5, 5, kBlank, Shapes::heart1)'),
    ('prim_flip_odd', "m.flip(S['cat1'], S['cat2'], 3, 0.01)", 'flip(engine, Shapes::cat1, Shapes::cat2, 3, 10)'),
    ('prim_sparkles_keep', "m.sparkles(3, 1, left=S['heart1'], right=True, delay=0.01)",
     'sparkles(engine, 3, 1, EyeSetting::keep(Shapes::heart1), EyeSetting::sparkle(), 10)'),
    ('prim_explode_one_eye', "m.explode(left=True, right=False)",
     'explode(engine, EyeSetting::sparkle(), EyeSetting::blank())'),
]

# Fonts checked character by character: Python name -> C++ name
FONTS = [('textFont1', 'textFont'), ('witch', 'witchFont'), ('bat', 'batFont'), ('pacman', 'pacmanFont')]


class StopShow(Exception):
    """Raised to stop the never-ending surprise show"""


class Recorder:
    """Collects the event lines for one effect"""

    def __init__(self):
        self.reset()

    def reset(self):
        self.events = []
        self.state = SEED

    # The shared random number generator: xorshift32, then x % n
    def r(self, n):
        if n <= 0:
            raise ValueError('r(n) needs n >= 1, got %r' % n)
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x
        return x % n

    def frame(self, left, right):
        self.events.append('F %s %s' % (eye_hex(left), eye_hex(right)))

    def add(self, line):
        self.events.append(line)


REC = Recorder()


def eye_hex(rows):
    """8 rows as 16 hex digits. Complains about anything the real display code would choke on."""
    rows = list(rows)
    if len(rows) < 8:
        raise ValueError('picture has only %d rows: %r' % (len(rows), rows))
    for row in rows[:8]:
        if not isinstance(row, int) or not 0 <= row <= 0xFF:
            raise ValueError('picture row out of range: %r in %r' % (row, rows))
    return ''.join('%02x' % row for row in rows[:8])


# ---- Fake modules ----

def make_fake_modules():
    machine = types.ModuleType('machine')

    class Pin:
        OUT = 1
        IN = 0

        def __init__(self, *args, **kwargs):
            self._value = 0

        def value(self, v=None):
            if v is None:
                return self._value
            self._value = 1 if v else 0

    class SPI:
        def __init__(self, *args, **kwargs):
            pass

        def write(self, data):
            pass

    machine.Pin = Pin
    machine.SPI = SPI

    micropython = types.ModuleType('micropython')
    micropython.const = lambda x: x

    max7219 = types.ModuleType('max7219_matrix')

    class max7219_matrix:
        def __init__(self, spi, cs):
            pass

        def show_char(self, cl, cr):
            # cl is the eye on the viewer's right, cr the viewer's left (see the module docstring)
            REC.frame(left=cr, right=cl)

        def set_brightness(self, brightness):
            REC.add('B %d' % brightness)

    max7219.max7219_matrix = max7219_matrix

    fake_random = types.ModuleType('random')

    def choice(seq):
        return seq[REC.r(len(seq))]

    def randint(a, b):
        return a + REC.r(b - a + 1)

    def getrandbits(k):
        return REC.r(1 << k)

    def randrange(n):
        return REC.r(n)

    def not_allowed(*args, **kwargs):
        raise RuntimeError('this random function has no C++ equivalent in the contract')

    fake_random.choice = choice
    fake_random.randint = randint
    fake_random.getrandbits = getrandbits
    fake_random.randrange = randrange
    for name in ('random', 'uniform', 'shuffle', 'seed', 'sample'):
        setattr(fake_random, name, not_allowed)

    fake_time = types.ModuleType('time')

    def sleep(seconds):
        REC.add('W %d' % round(seconds * 1000))

    def not_needed(*args, **kwargs):
        raise RuntimeError('effects should only use time.sleep()')

    fake_time.sleep = sleep
    fake_time.localtime = not_needed
    fake_time.time = not_needed
    fake_time.ticks_ms = not_needed

    return machine, micropython, max7219, fake_random, fake_time


# Called with the effect id when the surprise show prints 'FX:<id>'
FX_HOOK = None


def fake_print(*args, sep=' ', end='\n', **kwargs):
    text = sep.join(str(a) for a in args)
    if text.startswith('SAY:'):
        REC.add('S ' + SPOKEN.get(text[4:], text[4:]))
    elif text.startswith('FX:') and FX_HOOK:
        FX_HOOK(text[3:])
    else:
        sys.stderr.write('  (print) %s\n' % text)


def load_main():
    """Import legacy main.py with the fake hardware, and return (main module, matrix_fonts module)"""
    machine, micropython, max7219, fake_random, fake_time = make_fake_modules()
    sys.modules['machine'] = machine
    sys.modules['micropython'] = micropython
    sys.modules['max7219_matrix'] = max7219
    sys.path.insert(0, str(LEGACY))
    import matrix_fonts
    import main
    sys.path.remove(str(LEGACY))

    # Swap main.py's view of the outside world for the recorders
    main.random = fake_random
    main.time = fake_time
    main.print = fake_print
    main.set_rgb_color = lambda r, g, b: REC.add('M')

    # load_anims() opens 'eyes_ani.json' relative to the board's root folder
    real_load_anims = main.load_anims
    main.load_anims = lambda name: real_load_anims(str(LEGACY / name))

    # Nothing here runs main() (the default show is rebuilt from its blocks), but keep logs quiet
    main.log_message = lambda message, mode='a': None

    # The settings the C++ tests use
    assert main.BOARD == 'pico' and main.RGB_LED_CONNECTED, 'expected the Pico setup with the mouth LED'
    assert main.MAX_BRIGHT == 2, 'expected MAX_BRIGHT = 2'
    assert main.EYE_GAP == 8, 'expected EYE_GAP = 8'
    return main, matrix_fonts, fake_time, fake_random


def read_perl_hash(name):
    """A `my %NAME = ( key => 'string', ... );` table from pumpkin-buttons.pl: [(key, string)] in file order"""
    text = BUTTONS_PL.read_text(encoding='utf-8')
    block = re.search(r'my %' + name + r' = \((.*?)\n\);', text, re.S)
    if not block:
        raise SystemExit("Can't find %" + name + ' in ' + str(BUTTONS_PL))
    items = []
    for line in block.group(1).splitlines():
        line = line.strip()
        if not line:
            continue
        m = re.fullmatch(r'(\w+)\s*=>\s*(?:"((?:[^"\\]|\\.)*)"|\'((?:[^\'\\]|\\.)*)\'),?', line)
        if not m:
            raise SystemExit("Don't understand this %" + name + ' line: ' + line)
        if m.group(2) is not None:
            # Double quotes: backslash escapes, and $ or @ would interpolate
            value = re.sub(r'\\(.)', lambda e: {'n': '\n', 't': '\t'}.get(e.group(1), e.group(1)), m.group(2))
            if '$' in value or '@' in value:
                raise SystemExit('Perl would interpolate this line; update the parser: ' + line)
        else:
            value = re.sub(r"\\([\\'])", r'\1', m.group(3))
        items.append((m.group(1), value))
    return items


def read_button_effects():
    """The %EFFECTS table from pumpkin-buttons.pl: [(id, python code)] in file order"""
    return read_perl_hash('EFFECTS')


def read_default_show():
    """The blocks of defaultShow() in src/effects/show.cpp: [('Effect', id) or ('Words', words)] in order"""
    text = SHOW_CPP.read_text(encoding='utf-8')
    table = re.search(r'const Show &defaultShow\(\)\s*\{.*?static const Show kShow = \{\n(.*?)\n\s*\};', text, re.S)
    if not table:
        raise SystemExit("Can't find the defaultShow() table in " + str(SHOW_CPP))
    blocks = []
    for line in table.group(1).splitlines():
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        m = re.fullmatch(r'\{ShowBlock::Kind::(Effect|Words), "((?:[^"\\]|\\.)*)"\},?\s*(?://.*)?', line)
        if not m:
            raise SystemExit("Don't understand this defaultShow() line: " + line)
        if re.search(r'\\[^"\\]', m.group(2)):
            raise SystemExit('Only \\" and \\\\ escapes are understood here; update the parser: ' + line)
        blocks.append((m.group(1), re.sub(r'\\(["\\])', r'\1', m.group(2))))
    if not blocks:
        raise SystemExit('The defaultShow() table in ' + str(SHOW_CPP) + ' is empty')
    return blocks


def run_default_show(blocks, effects, namespace, phrases):
    """Play the default show's blocks the way playBlock() does: an effect says its phrase (as the button
    page did), then runs its %EFFECTS code; words are said, then scrolled with a space each side (as the
    page's "your own words" box did). Returns the Python, one line per block, for the golden file."""
    by_id = dict(effects)
    lines = []
    for n, (kind, value) in enumerate(blocks, 1):
        if kind == 'Effect':
            if value not in by_id:
                raise SystemExit('The default show has "%s", which pumpkin-buttons.pl never had' % value)
            code = by_id[value]
            if value == 'name':
                REC.add('S ' + RECORDED_NAME)  # the deliberate change: it says the pumpkin's own name first
            elif value in phrases:
                REC.add('S ' + phrases[value])
        else:
            code = 'm.scroll_message(T, %r)' % (' ' + value + ' ')
            REC.add('S ' + value)
        exec(code, dict(namespace))
        lines.append('%2d %-7s %-10s %s' % (n, kind, value if kind == 'Effect' else '', code.replace('\n', '; ')))
    return lines


def surprise_code(effects):
    """The surprise show's Python, built the way pumpkin-buttons.pl builds it"""
    mix = sorted(fx_id for fx_id, _ in effects if fx_id != 'spell')
    code = 'import random\nwhile True:\n n = random.randrange(%d)\n' % len(mix)
    by_id = dict(effects)
    for i, fx_id in enumerate(mix):
        code += "%s n == %d:\n  print('FX:%s')\n" % (' elif' if i else ' if', i, fx_id)
        code += ''.join('  %s\n' % line for line in by_id[fx_id].split('\n'))
    return code + ' time.sleep(0.5)'


def run_surprise(code, namespace, phrases, fake_random):
    """Run the surprise show until it starts effect number SURPRISE_EFFECTS + 1. The button page spoke
    each effect's phrase when it saw the 'FX:<id>' line, so that's recorded as a SAY."""
    global FX_HOOK
    started = []

    def on_fx(fx_id):
        started.append(fx_id)
        if len(started) > SURPRISE_EFFECTS:
            raise StopShow()
        if fx_id in phrases:
            REC.add('S ' + phrases[fx_id])

    real_random = sys.modules['random']
    sys.modules['random'] = fake_random  # the code does `import random`
    FX_HOOK = on_fx
    try:
        exec(code, dict(namespace))
    except StopShow:
        pass
    finally:
        FX_HOOK = None
        sys.modules['random'] = real_random
    return started[:SURPRISE_EFFECTS]


# Deliberate changes from the old pumpkin, applied to the recordings so the tests check the new behaviour.
# The "name" button scrolls each pumpkin's own name (the recording engine's is "Jack's Pumpkin"), so it says
# that name as it starts, instead of the old page's fixed phrase "The pumpkin's name!".
RECORDED_NAME = "Jack's Pumpkin"


# Sounds respelt so phone voices make the sound instead of spelling out the letters ("S, S, S..."). The
# Mac voice the old page used coped; speechSynthesis on phones doesn't.
SPOKEN = {
    'Sssssssss': 'Shhhhhhh!',
    'Sssssssssssss': 'Shhhhhhhhhh!',
    'Hrrmm?': 'Hurrmm?',
    'Hrrm! Hrrm!': 'Hurrmm! Hurrmm!',
    "I'm so sleepy. Zzzzzzzz.": "I'm so sleepy. Snore... snore...",
}


def button_phrases():
    """%PHRASES from pumpkin-buttons.pl, minus phrases the new firmware deliberately dropped or respelt"""
    phrases = dict(read_perl_hash('PHRASES'))
    del phrases['name']
    return {fx_id: SPOKEN.get(phrase, phrase) for fx_id, phrase in phrases.items()}


def capture_all():
    """Run everything; returns {id: (python call, [event lines])} in a fixed order"""
    main, f, fake_time, fake_random = load_main()
    anims = main.load_anims('eyes_ani.json')
    fonts_before = copy.deepcopy((f.textFont1, f.eyes, f.shapes, f.witch, f.bat, f.pacman, anims))

    namespace = {
        'm': main, 'f': f, 'a': anims, 'S': f.shapes, 'E': f.eyes, 'T': f.textFont1,
        'time': fake_time, 'random': fake_random, 'print': fake_print, 'binascii': binascii,
    }

    effects = read_button_effects()
    phrases = button_phrases()
    jobs = list(effects) + [('surprise', surprise_code(effects))]
    jobs += [(fx_id, code) for fx_id, code, _ in EXTRA]

    results = {}
    for fx_id, code in jobs:
        REC.reset()
        if fx_id == 'default_show':
            lines = run_default_show(read_default_show(), effects, namespace, phrases)
            code = '\n'.join(['each block of defaultShow() in src/effects/show.cpp, played as the button page played it:',
                              "the phrase, then the %EFFECTS code (\"name\" says the pumpkin's own name instead)"] + lines)
        elif fx_id == 'surprise':
            played = run_surprise(code, namespace, phrases, fake_random)
            code = ('the surprise show, built from %%EFFECTS as pumpkin-buttons.pl builds it, stopped as it\n'
                    'starts effect %d. It played: %s' % (SURPRISE_EFFECTS + 1, ', '.join(played)))
        else:
            exec(code, dict(namespace))
            if fx_id == 'name':
                REC.events.insert(0, 'S ' + RECORDED_NAME)
                code += "\n(new firmware: says the pumpkin's own name first)"
        results[fx_id] = (code, list(REC.events))

        # Nothing should change the shared pictures (the C++ ones are const)
        now = (f.textFont1, f.eyes, f.shapes, f.witch, f.bat, f.pacman, anims)
        if now != fonts_before:
            raise RuntimeError(fx_id + ' changed a font or animation in place')
    return results


def golden_text(fx_id, code, events):
    cpp = dict((i, c) for i, _, c in EXTRA).get(fx_id)
    lines = ['# %s: recorded from legacy/pico-micropython/main.py by tools/capture_python_frames.py' % fx_id]
    for n, code_line in enumerate(code.splitlines()):
        lines.append('# %s %s' % ('python:' if n == 0 else '       ', code_line))
    if cpp:
        lines.append('# c++:    %s' % cpp)
    lines.append('# %d events. Do not edit: re-run the capture script instead.' % len(events))
    return '\n'.join(lines + events) + '\n'


def phrases_text():
    """What the talking pumpkin said as each button's effect started (%PHRASES in pumpkin-buttons.pl)"""
    phrases = button_phrases()
    lines = ['# Phrases from %PHRASES in legacy/pico-micropython/tools/pumpkin-buttons.pl, by '
             'tools/capture_python_frames.py.',
             '# <id> <phrase>; an id on its own has no phrase.']
    for fx_id, _ in read_button_effects() + [('surprise', '')]:
        lines.append(('%s %s' % (fx_id, phrases[fx_id])) if fx_id in phrases else fx_id)
    return '\n'.join(lines) + '\n'


def fonts_text():
    """Every character 0-127 of every font, as font.get(c, blank) gives it"""
    import matrix_fonts as f
    lines = ['# Font pictures from legacy/pico-micropython/matrix_fonts.py, by tools/capture_python_frames.py.',
             '# <c++ font> <character code> <picture>; characters a font lacks are blank.']
    for py_name, cpp_name in FONTS:
        font = getattr(f, py_name)
        blank = [0] * 8
        for code in range(128):
            lines.append('%s %d %s' % (cpp_name, code, eye_hex(font.get(chr(code), blank))))
    return '\n'.join(lines) + '\n'


def main_cli():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true', help='only check the golden files are up to date')
    args = parser.parse_args()

    results = capture_all()
    files = {fx_id + '.txt': golden_text(fx_id, code, events) for fx_id, (code, events) in results.items()}
    files['index.txt'] = ('# Golden recordings, one id per line (test/golden/<id>.txt). Written by '
                          'tools/capture_python_frames.py.\n' + ''.join(i + '\n' for i in results))
    files['fonts.txt'] = fonts_text()
    files['phrases.txt'] = phrases_text()

    stale = []
    for name, text in files.items():
        path = GOLDEN / name
        old = path.read_text(encoding='utf-8') if path.exists() else None
        if old != text:
            stale.append(name)
            if not args.check:
                GOLDEN.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding='utf-8')
    known = set(files)
    extra = sorted(p.name for p in GOLDEN.glob('*.txt') if p.name not in known) if GOLDEN.exists() else []

    for fx_id, (_, events) in results.items():
        counts = {k: sum(1 for e in events if e[0] == k) for k in 'FWBSM'}
        print('%-21s %5d events  (%d frames, %d waits, %d brightness, %d say, %d mouth)' % (
            fx_id, len(events), counts['F'], counts['W'], counts['B'], counts['S'], counts['M']))
    if extra:
        print('Not made by this script (delete them?): ' + ', '.join(extra))
    if args.check:
        if stale:
            print('Out of date: ' + ', '.join(stale))
            return 1
        print('All %d golden files are up to date.' % len(files))
    else:
        print('Wrote %d of %d files in %s' % (len(stale), len(files), GOLDEN.relative_to(REPO)))
    return 0


if __name__ == '__main__':
    sys.exit(main_cli())
