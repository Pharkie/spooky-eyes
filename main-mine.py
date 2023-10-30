"""
Written by: GurgleApps.com
Tool To Make Sprites https://gurgleapps.com/tools/matrix
Full Instructions
https://gurgleapps.com/learn/projects/8x8-led-matrix-halloween-jack-o-lantern-pumpkin-project-with-a-pico

Revised by: Adam Knowles
Version: 0.1
Description: Pumpkin Eyes 8x8 LED Matrix Halloween Project with a Raspberry Pi Pico
GitHub Repository: https://github.com/Pharkie/pico-pumpkin/
License: GNU General Public License (GPL)
"""

import time
import json
import machine  # pylint: disable=import-error # type: ignore

import matrix_fonts
from max7219_matrix import max7219_matrix

# SPI config
CS_PIN = 5

USE_MAX7219 = True
USE_I2C = False

max7219_eyes = max7219_matrix(machine.SPI(0), machine.Pin(CS_PIN, machine.Pin.OUT, True))
print(f"max7219_eyes: {max7219_eyes}")

def load_anims(file_name):
    """Load animations from json file"""
    data={}
    try:
        with open(file_name, encoding='utf-8') as infile:
            data=json.load(infile)
    except FileNotFoundError as err:
        print('Oops problem loading JSON! File not found.')
        print (err)
    except json.JSONDecodeError as err:
        print('Oops problem loading JSON! JSON decode error.')
        print (err)

    print(f"Loaded {len(data)} animations from {file_name}")

    return data

def anim_runner(anim, font):
    """Run an animation"""

    for i in anim:
        eye.show_char(font[i["l"]])

        if "r" in i:
            if USE_MAX7219:
                max7219_eyes.show_char(font[i["l"]],font[i["r"]])

        if "bl" in i:

            if USE_MAX7219:
                max7219_eyes.set_brightness(i["bl"])

        time.sleep(i["d"])

def show_char(left, right):
    """Show character"""
    if USE_MAX7219:
        max7219_eyes.show_char(left,right)

    if USE_I2C:
        left_eye.show_char(left)
        right_eye.show_char(right)

def scroll_message(font,message='hello',delay=0.1):
    """Scroll message"""
    left_message = '   ' + message
    right_message = message + '   '
    length=len(right_message)
    char_range=range(length-1)

    for char_pos in char_range:
        right_left_char=font[right_message[char_pos]]
        right_right_char=font[right_message[char_pos+1]]
        left_left_char=font[left_message[char_pos]]
        left_right_char=font[left_message[char_pos+1]]

        for shift in range(8):
            left_bytes = [0,0,0,0,0,0,0,0]
            right_bytes = [0,0,0,0,0,0,0,0]

        for col in range(8):
            left_bytes[col]=left_bytes[col]|left_left_char[col]<<shift
            left_bytes[col]=left_bytes[col]|left_right_char[col]>>8-shift
            right_bytes[col]=right_bytes[col]|right_left_char[col]<<shift
            right_bytes[col]=right_bytes[col]|right_right_char[col]>>8-shift

        if USE_MAX7219:
            max7219_eyes.show_char(left_bytes,right_bytes)

        if USE_I2C:
            left_eye.show_char(left_bytes)
            right_eye.show_char(right_bytes)

        time.sleep(delay)

def main():
    """Run main program"""
    print(f"Running main()")
    anims = load_anims('eyes_ani.json')

    max7219_eyes.show_char(matrix_fonts.eyes['ghost1'], matrix_fonts.eyes['ghost1'])

    # while True:
    #     show_char(matrix_fonts.eyes['ghost1'], matrix_fonts.eyes['ghost1'])
    #     time.sleep(1)
    #     scroll_message(matrix_fonts.textFont1, ' Spooky ')
    #     anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
    #     anim_runner(anims['growEyes'],matrix_fonts.eyes)
    #     anim_runner(anims['roll'],matrix_fonts.eyes)
    #     anim_runner(anims['downLeftABit'],matrix_fonts.eyes)
    #     anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
    #     anim_runner(anims['downRightABit'],matrix_fonts.eyes)
    #     anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
    #     scroll_message(matrix_fonts.textFont1, ' Trick or Treat ')
    #     anim_runner(anims['roll'],matrix_fonts.eyes)
    #     anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
    #     anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
    #     anim_runner(anims['growEyes'],matrix_fonts.eyes)
    #     anim_runner(anims['roll'],matrix_fonts.eyes)
    #     anim_runner(anims['ghosts1'],matrix_fonts.eyes)
    #     anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)
    #     anim_runner(anims['winkLeft'],matrix_fonts.eyes)
    #     anim_runner(anims['winkRight'],matrix_fonts.eyes)
    #     anim_runner(anims['stareAndBlink'],matrix_fonts.eyes)

if __name__ == "__main__":
    main()
