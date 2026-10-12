#pragma once
// WiFi, ported from Scribe Evolution (src/core/network.cpp there):
// - with home WiFi saved: scan for it, connect, register name.local (mDNS)
// - with none saved, or it can't be found / joined: start the pumpkin's own WiFi (AP+STA) with a
//   captive-portal DNS server, so a phone that joins gets the setup page
// - a background task keeps the DNS server answering, reconnects after dropouts, blinks the
//   status LED, and watches the BOOT button (hold 5 s = forget WiFi)

#include <Arduino.h>
#include <vector>

namespace network
{
    enum class Mode : uint8_t
    {
        Connecting, // trying the home WiFi
        Connected,  // on the home WiFi
        SetupAP,    // running its own WiFi network (no home WiFi, or couldn't reach it)
        Disconnected,
    };

    // Connect (blocks for up to ~25 s: a ~3 s scan, a 15 s join and a ~7 s web name check), then start
    // the background task. Call after settings::begin().
    void begin();

    Mode mode();
    bool isSetupAP();

    // True if home WiFi is saved (so SetupAP means "couldn't reach it", not "never set up")
    bool hasSavedWifi();

    String ipAddress();   // home WiFi address, or 192.168.4.1 in setup mode
    String mdnsHostname(); // registered name without ".local", or "" if mDNS isn't running
    String apSsid();      // the setup WiFi's name
    String homeSsid();    // the home WiFi it's on (or trying)
    String setupReason(); // why it's in setup mode with home WiFi saved, e.g. "Couldn't join \"Home\" — wrong password?"
    bool homeWifiOutOfReach(); // in setup mode because the home WiFi wasn't found at boot, or was lost later
    String apNameFor(const String &pumpkinName); // the setup WiFi's name for this pumpkin name
    int rssi();           // home WiFi signal strength in dBm (0 if not connected)

    // Called from the network task when the mode changes after begin(), e.g. home WiFi lost for
    // kLostWifiFallbackMs -> SetupAP, so the eyes can say how to reach the pumpkin now
    using ModeListener = void (*)(Mode mode);
    void onModeChange(ModeListener listener);

    // --- WiFi scanning for the setup page (all scans go through here, one at a time) ---

    struct FoundNetwork
    {
        String ssid;
        int32_t rssi;
        bool secure;
        uint8_t channel;
    };

    enum class ScanStatus : uint8_t
    {
        Running, // started or still going: ask again in a second
        Ready,   // `found` holds the networks, strongest first, one entry per name
        Failed,
    };

    // Start a scan if none is running (safe to call from the web server task; never blocks)
    ScanStatus scan(std::vector<FoundNetwork> &found);
}
