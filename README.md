# pico-pumpkin

Here's modification of 8x8 led matrix eyes for a halloween pumpkin.

I removed support for i2c and the HT16k33 LED matrix in the process of making the code clearer for myself using
the MAX7219. I used these (Amazon UK) for the eyes: https://amzn.eu/d/e5Kw5vX

I fixed the issue mentioned in the video that the left and right eyes needed to be swapped to display scrolling messages.

I couldn't get the LED matrices to work from the tutorial, so I used the PIN connections shown in:
https://microcontrollerslab.com/max7219-led-dot-matrix-display-raspberry-pi-pico/

I also added a (common anode) RGB LED to light up the mouth, which you could comment out if you don't need.

For the RGB LED, I used one of these: https://amzn.eu/d/9fQ5G3V

Known issues:
I think some of the animations don't work e.g. winkLeft, winkRight.

[Instructions](https://gurgleapps.com/learn/projects/8x8-led-matrix-halloween-jack-o-lantern-pumpkin-project-with-a-pico)