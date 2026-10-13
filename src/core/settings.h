#pragma once
// User settings, saved in NVS (flash) so they survive power cuts. Thread-safe: the web server task
// writes, the effects task reads, so callers always get a copy. save() is the one place settings are
// checked: callers pass on its error message rather than checking for themselves. Limits are in
// constants.h; text rules are in text.h.

#include <Arduino.h>

struct Settings
{
    String pumpkinName;  // e.g. "Jack's Pumpkin": scrolled in the show, names the setup WiFi and the web address
    String wifiSsid;     // home WiFi name; empty = not set up yet
    String wifiPassword;
    uint8_t brightness;  // normal eye brightness 0..kMaxBrightness
    uint8_t eyeGap;      // 0..kMaxEyeGap
    bool mouthLed;       // drive the RGB mouth LED
    bool wifiSaver;      // switch the WiFi off when nobody's using it (kWifiIdleOffMs), to save batteries
};

namespace settings
{
    // Load from NVS, filling in defaults for anything not saved yet. Call once at boot.
    void begin();

    // A copy of the current settings
    Settings get();

    // Check and save. Returns false (and saves nothing) if a value is out of range; error says why.
    bool save(const Settings &updated, String &error);

    // save()'s rules for a home WiFi name and password, for checking one before it's saved (the setup
    // page's WiFi check). False if save() would refuse them; error says why.
    bool checkWifi(const String &ssid, const String &password, String &error);

    // Clear the home WiFi name and password (the pumpkin goes back to setup mode after a restart)
    void forgetWifi();
}
