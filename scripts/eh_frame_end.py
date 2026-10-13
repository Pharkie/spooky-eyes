"""End the C++ exception frame table (.eh_frame) with the zero word the unwinder looks for.

The unwinder walks the table from its start until it meets an entry of length 0. That word normally
comes from crtend.o, but ESP-IDF links without the start files, and its esp32c3 linker script only pads
.eh_frame to a multiple of 8. So a table that happens to end on a multiple of 8 runs straight into
whatever follows it. Then the first exception (an effect interrupted by a button press or an
announcement: EffectInterrupted) crashes in classify_object_over_fdes, and the board boot-loops.
Whether it happens depends on the table's size, so any code change can bring it on (it did, Oct 2026).

So: assemble scripts/eh_frame_end.S (a 4-byte zero in .eh_frame) and put it at the very end of the link
command, after every object and library, so its .eh_frame comes last in the table. Then check the built
firmware: the word straight after the table must be zero. Runs automatically
(extra_scripts = post:scripts/eh_frame_end.py)."""

import subprocess

Import("env")  # noqa: F821

terminator = env.Object("$BUILD_DIR/eh_frame_end.o", "$PROJECT_DIR/scripts/eh_frame_end.S")  # noqa: F821
env.Depends("$BUILD_DIR/${PROGNAME}.elf", terminator)  # noqa: F821
env.Replace(LINKCOM=env["LINKCOM"] + ' "%s"' % terminator[0].get_abspath())  # noqa: F821  (the path may have spaces)


def check_terminator(source, target, env):
    elf = str(target[0])
    objdump = env.subst("$CC").replace("-gcc", "-objdump")
    headers = subprocess.run([objdump, "-h", elf], capture_output=True, text=True, check=True).stdout
    start = size = None
    for line in headers.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[1] == ".eh_frame":
            size, start = int(parts[2], 16), int(parts[3], 16)
    if start is None:
        print("eh_frame_end: no .eh_frame section (C++ exceptions off?)")
        return
    last = start + size - 4
    dump = subprocess.run([objdump, "-s", "-j", ".eh_frame", "--start-address=%#x" % last, elf],
                          capture_output=True, text=True, check=True).stdout
    words = [line.split()[1] for line in dump.splitlines() if line.strip().lower().startswith("%x" % last)]
    if not words or int(words[0], 16) != 0:
        env.Exit("eh_frame_end: the C++ exception table doesn't end with a zero word, so the board would "
                 "crash on the first interrupted effect. Is scripts/eh_frame_end.o last in the link?")
    print("eh_frame_end: the exception table ends with a zero word (at %#x)" % last)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_terminator)  # noqa: F821
