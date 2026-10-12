#include "hardware/max7219_eyes.h"

#include <Arduino.h>
#include <SPI.h>
#include "hardware/board.h"

namespace
{
    // MAX7219 registers, as in max7219_matrix.py. Rows are digit registers 1-8, top row first.
    constexpr uint8_t kRegDigit0 = 1;
    constexpr uint8_t kRegDecodeMode = 9;
    constexpr uint8_t kRegIntensity = 10;
    constexpr uint8_t kRegScanLimit = 11;
    constexpr uint8_t kRegShutdown = 12;
    constexpr uint8_t kRegDisplayTest = 15;

    constexpr uint8_t kTopBrightness = 15;

    uint8_t brightness = 0; // last level sent, so reinit() can send it again
    bool started = false;

    SPISettings spiSettings()
    {
        return SPISettings(kEyesSpiHz, MSBFIRST, SPI_MODE0);
    }

    // One CS frame: a register write for each chip. The first pair shifted out ends up in the chip
    // furthest along the chain, the second pair in the chip wired to the board.
    void sendPair(uint8_t farCommand, uint8_t farData, uint8_t nearCommand, uint8_t nearData)
    {
        alignas(4) uint8_t packet[4] = {farCommand, farData, nearCommand, nearData};
        digitalWrite(kEyesCsPin, LOW);
        SPI.writeBytes(packet, sizeof packet);
        digitalWrite(kEyesCsPin, HIGH); // the chips latch their data as CS goes high
    }

    // write(command, data) in max7219_matrix.py: the same command to both chips
    void write(uint8_t command, uint8_t data)
    {
        SPI.beginTransaction(spiSettings());
        sendPair(command, data, command, data);
        SPI.endTransaction();
    }

    // show_char(cl, cr) in max7219_matrix.py. cr goes out first (far chip), cl second (near chip),
    // so show_char(a, b) puts a on the viewer's RIGHT eye.
    void showChar(const Frame &cl, const Frame &cr)
    {
        SPI.beginTransaction(spiSettings());
        for (uint8_t i = 0; i < 8; i++)
            sendPair(kRegDigit0 + i, cr[i], kRegDigit0 + i, cl[i]);
        SPI.endTransaction();
    }

    // setup() in max7219_matrix.py, plus the brightness
    void sendSetup()
    {
        write(kRegShutdown, 0);
        write(kRegDisplayTest, 0);
        write(kRegScanLimit, 7);
        write(kRegDecodeMode, 0);
        write(kRegIntensity, brightness);
        write(kRegShutdown, 1);
    }
}

namespace max7219_eyes
{
    void begin()
    {
        if (started)
            return;

        // CS idles high. Set the level before making it an output so the chips never see a stray low.
        digitalWrite(kEyesCsPin, HIGH);
        pinMode(kEyesCsPin, OUTPUT);

        // The C3 has one general-purpose SPI bus (FSPI), which the global SPI object uses. The MAX7219
        // never sends anything back, but SPIClass always attaches a MISO pin and logs a misleading
        // error at boot for -1 on the C3. So MISO is given DIN too: arduino-esp32 2.0 attaches MISO
        // first and MOSI second, which leaves DIN as an output; the MISO side is never read.
        SPI.begin(kEyesClkPin, kEyesDinPin, kEyesDinPin, -1);
        started = true;

        // Power-up leaves random dots in the chips: blank them before the display comes on
        write(kRegShutdown, 0);
        showChar(kBlank, kBlank);
        sendSetup();
    }

    void reinit()
    {
        if (!started)
            return;
        sendSetup();
    }

    void show(const Frame &left, const Frame &right)
    {
        if (!started)
            return;
        showChar(right, left); // show_eyes(left, right) in main.py
    }

    void setBrightness(uint8_t level)
    {
        brightness = level > kTopBrightness ? kTopBrightness : level;
        if (started)
            write(kRegIntensity, brightness);
    }
}
