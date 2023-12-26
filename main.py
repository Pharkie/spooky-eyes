"""
Written by: GurgleApps.com
Tool To Make Sprites https://gurgleapps.com/tools/matrix
Instructions
https://gurgleapps.com/learn/projects/8x8-led-matrix-halloween-jack-o-lantern-pumpkin-project-with-a-pico

Revised by: Adam Knowles
Version: 0.1
Description: Pumpkin Eyes 8x8 LED Matrix Halloween Project with a Raspberry Pi Pico
GitHub Repository: https://github.com/Pharkie/pico-pumpkin/
License: GNU General Public License (GPL)
"""

from machine import Pin, SPI
import time
import json
import random
import matrix_fonts
from max7219_matrix import max7219_matrix
from picozero import RGBLED

# SPI config
CS_PIN = 5

COLOUR_RED = (255, 0, 0)
COLOUR_GREEN = (0, 255, 0)
COLOUR_BLUE = (0, 0, 255)
COLOUR_YELLOW = (255, 255, 0)
COLOUR_MAGENTA = (255, 0, 255)
COLOUR_CYAN = (0, 255, 255)
COLOUR_WHITE = (255, 255, 255)
COLOUR_ORANGE = (255, 165, 0)
COLOUR_PURPLE = (128, 0, 128)
COLOUR_PINK = (255, 192, 203)

LED_COLOURS = [
    COLOUR_RED,
    COLOUR_GREEN,
    COLOUR_BLUE,
    COLOUR_YELLOW,
    COLOUR_MAGENTA,
    COLOUR_CYAN,
    COLOUR_WHITE,
    COLOUR_ORANGE,
    COLOUR_PURPLE,
    COLOUR_PINK
]

max7219_eyes = max7219_matrix(SPI(0, sck=Pin(2), mosi=Pin(3)), Pin(CS_PIN, Pin.OUT, True))

# Initiliase common anode RGB LED as a global variable
rgb_led = RGBLED(red = 19, green = 20, blue = 21, active_high = False)

def load_anims(file_name):
    """Load animations from json file"""
    data = {}
    try:
        with open(file_name, encoding='utf-8') as infile:
            data = json.load(infile)
    except FileNotFoundError as err:
        print('Oops problem loading JSON! File not found.')
    except json.JSONDecodeError as err:
        print('Oops problem loading JSON! Invalid JSON data.')

    return data

def anim_runner(anims, font):
    """Run animations"""
    for anim in anims:
        # Choose a new LED colour
        rgb_led.color = random.choice(LED_COLOURS)
        
        left = anim.get("l")
        right = anim.get("r")

        if left is not None and right is not None:
            max7219_eyes.show_char(font[left], font[right])

        brightness = anim.get("bl")

        if brightness is not None:
            max7219_eyes.set_brightness(brightness)

        delay = anim.get("d")

        if delay is not None:
            time.sleep(delay)

def show_char(left, right):
    """Show character"""
    max7219_eyes.show_char(left,right)

def scroll_message(font, message='hello', delay=0.04):
    """Scroll message"""
    left_message = '   ' + message
    right_message = message + '   '
    length = len(right_message)
    char_range = range(length - 1)

    for char_pos in char_range:
        right_left_char = font[right_message[char_pos]]
        right_right_char = font[right_message[char_pos + 1]]
        left_left_char = font[left_message[char_pos]]
        left_right_char = font[left_message[char_pos + 1]]

        for shift in range(8):
            rgb_led.color = random.choice(LED_COLOURS)
            left_bytes = [0, 0, 0, 0, 0, 0, 0, 0]
            right_bytes = [0, 0, 0, 0, 0, 0, 0, 0]

            for col in range(8):
                left_bytes[col] = left_bytes[col] | left_left_char[col] << shift
                left_bytes[col] = left_bytes[col] | left_right_char[col] >> 8 - shift
                right_bytes[col] = right_bytes[col] | right_left_char[col] << shift
                right_bytes[col] = right_bytes[col] | right_right_char[col] >> 8 - shift

            max7219_eyes.show_char(right_bytes, left_bytes)

            time.sleep(delay)

def main():
    """Run main program"""
    print(f"Running main()")
    anims = load_anims('eyes_ani.json')

    while True:
        show_char(matrix_fonts.eyes['ghost1'], matrix_fonts.eyes['ghost1'])
        time.sleep(1)
        anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
        scroll_message(matrix_fonts.textFont1, " Jack's Pumpkin woooOOOO.. ")
        anim_runner(anims['growEyes'],matrix_fonts.eyes)
        anim_runner(anims['roll'],matrix_fonts.eyes)
        anim_runner(anims['downLeftABit'],matrix_fonts.eyes)
        anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
        anim_runner(anims['downRightABit'],matrix_fonts.eyes)
        anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
        scroll_message(matrix_fonts.textFont1, ' Trick or Treat? ')
        anim_runner(anims['roll'],matrix_fonts.eyes)
        anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
        anim_runner(anims['growEyes'],matrix_fonts.eyes)
        scroll_message(matrix_fonts.textFont1, ' Spooky! ')
        anim_runner(anims['ghosts1'],matrix_fonts.eyes)
        anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
        anim_runner(anims['winkLeft'],matrix_fonts.eyes)
        anim_runner(anims['winkRight'],matrix_fonts.eyes)
        anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
        scroll_message(matrix_fonts.textFont1, ' Happy Halloween! ', 0.03)

# Run the thing
if __name__ == "__main__":
    main()