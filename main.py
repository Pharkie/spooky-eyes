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
SPI_BUS = 1  # Use SPI(1) for ESP32-C3
CLK_PIN = 6
DIN_PIN = 7
CS_PIN = 10

MAX_BRIGHT = 2  # Set global max brightness 0-15

RGB_LED_CONNECTED = True  # Set to False if RGB LED is not connected
# These pins aren't used if RGB_LED_CONNECTED is False
RED_PIN = 1
GREEN_PIN = 2
BLUE_PIN = 3

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
    COLOUR_PINK = (255, 192, 203)

    LED_COLOURS = [
        COLOUR_RED,
        COLOUR_GREEN,
        COLOUR_MAGENTA,
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

def scroll_message(font, message, delay=0.04):
    """Scroll message"""
    log_message(f"scroll_message() with message: {message}")

    left_eye_message = "     " + message
    right_eye_message = message + "     "
    length = len(right_eye_message)
    char_range = range(length-1)

    for char_pos in char_range:
        right_left_char = font[right_eye_message[char_pos]]
        right_right_char = font[right_eye_message[char_pos + 1]]
        left_left_char = font[left_eye_message[char_pos]]
        left_right_char = font[left_eye_message[char_pos + 1]]

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
    log_message("Starting main.py")

    anims_JSON = load_anims('eyes_ani.json')
    loop_counter = 0

    # Main animation loop defined here
    while True:
        loop_counter += 1
        log_message(f">>> Starting loop {loop_counter}")

        gc.collect()
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
        anim_runner(anims_JSON, "growEyes", matrix_fonts.eyes)
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
        anim_runner(anims_JSON, "downRightABit", matrix_fonts.eyes)
        anim_runner(anims_JSON, "stareAndBlink", matrix_fonts.eyes)

        scroll_message(matrix_fonts.textFont1, " Trick or Treat? ", 0.02)

        anim_runner(anims_JSON, "roll", matrix_fonts.eyes)
        anim_runner(anims_JSON, "stareAndBlink", matrix_fonts.eyes)
        anim_runner(anims_JSON, "growEyes", matrix_fonts.eyes)

        scroll_message(matrix_fonts.textFont1, ' Spooky! ')

        anim_runner(anims_JSON, "stareAndBlink", matrix_fonts.eyes)
        anim_runner(anims_JSON, "winkRight", matrix_fonts.eyes)
        anim_runner(anims_JSON, "stareAndBlink", matrix_fonts.eyes)
        scroll_message(matrix_fonts.textFont1, " Happy Halloween! ", 0.03)

# Run the thing
if __name__ == "__main__":
    main()
