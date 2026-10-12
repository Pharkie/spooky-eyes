#pragma once
// WiFi, ported from Scribe Evolution (src/core/network.cpp there):
// - with home WiFi saved: scan for it, connect, register name.local (mDNS)
// - with none saved, or it can't be found / joined: start the pumpkin's own WiFi (AP+STA) with a
//   captive-portal DNS server, so a phone that joins gets the setup page
// - a background task keeps the DNS server answering, reconnects after dropouts, blinks the
//   status LED, and watches the BOOT button (hold 5 s = forget WiFi)
// - in setup mode, that task can check a home WiFi's password before the page saves it (startWifiTest())
//
// Threads: begin() runs on the Arduino loop task and returns within about a second, so the show starts at
// switch-on. Everything slow happens on the network task (priority 2): first the joining (a ~3 s scan, up
// to 15 s to join and a ~7 s check that the web name is free; or the setup WiFi), then the housekeeping
// above. The mode listener is called on that task too. Everything below is safe to call from any task
// once begin() has returned, including while it's still connecting: mode() says Connecting and
// ipAddress(), mdnsHostname() and setupReason() are "" until there's something to say. apSsid(),
// homeSsid() and hasSavedWifi() come from the settings at boot, so they're right from the start.

#include <Arduino.h>
#include <vector>

namespace network
{
    enum class Mode : uint8_t
    {
        Connecting, // starting up, or trying the home WiFi
        Connected,  // on the home WiFi
        SetupAP,    // running its own WiFi network (no home WiFi, or couldn't reach it)
        Disconnected,
    };

    // Start the WiFi and the network task, which joins the home WiFi (or starts the setup WiFi) in the
    // background. Quick: it doesn't wait for the joining. Call after settings::begin() and onModeChange(),
    // and before web::begin() (the web server needs the network stack, which this starts).
    void begin();

    Mode mode();
    bool isSetupAP();

    // True if home WiFi is saved (so SetupAP means "couldn't reach it", not "never set up")
    bool hasSavedWifi();

    String ipAddress();   // home WiFi address, 192.168.4.1 in setup mode, or "" (connecting, no WiFi)
    String mdnsHostname(); // registered name without ".local", or "" if mDNS isn't running (yet)
    String apSsid();      // the setup WiFi's name
    String homeSsid();    // the home WiFi it's on (or trying)
    String setupReason(); // why it's in setup mode with home WiFi saved, e.g. "Couldn't join \"Home\" — wrong password?"
    bool homeWifiOutOfReach(); // in setup mode because the home WiFi wasn't found at boot, or was lost later
    String apNameFor(const String &pumpkinName); // the setup WiFi's name for this pumpkin name
    int rssi();           // home WiFi signal strength in dBm (0 if not connected)

    // Called on the network task so the eyes can say how to reach the pumpkin. First when the joining at
    // boot is done: Connected (once the web name is registered, so mdnsHostname() is ready), SetupAP, or
    // Disconnected if no WiFi would start. Then whenever that changes, e.g. home WiFi lost for
    // kLostWifiFallbackMs -> SetupAP. Set it before begin(), or the first call can be missed. Keep it
    // quick: the captive DNS and the BOOT button wait while it runs.
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

    // --- Checking a home WiFi before it's saved (the setup page's "Check and save") ---
    //
    // From Scribe Evolution's handleTestWiFi(), but run on the network task so the web server never waits.

    enum class WifiTestStatus : uint8_t
    {
        Idle,    // no check lately
        Running, // asked for or under way: ask again in a moment
        Passed,  // joined it: rssi says how strong
        Failed,  // couldn't: reason says why, in words for the family
    };

    struct WifiTest
    {
        WifiTestStatus status;
        String ssid;   // the WiFi checked (never the password)
        int32_t rssi;  // signal in dBm when Passed, otherwise 0
        String reason; // when Failed, e.g. "That password didn't work"
    };

    enum class WifiTestStart : uint8_t
    {
        Started,
        Busy,       // a check is already running
        NotInSetup, // not running the setup WiFi (on home WiFi, say): a check would drop the connection
    };

    // Setup mode only: ask the network task to join this WiFi on the station side while the setup WiFi
    // stays up, then leave it again. It takes up to about 12 s; ask wifiTest() how it went. Nothing is
    // saved, and the password is wiped from memory once the check starts. Safe from any task; never
    // waits for the check.
    WifiTestStart startWifiTest(const String &ssid, const String &password);

    // The latest check. A finished one is kept for a minute (a phone that dropped off the setup WiFi
    // while the radio changed channel can still ask), then it's Idle again.
    WifiTest wifiTest();
}
