"""
Version 1 by GurgleApps.com
Tool To Make Sprites https://gurgleapps.com/tools/matrix
Instructions
https://gurgleapps.com/learn/projects/8x8-led-matrix-halloween-jack-o-lantern-pumpkin-project-with-a-pico

Revised by: Adam Knowles
Version: 0.1
Description: Halloween pumpkin eyes for 2 x 8x8 LED Matrices (MAX7219)
GitHub Repository: https://github.com/Pharkie/pico-pumpkin/
License: GNU General Public License (GPL)
"""

import time
import json
import random
import gc
import machine
import matrix_fonts
from max7219_matrix import max7219_matrix

# User-settable variables

BOARD = "pico"  # Which board the eyes are wired to: "pico" (Raspberry Pi Pico) or "esp32c3"

if BOARD == "pico":
    SPI_BUS = 0  # Use SPI(0) for Raspberry Pi Pico
    CLK_PIN = 2  # CLK / SCK
    DIN_PIN = 3  # DIN / MOSI / TX
    CS_PIN = 1   # CS
    RGB_LED_CONNECTED = True  # Set to False if RGB LED is not connected
    # These pins aren't used if RGB_LED_CONNECTED is False
    RED_PIN = 14
    GREEN_PIN = 15
    BLUE_PIN = 16
else:  # ESP32-C3
    SPI_BUS = 0  # If the C3 says this SPI bus doesn't exist, try 1
    CLK_PIN = 6
    DIN_PIN = 7
    CS_PIN = 5
    RGB_LED_CONNECTED = False  # Set to True once the RGB LED is wired up
    RED_PIN = 1
    GREEN_PIN = 2
    BLUE_PIN = 3

MAX_BRIGHT = 2  # Set global max brightness 0-15

EYE_GAP = 8  # Blank columns between the eyes when text scrolls (0 = like one wide screen)

DEBUG = False # Set to True to log messages to log.txt

# Custom logging function
# Logs messages to a file with a timestamp
# mode='a' appends to the file, mode='w' overwrites the file
def log_message(message, mode='a'):
    """Log a message to a file"""
    if DEBUG:
        print(message)

        with open('log.txt', mode) as log_file:
            current_time = time.localtime()
            formatted_time = (
                f"{current_time[0]:04}-{current_time[1]:02}-{current_time[2]:02} " +
                f"{current_time[3]:02}:{current_time[4]:02}:{current_time[5]:02}"
            )
            log_file.write(f"{formatted_time} - {message}\n")

# Wipe the log file blank on each program run
log_message("Starting new log session", mode='w')

# Initialize SPI and MAX7219 matrix
log_message("Initializing SPI and MAX7219 matrix")
spi = machine.SPI(
    SPI_BUS,
    baudrate=10000000,
    polarity=0,
    phase=0,
    sck=machine.Pin(CLK_PIN),
    mosi=machine.Pin(DIN_PIN)
)
cs = machine.Pin(CS_PIN, machine.Pin.OUT)
max7219_eyes = max7219_matrix(spi, cs)
log_message("SPI and MAX7219 matrix initialized")

if RGB_LED_CONNECTED:
    # Define colours for RGB inner lights
    COLOUR_RED = (255, 0, 0)
    COLOUR_GREEN = (0, 255, 0)
    COLOUR_MAGENTA = (255, 0, 255)

    LED_COLOURS = [
        COLOUR_RED,
        COLOUR_GREEN,
        COLOUR_MAGENTA,
    ]

    red = machine.Pin(RED_PIN, machine.Pin.OUT)
    green = machine.Pin(GREEN_PIN, machine.Pin.OUT)
    blue = machine.Pin(BLUE_PIN, machine.Pin.OUT)

    def set_rgb_color(r, g, b):
        """Set the color of the RGB LED"""
        red.value(not r)  # Invert due to common anode
        green.value(not g)
        blue.value(not b)

def load_anims(file_name):
    """Load animations from json file"""
    data = {}
    try:
        with open(file_name, encoding='utf-8') as infile:
            data = json.load(infile)
    except FileNotFoundError:
        log_message('Problem loading JSON. File not found.')
    except json.JSONDecodeError:
        log_message('Problem loading JSON. Invalid JSON data.')

    log_message("Loaded animations from JSON file")

    return data

def anim_runner(anims_JSON, anim_name, font):
    """Run animations"""

    log_message(f"anim_runner() with {anim_name}")

    anims = anims_JSON[anim_name]

    for anim in anims:
        # Choose a new LED colour if RGB LED is connected
        if RGB_LED_CONNECTED:
            color = random.choice(LED_COLOURS)
            set_rgb_color(*color)

        left = anim.get("l")
        right = anim.get("r")

        if left is not None and right is not None:
            max7219_eyes.show_char(font[left], font[right])

        brightness = anim.get("br")

        if brightness is not None:
            if brightness > MAX_BRIGHT:
                brightness = MAX_BRIGHT
            # print(f"Setting brightness to: {brightness}")
            max7219_eyes.set_brightness(brightness)

        delay = anim.get("d")

        if delay is not None:
            time.sleep(delay)

    gc.collect()

def show_char(left, right):
    """Show character"""
    max7219_eyes.show_char(left,right)

def flip(pic1, pic2, times=4, delay=0.5):
    """Swap two pictures back and forth between the eyes, like a cartoon"""
    for i in range(times):
        if i % 2 == 0:
            show_char(pic1, pic2)
        else:
            show_char(pic2, pic1)
        time.sleep(delay)

def text_window(font, message, x):
    """Get the 8 columns of the message starting at column x (blank outside the message)"""
    char_pos, shift = divmod(x, 8)
    blank = font[' ']
    left_char = font.get(message[char_pos], blank) if 0 <= char_pos < len(message) else blank
    right_char = font.get(message[char_pos + 1], blank) if 0 <= char_pos + 1 < len(message) else blank
    return [((left_char[row] << shift) | (right_char[row] >> (8 - shift))) & 0xFF for row in range(8)]

def scroll_message(font, message, delay=0.04):
    """Scroll message into the right eye, then across into the left eye"""
    log_message(f"scroll_message() with message: {message}")

    # The left eye runs behind the right eye by one eye's width plus the gap between them
    left_eye_lag = 8 + EYE_GAP

    for x in range(-8, len(message) * 8 + left_eye_lag + 1):
        if RGB_LED_CONNECTED:
            color = random.choice(LED_COLOURS)
            set_rgb_color(*color)

        right_bytes = text_window(font, message, x)
        left_bytes = text_window(font, message, x - left_eye_lag)

        max7219_eyes.show_char(right_bytes, left_bytes)

        time.sleep(delay)

def show_eyes(left, right):
    """Show a picture on each eye (left and right as you look at the pumpkin)"""
    max7219_eyes.show_char(right, left)

def sequence(steps):
    """Show (left picture, right picture, seconds) steps one after another"""
    for left, right, seconds in steps:
        show_eyes(left, right)
        time.sleep(seconds)

def random_dots(sparseness):
    """One row of random dots; higher sparseness means fewer dots"""
    dots = 0xff
    for _ in range(sparseness):
        dots &= random.getrandbits(8)
    return dots

def sparkles(frames, sparseness=2, delay=0.06, left=True, right=True):
    """Random twinkly dots; higher sparseness means fewer dots.
    left/right: True to sparkle that eye, False to leave it blank, or a picture to keep showing."""
    def eye(setting):
        if setting is True:
            return [random_dots(sparseness) for _ in range(8)]
        return setting or [0, 0, 0, 0, 0, 0, 0, 0]

    for _ in range(frames):
        show_eyes(eye(left), eye(right))
        time.sleep(delay)

def explode(left=True, right=True):
    """BOOM: a bright burst, then smoke. left/right work like sparkles()"""
    S = matrix_fonts.shapes

    def eye(setting, name):
        return S[name] if setting is True else (setting or S['empty'])

    max7219_eyes.set_brightness(min(MAX_BRIGHT + 4, 15))
    for name in ('star1', 'star2', 'star3', 'star4', 'all_on'):
        show_eyes(eye(left, name), eye(right, name))
        time.sleep(0.07)
    max7219_eyes.set_brightness(MAX_BRIGHT)
    sparkles(8, 2, left=left, right=right)
    sparkles(8, 3, left=left, right=right)

def place(picture, x):
    """A picture moved x columns to the right (or left if x is negative)"""
    if x >= 0:
        return [row >> x for row in picture]
    return [(row << -x) & 0xff for row in picture]

def lower(picture, rows):
    """A picture moved down by some rows (padded with blank rows, and cut to 8 rows)"""
    return ([0] * rows + picture + [0] * 8)[:8]

def slide(frames, start, end, left_bg=None, right_bg=None, delay=0.03):
    """Slide a picture across the eyes, from column start to column end, over still pictures.
    Column 0 is the left edge of the left eye; the right eye starts at column 8 + EYE_GAP."""
    blank = [0, 0, 0, 0, 0, 0, 0, 0]
    left_bg, right_bg = left_bg or blank, right_bg or blank
    far = 8 + EYE_GAP
    step = 1 if end >= start else -1
    for i, x in enumerate(range(start, end + step, step)):
        sprite = frames[(i // 2) % len(frames)]
        show_eyes([a | b for a, b in zip(left_bg, place(sprite, x))],
                  [a | b for a, b in zip(right_bg, place(sprite, x - far))])
        time.sleep(delay)

def fly(font, frames, delay=0.04, flap=3):
    """Fly a picture across the eyes like scrolling text, swapping frames to animate it"""
    left_eye_lag = 8 + EYE_GAP
    width = max(len(frame) for frame in frames)
    for x in range(-8, width * 8 + left_eye_lag + 1):
        message = frames[(x // flap) % len(frames)]
        show_eyes(text_window(font, message, x - left_eye_lag), text_window(font, message, x))
        time.sleep(delay)

def spider_drop(delay=0.1):
    """A spider drops down on its thread, bounces, and climbs back up"""
    spider = matrix_fonts.shapes['spider']

    def frame(y):
        return [spider[row - y] if 0 <= row - y < 4 else (0x10 if row < y else 0) for row in range(8)]

    for y in list(range(-4, 5)) + [3, 4, 3, 4]:
        show_eyes(frame(y), frame(y))
        time.sleep(delay)
    for y in range(4, -5, -1):
        show_eyes(frame(y), frame(y))
        time.sleep(delay * 2)

def bounce(steps=120, delay=0.04):
    """Pong: a ball bounces from eye to eye between two paddles"""
    width = 16 + EYE_GAP
    x, y, dx, dy = 2, random.randint(0, 6), 1, 1
    for _ in range(steps):
        x += dx
        y += dy
        if x <= 1 or x >= width - 3:
            dx = -dx
        if y <= 0 or y >= 6:
            dy = -dy
        left = [0, 0, 0, 0, 0, 0, 0, 0]
        right = [0, 0, 0, 0, 0, 0, 0, 0]
        paddle = min(max(y - 1, 0), 5)
        for row in range(paddle, paddle + 3):
            left[row] |= 0x80
            right[row] |= 0x01
        for col in (x, x + 1):
            for row in (y, y + 1):
                if col < 8:
                    left[row] |= 0x80 >> col
                elif col >= 8 + EYE_GAP:
                    right[row] |= 0x80 >> (col - 8 - EYE_GAP)
        show_eyes(left, right)
        time.sleep(delay)

def star_spin(turns=3, delay=0.08):
    """A star grows, spins round, then shrinks away"""
    S = matrix_fonts.shapes
    steps = [(S['star%d' % n], S['star%d' % n], 0.15) for n in (1, 2, 3, 4)]
    steps += [(S['star%d' % n], S['star%d' % n], delay) for n in range(5, 12)] * turns
    steps += [(S['star%d' % n], S['star%d' % n], 0.15) for n in (4, 3, 2, 1)]
    sequence(steps + [(S['empty'], S['empty'], 0.3)])

def cat_blink():
    """A cat face that blinks"""
    awake, shut = matrix_fonts.shapes['cat1'], matrix_fonts.shapes['cat2']
    sequence([(awake, awake, 1.2), (shut, shut, 0.15), (awake, awake, 0.8),
              (shut, shut, 0.15), (awake, awake, 0.15), (shut, shut, 0.15), (awake, awake, 1)])

def angry_eyes():
    """Cross, frowning eyes that shake with rage"""
    E = matrix_fonts.eyes
    left, right = E['angryLeft'], E['angryRight']
    sequence([(E['straight'], E['straight'], 0.5), (left, right, 1)])
    for i in range(8):
        if i % 2:
            show_eyes(left, right)
        else:
            show_eyes([0] + left[:7], [0] + right[:7])
        time.sleep(0.07)
    sequence([(left, right, 1.5), (E['straight'], E['straight'], 0.3)])

def sleepy_eyes():
    """Eyelids droop... and droop... fall asleep... then wake up with a jump"""
    E = matrix_fonts.eyes
    sequence([(E[name], E[name], seconds) for name, seconds in (
        ('straight', 0.8), ('sleepy1', 0.5), ('sleepy2', 0.8), ('sleepy3', 1), ('sleepy2', 0.3),
        ('sleepy3', 0.8), ('sleepy4', 2.5), ('straight', 0.8))])

def cross_eyed():
    """Both eyes look in at the nose"""
    E = matrix_fonts.eyes
    sequence([(E['straight'], E['straight'], 0.6), (E['right1'], E['left1'], 0.15),
              (E['right2'], E['left2'], 2), (E['right1'], E['left1'], 0.15),
              (E['straight'], E['straight'], 0.6), (E['right2'], E['left2'], 1.2),
              (E['straight'], E['straight'], 0.5)])

def creeper_explode():
    """A creeper stares, hisses and flashes... then BOOM"""
    S = matrix_fonts.shapes
    creeper, flash = S['creeper'], S['creeperFlash']
    show_eyes(creeper, creeper)
    time.sleep(1.5)
    print('SAY:Sssssssssssss')
    for i in range(10):
        face = flash if i % 2 else creeper
        show_eyes(face, face)
        time.sleep(0.3 - i * 0.025)
    print('SAY:BOOM!')
    explode()
    sequence([(S['empty'], S['empty'], 0.5)])

def enderman():
    """An Enderman stares out of one eye, then teleports from eye to eye, faster and faster"""
    face, blank = matrix_fonts.eyes['enderFace'], matrix_fonts.eyes['all_off']
    on_left = True
    for stay in (2, 1.5, 1, 0.7, 0.5):
        sequence([(face, blank, stay) if on_left else (blank, face, stay)])
        print('SAY:Whoosh!')
        sparkles(5, 3, left=on_left, right=not on_left)
        on_left = not on_left
        sparkles(5, 3, left=on_left, right=not on_left)
    sequence([(face, blank, 1.5) if on_left else (blank, face, 1.5)])

def roll_dice():
    """Roll two dice, one on each eye"""
    S = matrix_fonts.shapes
    delay = 0.05
    while delay < 0.4:
        a, b = random.randint(1, 6), random.randint(1, 6)
        show_eyes(S['dice%d' % a], S['dice%d' % b])
        time.sleep(delay)
        delay *= 1.25
    print('SAY:You rolled %d and %d. That makes %d!' % (a, b, a + b))
    time.sleep(3)

MAGIC_ANSWERS = ('Yes!', 'No!', 'Maybe...', 'Definitely!', 'No way!', 'Ask again', 'Of course!', 'I think so')

def magic_answer():
    """Think hard, then answer a question: yes, no, maybe..."""
    E = matrix_fonts.eyes
    sequence([(E['upLeft'], E['upLeft'], 0.5), (E['upRight'], E['upRight'], 0.5),
              (E['upLeft'], E['upLeft'], 0.5), (E['up1'], E['up1'], 0.6)])
    answer = random.choice(MAGIC_ANSWERS)
    print('SAY:' + answer)
    scroll_message(matrix_fonts.textFont1, ' ' + answer + ' ', 0.03)

def countdown_boo():
    """3, 2, 1... BOO! with a flash"""
    T, S = matrix_fonts.textFont1, matrix_fonts.shapes
    for digit, word in (('3', 'Three'), ('2', 'Two'), ('1', 'One')):
        print('SAY:' + word)
        sequence([(T[digit], T[digit], 1)])
    print('SAY:BOO!')
    max7219_eyes.set_brightness(min(MAX_BRIGHT + 4, 15))
    for i in range(6):
        flash = S['all_on'] if i % 2 == 0 else S['empty']
        sequence([(flash, flash, 0.08)])
    max7219_eyes.set_brightness(MAX_BRIGHT)
    scroll_message(T, ' BOO! ', 0.02)

def emerald_trade():
    """A villager trades: your emerald slides across, and a diamond comes back"""
    S = matrix_fonts.shapes
    villager, nod, blank = S['villager'], S['villagerNod'], S['empty']
    for _ in range(3):
        sequence([(villager, S['emerald1'], 0.3), (villager, S['emerald2'], 0.3)])
    print('SAY:Hrrmm?')
    slide((S['emerald1'], S['emerald2']), 8 + EYE_GAP, 8, left_bg=villager, delay=0.06)
    print('SAY:Hrrm! Hrrm!')
    for _ in range(3):
        sequence([(nod, blank, 0.15), (villager, blank, 0.15)])
    slide((S['diamond1'],), 8, 8 + EYE_GAP, left_bg=villager, delay=0.06)
    for _ in range(3):
        sequence([(villager, S['diamond1'], 0.3), (villager, S['diamond2'], 0.3)])

def bamboo_stalk(height):
    """A bamboo stalk with joints and a leaf on top, some rows tall"""
    rows = [0, 0, 0, 0, 0, 0, 0, 0]
    for row in range(8 - height, 8):
        rows[row] = 0x3c if row % 3 == 1 else 0x18
    if height:
        rows[8 - height] |= 0x06
    return rows

def panda():
    """A panda munches bamboo until it's all gone... then sneezes. ACHOO!"""
    S = matrix_fonts.shapes
    face, chew, squeeze, blank = S['panda1'], S['panda2'], S['pandaSqueeze'], S['empty']
    for height in range(8, -1, -1):
        bamboo = bamboo_stalk(height)
        sequence([(face, bamboo, 0.25), (chew, bamboo, 0.15)])
    print('SAY:Ah... ah... ACHOO!')
    sequence([(squeeze, blank, 0.6), (face, blank, 0.3), (squeeze, blank, 0.6)])
    sparkles(8, 2, left=face[1:] + [0])
    sequence([(face, blank, 1)])

CRACKS = ((3, 3), (4, 4), (4, 3), (2, 2), (5, 5), (2, 4), (5, 2), (1, 1),
          (6, 6), (1, 5), (6, 2), (3, 6), (4, 1), (0, 3), (7, 4))
MINING_FINDS = (('diamond', 'a diamond'), ('emerald', 'an emerald'), ('iron', 'some iron'), ('redstone', 'some redstone'))

def cracked_block(cracks):
    """A stone block with the first few cracks knocked out of it"""
    rows = [0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff]
    for row, col in CRACKS[:cracks]:
        rows[row] &= ~(0x80 >> col) & 0xff
    return rows

def mine():
    """A pickaxe chips away at a block until it breaks open... what's inside?"""
    S = matrix_fonts.shapes
    pick = S['pickaxe']
    swing = lower(place(pick, 2), 1)
    for hit in range(5):
        sequence([(pick, cracked_block(hit * 3), 0.35), (swing, cracked_block(hit * 3 + 3), 0.15)])
    sparkles(6, 2, left=pick)
    name, found = random.choice(MINING_FINDS)
    print('SAY:You found ' + found + '!')
    for _ in range(4):
        sequence([(pick, S[name + '1'], 0.3), (pick, S[name + '2'], 0.3)])

def dirt_pile(height):
    """A pile of dirt, some rows high"""
    rows = [0, 0, 0, 0, 0, 0, 0, 0]
    for i in range(height):
        rows[7 - i] = (0xff >> i) & (0xff << i) & 0xff
    return rows

def dig():
    """A shovel digs, and the pile of dirt grows with every scoop"""
    S = matrix_fonts.shapes
    shovel = S['shovel']
    for scoop in range(1, 5):
        pile = dirt_pile(scoop - 1)
        sequence([(shovel, pile, 0.3), (lower(shovel, 2), pile, 0.25)])
        slide((S['dirt'],), 0, 8 + EYE_GAP, left_bg=shovel, right_bg=pile, delay=0.015)
        sequence([(shovel, dirt_pile(scoop), 0.3)])
    sequence([(shovel, dirt_pile(4), 1)])

def chop_tree():
    """A sapling grows into a tree... then an axe chops it down. Timber!"""
    S = matrix_fonts.shapes
    tree, axe, blank = S['oak'], S['axe'], S['empty']
    sequence([(S[name], blank, 0.6) for name in ('sapling1', 'sapling2', 'sapling3')])
    sequence([(tree, blank, 0.8), (tree, axe, 0.8)])
    for _ in range(3):
        print('SAY:Chop!')
        sequence([(tree, place(axe, -1), 0.15), (place(tree, 1), axe, 0.25), (tree, axe, 0.3)])
    print('SAY:Timber!')
    for lean in (1, 2, 4, 7):
        sequence([([(row << (lean * (7 - r) // 7)) & 0xff for r, row in enumerate(tree)], axe, 0.12)])
    sequence([(S['fallenTree'], axe, 0.3)])
    sparkles(5, 3, right=axe)
    print('SAY:You got some wood!')
    sequence([(S['log'], axe, 2)])

def iron_golem():
    """An iron golem holds out a poppy for you... aww"""
    S = matrix_fonts.shapes
    golem, poppy = S['golem'], S['poppy']
    sequence([(golem, S['empty'], 1.5)])
    slide((poppy,), 8, 8 + EYE_GAP, left_bg=golem, delay=0.06)
    print("SAY:Here's a flower for you!")
    sequence([(golem, poppy, 1.5)])
    for _ in range(3):
        sequence([(golem, S['heart1'], 0.4), (golem, S['heart2'], 0.4)])
    sequence([(golem, poppy, 1)])

def redstone(tnt=False):
    """A redstone signal zooms along the wire... and switches on a lamp, or lights the TNT!"""
    S = matrix_fonts.shapes
    torch = S['torch']
    target = S['tnt'] if tnt else S['lampOff']
    far = 8 + EYE_GAP
    for pos in range(-3, far + 8):
        left_wire, right_wire = 0x55, 0x55
        for col in range(pos, pos + 3):
            if 0 <= col < 8:
                left_wire |= 0x80 >> col
            elif far <= col < far + 8:
                right_wire |= 0x80 >> (col - far)
        show_eyes(torch[:7] + [left_wire], target[:7] + [right_wire])
        time.sleep(0.03)
    if tnt:
        print('SAY:Sssssssss')
        for i in range(8):
            show_eyes(torch, S['tntFlash'] if i % 2 else S['tnt'])
            time.sleep(0.25)
        print('SAY:BOOM!')
        explode(left=torch)
        sequence([(torch, S['empty'], 0.5)])
    else:
        print('SAY:Ding! The lamp is on!')
        for i in range(10):
            glow = S['lampOn2'] if i % 2 else S['lampOn']
            show_eyes(torch[:7] + [0xff], glow[:7] + [0xff])
            time.sleep(0.2)

def ghast_fireball(shots=2):
    """A ghast wails and shoots fireballs across into the other eye"""
    S = matrix_fonts.shapes
    calm, angry = S['ghast1'], S['ghast2']
    for _ in range(shots):
        sequence([(calm, S['empty'], 1.2), (angry, S['empty'], 0.4)])
        print('SAY:Waaaaah!')
        slide((S['fireball1'], S['fireball2']), 8, 8 + EYE_GAP, left_bg=angry, delay=0.08)
        print('SAY:Boom!')
        explode(left=calm)
    sequence([(calm, S['empty'], 1)])

def skeleton_arrow():
    """A skeleton fires an arrow across into a target. Bullseye!"""
    S = matrix_fonts.shapes
    skeleton, target, arrow = S['skeleton'], S['target'], S['bowArrow']
    sequence([(skeleton, target, 1.2)])
    print('SAY:Twang!')
    slide((arrow,), 8, 8 + EYE_GAP - 3, left_bg=skeleton, right_bg=target, delay=0.04)
    print('SAY:Thwack! Bullseye!')
    for wobble in (arrow[1:] + [0], arrow, [0] + arrow[:7], arrow) * 2:
        show_eyes(skeleton, [a | b for a, b in zip(target, place(wobble, -3))])
        time.sleep(0.08)
    time.sleep(1.5)

def slime_bounce(hops=4):
    """A slime hops from eye to eye, squishing flat every time it lands"""
    S = matrix_fonts.shapes
    tall, flat = S['slime'], S['slimeFlat']
    far = 8 + EYE_GAP
    x = far
    for _ in range(hops):
        target = 0 if x else far
        for step, height in enumerate((0, 1, 2, 3, 3, 3, 2, 1, 0)):
            picture = lower(tall, 3 - height)
            at = x + (target - x) * step // 8
            show_eyes(place(picture, at), place(picture, at - far))
            time.sleep(0.05)
        x = target
        print('SAY:Squish!')
        sequence([(place(flat, x), place(flat, x - far), 0.2)])
    sequence([(place(lower(tall, 3), x), place(lower(tall, 3), x - far), 1)])

def day_and_night():
    """The sun sets in one eye while the moon rises in the other... then morning comes"""
    S = matrix_fonts.shapes
    sun, moon = S['sun'], S['moon']
    print('SAY:The sun is setting.')
    for k in range(9):
        show_eyes(lower(sun, k), lower(moon, 8 - k))
        time.sleep(0.25)
    print('SAY:Goodnight!')
    for _ in range(20):
        show_eyes([random_dots(4) for _ in range(8)], moon)
        time.sleep(0.15)
    print('SAY:Good morning!')
    for k in range(9):
        show_eyes(lower(sun, 8 - k), lower(moon, k))
        time.sleep(0.25)
    time.sleep(1)

def nether_portal():
    """A swirly Nether portal opens... and a ghast floats out of it"""
    def portal(t):
        rows = [0xff]
        for r in range(1, 7):
            inside = 0
            for c in range(1, 7):
                if (r + c + t) % 4 == 0:
                    inside |= 0x80 >> c
            rows.append(0x81 | inside)
        return rows + [0xff]

    for t in range(30):
        show_eyes(portal(t), portal(t + 2))
        time.sleep(0.08)
    ghast = matrix_fonts.shapes['ghast2']
    slide((ghast,), 0, 8 + EYE_GAP, left_bg=[0xff, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xff], delay=0.05)
    print('SAY:Waaaaah!')
    for t in range(20):
        show_eyes(portal(t), ghast)
        time.sleep(0.08)

def main():
    """Run main program"""
    log_message("Starting main.py")

    anims_JSON = load_anims('eyes_ani.json')
    loop_counter = 0

    # Main animation loop defined here
    while True:
        loop_counter += 1
        log_message(f">>> Starting loop {loop_counter}")

        gc.collect()

        # Opening show: Minecraft eyes, pumpkins, space invaders, Christmas trees and bunnies
        anim_runner(anims_JSON, "minecraft", matrix_fonts.eyes)
        flip(matrix_fonts.shapes['pumpkin1'], matrix_fonts.shapes['pumpkin2'], 6, 0.4)
        flip(matrix_fonts.shapes['invader1'], matrix_fonts.shapes['invader2'], 8, 0.3)
        flip(matrix_fonts.shapes['tree1'], matrix_fonts.shapes['tree1lit2'], 6, 0.4)
        flip(matrix_fonts.shapes['bunny1'], matrix_fonts.shapes['bunny3'], 8, 0.25)
        scroll_message(matrix_fonts.witch, " 12 ")  # Witch on a broomstick flying across the eyes

        show_char(matrix_fonts.eyes['ghost1'], matrix_fonts.eyes['ghost2'])
        time.sleep(1)
        anim_runner(anims_JSON, "stareAndBlink", matrix_fonts.eyes)
        scroll_message(matrix_fonts.textFont1, " Jack's Pumpkin ")

        show_char(matrix_fonts.shapes['heart1'], matrix_fonts.shapes['heart2'])
        time.sleep(0.5)
        show_char(matrix_fonts.shapes['heart2'], matrix_fonts.shapes['heart1'])
        time.sleep(0.5)
        show_char(matrix_fonts.shapes['heart1'], matrix_fonts.shapes['heart2'])
        time.sleep(0.5)

        anim_runner(anims_JSON, "winkLeft", matrix_fonts.eyes)
        
        scroll_message(
            matrix_fonts.textFont1,
            " Double, double toil and trouble" +
            " Fire burn and caldron bubble ",
            0.03
        )
        
        anim_runner(anims_JSON, "roll", matrix_fonts.eyes)
        gc.collect()

        show_char(matrix_fonts.shapes['invader1'], matrix_fonts.shapes['invader2'])
        time.sleep(0.5)
        show_char(matrix_fonts.shapes['invader2'], matrix_fonts.shapes['invader1'])
        time.sleep(0.5)
        show_char(matrix_fonts.shapes['invader1'], matrix_fonts.shapes['invader2'])
        time.sleep(0.5)

        anim_runner(anims_JSON, "downLeftABit", matrix_fonts.eyes)
        anim_runner(anims_JSON, "stareAndBlink", matrix_fonts.eyes)

        scroll_message(matrix_fonts.textFont1, " Trick or Treat? ", 0.02)

        anim_runner(anims_JSON, "roll", matrix_fonts.eyes)
        anim_runner(anims_JSON, "stareAndBlink", matrix_fonts.eyes)
        anim_runner(anims_JSON, "growEyes", matrix_fonts.eyes)

        # scroll_message(
        #     matrix_fonts.textFont1,
        #     " Come, you spirits " +
        #     "That tend on mortal thoughts! Unsex me here, " +
        #     "And fill me from the crown to the toe top full " +
        #     "Of direst cruelty; make thick my blood, " +
        #     "Stop up the access and passage to remorse ",
        #     0.03
        # )

        anim_runner(anims_JSON, "stareAndBlink", matrix_fonts.eyes)
        anim_runner(anims_JSON, "winkRight", matrix_fonts.eyes)
        anim_runner(anims_JSON, "stareAndBlink", matrix_fonts.eyes)
        scroll_message(matrix_fonts.textFont1, " Happy Halloween! ", 0.03)

# Run the thing
if __name__ == "__main__":
    main()
