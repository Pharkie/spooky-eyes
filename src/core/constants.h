#pragma once
// Fixed settings. User-changeable ones (name, WiFi, brightness...) are in settings.h and live in NVS.

#include <cstddef>
#include <cstdint>

#define FIRMWARE_VERSION "2026.10.0"

namespace PumpkinTime
{
    constexpr uint32_t Seconds(uint32_t s) { return s * 1000UL; }
    constexpr uint32_t Minutes(uint32_t m) { return m * 60UL * 1000UL; }
}

// Setup WiFi network the pumpkin makes when it has no home WiFi (or can't find it), and its web address
// on the home WiFi (http://pumpkin.local). Both are fixed, not made from the pumpkin's name, so they're
// easy to remember and the instructions can print them. (Scribe Evolution names them after the device
// because a house can have several; a house has one pumpkin.)
constexpr const char *kSetupApName = "Spooky Pumpkin";
constexpr const char *kSetupApPassword = "spookyeyes";
constexpr const char *kHostname = "pumpkin";

// WiFi timings (from Scribe Evolution's network code)
// Joining can take 20 s or more: a mesh tries each of its access points in turn, and WPA3's handshake is
// slow on this little chip
constexpr uint32_t kWifiConnectTimeoutMs = PumpkinTime::Seconds(30);
constexpr uint32_t kWifiReconnectIntervalMs = PumpkinTime::Seconds(5);

// On home WiFi, after losing it for this long (carried out to the doorstep?), start the setup WiFi so
// phones can still reach the pumpkin. It goes back to the home WiFi once it's in range again.
constexpr uint32_t kLostWifiFallbackMs = PumpkinTime::Minutes(3);

// Hold BOOT this long to forget the home WiFi and go back to setup mode
constexpr uint32_t kForgetWifiHoldMs = PumpkinTime::Seconds(5);

// A short BOOT press unlocks the risky web actions (firmware update, and changing the WiFi while strangers
// could be on the setup WiFi) for this long. Proves someone has the pumpkin in their hands.
constexpr uint32_t kBootUnlockMs = PumpkinTime::Minutes(5);

// The setup instructions scroll once when the setup WiFi starts, then at the start of this many more
// show passes, and then the show plays on by itself: WiFi is optional, and the password shouldn't scroll
// at trick-or-treaters all night. A short BOOT press shows them again.
constexpr int kSetupMessageRepeats = 1;

// Saving batteries. With the "save batteries" setting on (the default), the pumpkin's own WiFi (setup, or
// the doorstep fallback) switches off after this long with nobody using it: no page or API request, no page
// open listening for news, and no phone on it. It can't doze between messages as the home WiFi does, so it
// draws several times more. On the home WiFi the pumpkin stays joined (dozing), so the page always works.
// A short BOOT press, or switching off and on, brings the WiFi back.
constexpr uint32_t kWifiIdleOffMs = PumpkinTime::Minutes(10);

// Processor speed. 80 MHz is the slowest that keeps WiFi (and USB serial) working, and draws a few mA
// less than the usual 160 MHz. The effects use a small fraction of it.
constexpr uint32_t kCpuMhz = 80;

// The mouth LED is lit for this much of the time (PWM), so it draws about a quarter of the current.
// Its pins drive it with no resistor, which can pull 10-20 mA per colour when fully on.
constexpr uint8_t kMouthDutyPercent = 25;

// A new firmware that runs this long without crashing is kept; one that crashes sooner is rolled back
constexpr uint32_t kFirmwareTrialMs = PumpkinTime::Seconds(30);

// Delay before a restart asked for from a web page, so the reply (and the page's next steps) get out
constexpr uint32_t kRestartDelayMs = PumpkinTime::Seconds(2);
// After saving the home WiFi: long enough for the reply to reach the phone (the setup page then waits for
// the pumpkin to answer on that WiFi and opens the buttons by itself), and no longer: people are waiting
constexpr uint32_t kWifiSavedRestartDelayMs = PumpkinTime::Seconds(3);

// Buttons mode lasts this long after the last press, or the last heartbeat from an open button page
// (which sends one every kButtonsHeartbeatMs while it's on screen); then it's back to the show
constexpr uint32_t kButtonsIdleTimeoutMs = PumpkinTime::Minutes(2);
constexpr uint32_t kButtonsHeartbeatMs = PumpkinTime::Seconds(30);

constexpr uint16_t kWebServerPort = 80;

// Defaults for a new pumpkin
constexpr const char *kDefaultPumpkinName = "Spooky Pumpkin";
constexpr uint8_t kDefaultBrightness = 2; // 0-15. Low is plenty in the dark, and kind to batteries.
constexpr uint8_t kDefaultEyeGap = 8;     // as on the original pumpkin
constexpr bool kDefaultMouthLed = true;
constexpr bool kDefaultWifiSaver = true;  // switch its own WiFi off when nobody's using it (kWifiIdleOffMs)

// Limits (the web pages and tools/mock_server.py read these too)
constexpr uint8_t kChipMaxBrightness = 15; // the MAX7219's top setting
constexpr uint8_t kMaxBrightness = 11;     // flashes go +4 above the normal brightness
constexpr uint8_t kMaxEyeGap = 16;
constexpr size_t kMaxNameLength = 24;      // scrolls past in a few seconds
constexpr size_t kMaxWordsLength = 200;    // "your own words" box
constexpr size_t kMaxSsidBytes = 32;       // WiFi standard
constexpr size_t kMinWifiPasswordLength = 8;  // WPA2: 8-63 characters, or exactly 64 hex digits
constexpr size_t kMaxWifiPasswordLength = 64;
constexpr size_t kMaxShowBlocks = 60;      // blocks in a saved show
