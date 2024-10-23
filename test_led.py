import machine
import time

# Define GPIO pins for the RGB LED

# Pico specific pins
# RED_PIN = 14
# GREEN_PIN = 15
# BLUE_PIN = 16

# ESP32-C3 specific pins
RED_PIN = 1
GREEN_PIN = 2
BLUE_PIN = 3

# Initialize the pins
red = machine.Pin(RED_PIN, machine.Pin.OUT)
green = machine.Pin(GREEN_PIN, machine.Pin.OUT)
blue = machine.Pin(BLUE_PIN, machine.Pin.OUT)

def set_rgb_color(r, g, b):
    """Set the color of the RGB LED"""
    red.value(not r)
    green.value(not g)
    blue.value(not b)

def cycle_rgb_led():
    """Cycle the RGB LED through red, green, and blue colors"""
    while True:
        # Red
        print("Red")
        set_rgb_color(1, 0, 0)
        time.sleep(1)
        # Green
        print("Green")
        set_rgb_color(0, 1, 0)
        time.sleep(1)
        # Blue
        print("Blue")
        set_rgb_color(0, 0, 1)
        time.sleep(1)

# Run the test
if __name__ == "__main__":
    cycle_rgb_led()