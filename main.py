"""
Written by: GurgleApps.com
Tool To Make Sprites https://gurgleapps.com/tools/matrix
Instructions
https://gurgleapps.com/learn/projects/8x8-led-matrix-halloween-jack-o-lantern-pumpkin-project-with-a-pico

Revised by: Adam Knowles
Version: 0.1
Description: Pumpkin Eyes 8x8 LED Matrix Halloween Project with an ESP32-C3
GitHub Repository: https://github.com/Pharkie/pico-pumpkin/
License: GNU General Public License (GPL)
"""

import machine
import time
import json
import random
import matrix_fonts
from max7219_matrix import max7219_matrix

# User-settable variables
SPI_BUS = 1  # Use SPI(1) for ESP32-C3
CLK_PIN = 6  
DIN_PIN = 7  
CS_PIN = 10  
RGB_LED_CONNECTED = True  # Set to False if RGB LED is not connected
# These pins aren't used if RGB_LED_CONNECTED is False
RED_PIN = 1
GREEN_PIN = 2
BLUE_PIN = 3

DEBUG = False  # Set to False to disable logging and printing

# Custom logging function
# Logs messages to a file with a timestamp
# mode='a' appends to the file, mode='w' overwrites the file
def log_message(message, mode='a'):
    if DEBUG:
        with open('log.txt', mode) as log_file:
            current_time = time.localtime()
            formatted_time = "{:04}-{:02}-{:02} {:02}:{:02}:{:02}".format(
                current_time[0], current_time[1], current_time[2],
                current_time[3], current_time[4], current_time[5]
            )
            log_file.write(f"{formatted_time} - {message}\n")

# Wipe the log file blank on each program run
log_message("Starting new log session", mode='w')

# Initialize SPI and MAX7219 matrix
log_message("Initializing SPI and MAX7219 matrix")
spi = machine.SPI(SPI_BUS, baudrate=10000000, polarity=0, phase=0, sck=machine.Pin(CLK_PIN), mosi=machine.Pin(DIN_PIN))
cs = machine.Pin(CS_PIN, machine.Pin.OUT)
max7219_eyes = max7219_matrix(spi, cs)
log_message("SPI and MAX7219 matrix initialized")

if RGB_LED_CONNECTED:
    # Define colours for RGB inner lights
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
        if DEBUG:
            print('Oops problem loading JSON! File not found.')
    except json.JSONDecodeError:
        if DEBUG:
            print('Oops problem loading JSON! Invalid JSON data.')

    log_message("Loaded animations from JSON file")

    return data

def anim_runner(anims, font):
    """Run animations"""

    if DEBUG:
        print("Running next anim_runner()")
        log_message("Running next anim_runner()")

    for anim in anims:
        # Choose a new LED colour if RGB LED is connected
        if RGB_LED_CONNECTED:
            color = random.choice(LED_COLOURS)
            set_rgb_color(*color)
        
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
            if RGB_LED_CONNECTED:
                color = random.choice(LED_COLOURS)
                set_rgb_color(*color)
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
    if DEBUG:
        print("Running main()")
        log_message("Starting main.py")
    
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
    