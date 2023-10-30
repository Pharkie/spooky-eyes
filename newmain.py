from machine import Pin, SPI
import max7219
from time import sleep

spi = SPI(0,sck=Pin(2),mosi=Pin(3))
cs = Pin(5, Pin.OUT)

display = max7219.Matrix8x8(spi, cs, 4)

display.brightness(10)

while True:

    display.fill(0)
    display.text('PICO',0,0,1)
    display.show()
    sleep(1)

    display.fill(0)
    display.text('1234',0,0,1)
    display.show()
    sleep(1)

    display.fill(0)
    display.text('done',0,0,1)
    display.show()
    sleep(1)