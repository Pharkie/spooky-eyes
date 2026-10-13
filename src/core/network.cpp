// WiFi, closely ported from Scribe Evolution's src/core/network.cpp:
//   connectToWiFi()          -> connectHome(): look for the home WiFi first, then join with a timeout
//   startFallbackAP()        -> startSetupAP(): AP+STA (so the setup page can scan) plus captive DNS
//   setupmDNS()              -> startMdns(): ping each candidate name, then claim the first free one
//   handleWiFiReconnection() -> keepConnected(), now without blocking
//   handleDNSServer() and updateStatusLED(), which Scribe called from loop() -> networkTask()
//
// Changes for the pumpkin:
// - begin() returns at once and the joining happens at the start of the network task (connectAtStart()),
//   so the show starts the moment the pumpkin is switched on. Scribe connected in setup(), before
//   anything else ran. The mode listener hears how it went.
// - fixed names for the setup WiFi and the web address ("Spooky Pumpkin", pumpkin.local), where Scribe
//   makes them from the device's name: a house has one pumpkin, and the guide can print them
// - the transmit power is turned down for the SuperMini's tiny antenna (kTxPower)
// - the look for the home WiFi is an active scan for its name, so a hidden network is found too
// - in setup mode the WiFi driver's own auto-reconnect is switched off, so the radio stays on the setup
//   WiFi's channel and phones aren't dropped (Scribe left it running after a failed join)
// - our own settings hold the WiFi details; the driver keeps no copy in flash
// - our own captive DNS (captive_dns.cpp) in place of Arduino's DNSServer
// - home WiFi lost for kLostWifiFallbackMs (carried out to the doorstep?): start the setup WiFi, so
//   phones can still reach the pumpkin
// - in setup mode with home WiFi saved (it was out of range at boot, or lost later), look for it now
//   and then, and restart to join it once it's back. At most kMaxRetryRestarts in a row. Not after a
//   wrong password: that won't fix itself, so it waits for the family to type it again.
// - every WiFi scan, the setup page's included, goes through scanMutex, with the scan settings zeroed
//   first (startScan(): arduino 2.0.17 leaves some uninitialised, e.g. how long to spend back on the
//   setup WiFi's channel between channels)
// - joins go straight to the access point a scan found (its channel and BSSID: no second, slower scan
//   by the driver), allow WPA3's newer handshake, run the processor at full speed, allow 30 s (a mesh tries
//   its access points one by one), and log every step (onWifiEvent())
// - Scribe's handleTestWiFi() (check the password before saving it) runs here on the network task,
//   under scanMutex, while the web server only asks and polls (runWifiTest())
// - nobody has used the pumpkin for kWifiIdleOffMs: switch the WiFi off to save batteries, until a
//   restart (saveBatteries())
// - the country is set to GB, channels 1-13, so routers on 12 and 13 are found
// - a brand-new pumpkin starts its setup WiFi on the channel of the strongest network around (most likely
//   the home WiFi), so checking the home WiFi rarely moves it to another channel and drops phones
// - no check that pumpkin.local is free (Scribe pinged each name first): a house has one pumpkin
// - left out: printAPDetailsOnStartup() (the eyes scroll the address instead), MQTT, NTP, LogManager and
//   the custom PCB's RGB status LED

#include "core/network.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <algorithm>
#include <atomic>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "core/captive_dns.h"
#include "core/constants.h"
#include "core/firmware.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/wifi_saver.h"
#include "hardware/boot_button.h"
#include "hardware/status_led.h"

using network::FoundNetwork;
using network::Mode;
using network::ScanStatus;

namespace
{
    // ESP32-C3 SuperMini boards have a tiny, poorly matched chip antenna. At the default full transmit
    // power (19.5 dBm) the signal distorts and many boards can't join a network at all, or keep dropping
    // off. 8.5 dBm is the widely reported fix, and still reaches across a house. It's set again after
    // every WiFi.mode() change in case the driver resets it.
    constexpr wifi_power_t kTxPower = WIFI_POWER_8_5dBm;

    // Scribe's scan tuning: 120 ms per channel, about 2 s in all
    constexpr uint32_t kScanMsPerChannel = 120;
    constexpr int kBootScanAttempts = 2; // a busy channel can hide a reply, so look twice before giving up
    // The look for the home WiFi gives up after this long, as the library's own wait does
    constexpr uint32_t kHomeScanGiveUpMs = PumpkinTime::Seconds(10);
    // The setup page's scan. The library gives up after 20 x kScanMsPerChannel, which can come before
    // the scan really ends while the setup WiFi is on (the radio keeps hopping back to its channel), so
    // we keep asking for longer than that.
    constexpr uint32_t kPageScanGiveUpMs = PumpkinTime::Seconds(10);
    constexpr uint32_t kPageScanKeepMs = PumpkinTime::Seconds(10); // hand the same results out again
    constexpr size_t kMaxFoundNetworks = 30;

    constexpr int kSetupApAttempts = 3;
    constexpr uint32_t kSetupApRetryMs = 500;

    // Network task: see networkTask() for why these values
    constexpr uint32_t kTaskStackBytes = 8192;
    constexpr UBaseType_t kTaskPriority = 2;
    constexpr uint32_t kTaskPeriodMs = 10;
    constexpr uint32_t kStackReportMs = PumpkinTime::Minutes(1);

    // Home WiFi lost: tell the mode listener once it has been gone this long, so a blip the driver
    // fixes in a second or two doesn't make the eyes scroll anything
    constexpr uint32_t kLostNoticeMs = PumpkinTime::Seconds(30);
    // On the home WiFi this long: the restarts that got it back worked, so the count starts again
    constexpr uint32_t kHomeSettledMs = PumpkinTime::Minutes(10);
    // Setup mode with home WiFi saved: how often to look for it (only while no phone is on the setup WiFi)
    constexpr uint32_t kHomeRecheckMs = PumpkinTime::Minutes(3);
    // The setup WiFi wouldn't start: restart and try everything again after this long
    constexpr uint32_t kBrokenRestartMs = PumpkinTime::Minutes(1);
    // Most restarts in a row for either of those. Then it stays put until switched off and on, so a
    // pumpkin on a doorstep at the edge of the WiFi doesn't keep interrupting its show.
    constexpr uint32_t kMaxRetryRestarts = 3;

    // Checking a home WiFi for the setup page (runWifiTest()). A right password usually joins in 3-10 s
    // (with the setup WiFi on, the radio keeps going back to its channel), but a mesh or a WPA3 router can
    // take 20 s or more: a check that saw neither a join nor a refusal in time is "unsure", not a failure.
    // A wrong password is turned down after about 4 s, and the library tries once more if it's its first
    // join.
    constexpr uint32_t kWifiTestMaxMs = PumpkinTime::Seconds(30);
    // After a refusal, how long to wait for that one extra try before calling it a failure
    constexpr uint32_t kWifiTestSettleMs = PumpkinTime::Seconds(6);
    // After leaving a WiFi, before its details are cleared out of the driver (which refuses mid-join)
    constexpr uint32_t kLeaveSettleMs = 200;
    // A finished check is kept this long, for a phone that dropped off the setup WiFi meanwhile
    constexpr uint32_t kWifiTestKeepMs = PumpkinTime::Minutes(1);

    // Processor speed while joining: the password maths (WPA2's key derivation, WPA3's handshake) is
    // twice as quick as at kCpuMhz. The peripherals' clock stays 80 MHz either way.
    constexpr uint32_t kJoinCpuMhz = 160;

    // How often to ask whether it's time to switch the WiFi off (saveBatteries())
    constexpr uint32_t kSaverCheckMs = PumpkinTime::Seconds(1);

    // What the network task looks after
    enum class Role : uint8_t
    {
        Starting, // the network task is still connecting (connectAtStart()): no scans for the setup page yet
        Home,     // joined the home WiFi at boot: keep it connected
        Setup,    // running the setup WiFi: answer DNS
        Broken,   // the setup WiFi wouldn't start
        Off,      // WiFi switched off to save batteries, until a restart
        Offline,  // never started (beginOffline(), after a brownout): the housekeeping only
    };

    // Why it's in setup mode with home WiFi saved
    enum class Reason : uint8_t
    {
        None,
        NotFound,      // not seen at boot
        WrongPassword, // turned us away during the boot join
        JoinFailed,    // seen, but joining didn't work for some other reason
        Lost,          // was on it, then lost it for kLostWifiFallbackMs
    };

    std::atomic<Mode> currentMode{Mode::Connecting};
    std::atomic<Role> role{Role::Starting};
    std::atomic<Reason> reason{Reason::None};
    std::atomic<uint8_t> lastDisconnectReason{0}; // from the WiFi event task
    std::atomic<bool> sawAuthFailure{false};      // ditto: sticky, cleared when a join starts
    std::atomic<uint8_t> refusalCount{0};         // ditto: goes up with each failed join (wraps round)
    std::atomic<network::ModeListener> modeListener{nullptr};
    std::atomic<uint32_t> lastActivityMs{0};      // millis() when someone last used the pumpkin
    uint8_t phonesOnSetupWifi = 0;                // last count logged (network task only)

    // Set by begin() before it starts the network task, then only read (from any task)
    String savedSsid;
    String savedPassword;

    // Can change after boot (the doorstep fallback), so read and written under textMutex
    SemaphoreHandle_t textMutex = nullptr;
    String registeredMdnsName;
    String setupReasonText; // why the home WiFi didn't work, for the setup page and the eyes

    // arduino-esp32 keeps one set of scan results for everyone, so scans take turns: scanMutex guards
    // the library's results and everything here
    SemaphoreHandle_t scanMutex = nullptr;
    bool pageScanRunning = false;
    bool pageScanFailed = false; // reported once, then the next ask starts afresh
    uint32_t pageScanStartedMs = 0;
    std::vector<FoundNetwork> pageScanResults;
    bool havePageScanResults = false;
    uint32_t pageScanResultsMs = 0;

    // The setup page's WiFi check: the web server's task asks (startWifiTest()), the network task runs it
    // (runWifiTest()). testMutex guards these; it's never held while the check waits.
    SemaphoreHandle_t testMutex = nullptr;
    bool testPending = false;      // asked for, not started yet
    bool testRunning = false;      // asked for or under way
    String testSsid;
    String testPassword;           // only until the check starts, then wiped
    network::WifiTestStatus testOutcome = network::WifiTestStatus::Idle; // the last finished check
    int32_t testRssi = 0;
    String testReason;
    uint32_t testFinishedMs = 0;

    // Where a scan last saw a network. Joining there skips the driver's own scan of every channel, which
    // is slow with the setup WiFi on (the radio keeps going back to its channel) and walks a mesh's access
    // points one by one.
    struct JoinHint
    {
        bool known = false;
        int32_t channel = 0;
        uint8_t bssid[6] = {};
    };

    std::atomic<uint32_t> joinStartedMs{0}; // when the latest join began, for the log's timings

    // Network task only (begin() sets retryRestartsSoFar before starting it)
    JoinHint homeHint; // the home WiFi's strongest access point, from scanForHome()
    uint32_t retryRestartsSoFar = 0;
    uint32_t attemptStartMs = 0;
    uint32_t nextAttemptMs = 0;
    uint32_t lostSinceMs = 0;
    bool lostNoticeSent = false;
    uint32_t connectedSinceMs = 0;
    uint32_t lastRecheckMs = 0;
    uint32_t brokenSinceMs = 0;
    uint32_t lastSaverCheckMs = 0;

    // These survive a restart (not a power cut), to count restarts made to retry the WiFi
    RTC_NOINIT_ATTR uint32_t rtcRetryMagic;
    RTC_NOINIT_ATTR uint32_t rtcRetryCount;
    constexpr uint32_t kRetryMagic = 0x50554D50; // "PUMP"

    // Holds a FreeRTOS mutex until the end of the scope. Does nothing before begin() creates it.
    class Lock
    {
    public:
        explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex)
        {
            if (mutex_ != nullptr)
                xSemaphoreTake(mutex_, portMAX_DELAY);
        }
        ~Lock()
        {
            if (mutex_ != nullptr)
                xSemaphoreGive(mutex_);
        }
        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;

    private:
        SemaphoreHandle_t mutex_;
    };

    // Retry restarts in a row just before this boot (0 after power-on or any other kind of restart)
    uint32_t takeRetryCount()
    {
        const bool retried = esp_reset_reason() == ESP_RST_SW && rtcRetryMagic == kRetryMagic;
        const uint32_t count = retried ? rtcRetryCount : 0;
        rtcRetryMagic = 0;
        return count;
    }

    [[noreturn]] void retryRestart(const char *why)
    {
        rtcRetryMagic = kRetryMagic;
        rtcRetryCount = retryRestartsSoFar + 1;
        firmware::restart(why);
    }

    void setReason(Reason why, const String &text)
    {
        Lock lock(textMutex);
        setupReasonText = text;
        reason = why;
    }

    void setRegisteredMdnsName(const String &name)
    {
        Lock lock(textMutex);
        registeredMdnsName = name;
    }

    // The listener runs on this task, so it's called with no lock held
    void tellListener(Mode mode)
    {
        const network::ModeListener listener = modeListener.load();
        if (listener != nullptr)
            listener(mode);
    }

    bool isAuthFailure(uint8_t why)
    {
        return why == WIFI_REASON_AUTH_FAIL || why == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
               why == WIFI_REASON_HANDSHAKE_TIMEOUT;
    }

    const char *authName(wifi_auth_mode_t mode)
    {
        switch (mode)
        {
        case WIFI_AUTH_OPEN:
            return "open";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2/WPA3";
        default:
            return "other";
        }
    }

    const char *reasonText(uint8_t why); // below

    unsigned long msSinceJoin()
    {
        return static_cast<unsigned long>(millis() - joinStartedMs.load());
    }

    // On the WiFi event task: count refusals for the joins, and log each step of a join with its timing
    // (the only way to see why one is slow or fails)
    void onWifiEvent(arduino_event_t *event)
    {
        switch (event->event_id)
        {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        {
            const auto &joined = event->event_info.wifi_sta_connected;
            LOG("WIFI", "+%lu ms: joined %02x:%02x:%02x:%02x:%02x:%02x on channel %u (%s)", msSinceJoin(),
                joined.bssid[0], joined.bssid[1], joined.bssid[2], joined.bssid[3], joined.bssid[4], joined.bssid[5],
                static_cast<unsigned>(joined.channel), authName(joined.authmode));
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            LOG("WIFI", "+%lu ms: got an address from the router", msSinceJoin());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        {
            const uint8_t why = event->event_info.wifi_sta_disconnected.reason;
            if (why == WIFI_REASON_ASSOC_LEAVE)
                return; // our own disconnect (WiFi.begin() and WiFi.reconnect() start with one)
            LOG("WIFI", "+%lu ms: turned away: %s (%u)", msSinceJoin(), reasonText(why), static_cast<unsigned>(why));
            lastDisconnectReason = why;
            refusalCount++;
            // A router can answer later tries with "not found" after turning down a wrong password, so the
            // last reason alone can't be trusted: remember that it happened at all
            if (isAuthFailure(why))
                sawAuthFailure = true;
            break;
        }
        default:
            break;
        }
    }

    // Overwrite a String's characters before it's freed, so a password doesn't linger in the heap
    void wipe(String &text)
    {
        if (text.length() > 0)
            memset(text.begin(), 0, text.length());
        text = String();
    }

    const char *reasonText(uint8_t why)
    {
        if (why == 0)
            return "timed out";
        return WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(why));
    }

    // Change the WiFi mode, then turn the transmit power down (see kTxPower)
    bool setWifiMode(wifi_mode_t mode)
    {
        if (!WiFi.mode(mode))
        {
            LOG("WIFI", "Couldn't change the WiFi mode");
            return false;
        }
        if (mode == WIFI_MODE_NULL)
            return true;
        // setTxPower() refuses until the WiFi event task has seen the radio start, which can lag
        // WiFi.mode() slightly, so wait for that first (normally it's already done)
        int startedBits = 0;
        if (mode & WIFI_MODE_STA)
            startedBits |= STA_STARTED_BIT;
        if (mode & WIFI_MODE_AP)
            startedBits |= AP_STARTED_BIT;
        WiFi.waitStatusBits(startedBits, 1000);
        if (!WiFi.setTxPower(kTxPower))
            LOG("WIFI", "Couldn't turn the transmit power down");
        return true;
    }

    // The processor at kJoinCpuMhz while one of these is in scope, then back to kCpuMhz
    class FullSpeed
    {
    public:
        FullSpeed() { setCpuFrequencyMhz(kJoinCpuMhz); }
        ~FullSpeed() { setCpuFrequencyMhz(kCpuMhz); }
        FullSpeed(const FullSpeed &) = delete;
        FullSpeed &operator=(const FullSpeed &) = delete;
    };

    // Start joining a WiFi: straight to the access point a scan found, if there's a hint, otherwise the
    // driver scans every channel first. WPA3 routers may use either way of deriving the password (some
    // only take the newer, "H2E"; arduino leaves only the older one on). False if the driver wouldn't
    // start, which the log says.
    bool startJoin(const String &ssid, const String &password, const JoinHint &hint)
    {
        joinStartedMs = millis();
        lastDisconnectReason = 0;
        sawAuthFailure = false;
        if (hint.known)
            LOG("WIFI", "Joining on channel %ld", static_cast<long>(hint.channel));
        // The scan method is read when the config is built, in begin()
        WiFi.setScanMethod(hint.known ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN);
        const wl_status_t status = WiFi.begin(ssid.c_str(), password.c_str(), hint.known ? hint.channel : 0,
                                              hint.known ? hint.bssid : nullptr, false);
        WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
        if (status == WL_CONNECT_FAILED)
        {
            LOG("WIFI", "The WiFi driver wouldn't take those details");
            return false;
        }
        wifi_config_t config;
        if (esp_wifi_get_config(WIFI_IF_STA, &config) == ESP_OK)
        {
            config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
            esp_wifi_set_config(WIFI_IF_STA, &config);
            memset(config.sta.password, 0, sizeof(config.sta.password));
        }
        const esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            LOG("WIFI", "Couldn't start joining: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    // After a join: the router's country information can lift the transmit power again (see kTxPower)
    void keepTxPowerDown()
    {
        if (!WiFi.setTxPower(kTxPower))
            LOG("WIFI", "Couldn't turn the transmit power down");
    }

    // The library's scan bookkeeping, so a scan started here works with scanComplete(), SSID(i) and so on
    struct ScanBits : WiFiGenericClass
    {
        using WiFiGenericClass::clearStatusBits;
        using WiFiGenericClass::setStatusBits;
    };

    // Start a scan in the background (as WiFi.scanNetworks(true, ...) does, but with every setting given:
    // arduino 2.0.17 leaves some of them uninitialised). Active scans ask each channel who's there;
    // ssid limits it to one network (hidden ones included). With scanMutex held.
    bool startScan(const char *ssid, uint32_t msPerChannel)
    {
        WiFi.scanDelete();
        wifi_scan_config_t config = {};
        config.ssid = reinterpret_cast<uint8_t *>(const_cast<char *>(ssid));
        config.show_hidden = ssid != nullptr;
        config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        config.scan_time.active.min = msPerChannel / 2;
        config.scan_time.active.max = msPerChannel;
        config.scan_time.passive = msPerChannel; // channels where asking isn't allowed
        config.home_chan_dwell_time = 0;         // the driver's default between channels (30 ms)
        if (esp_wifi_scan_start(&config, false) != ESP_OK)
            return false;
        ScanBits::clearStatusBits(WIFI_SCAN_DONE_BIT);
        ScanBits::setStatusBits(WIFI_SCANNING_BIT);
        return true;
    }

    // --- Housekeeping ---

    status_led::Pattern patternFor(Mode mode)
    {
        switch (mode)
        {
        case Mode::Connecting:
            return status_led::Pattern::FastBlink;
        case Mode::Connected:
            return status_led::Pattern::Heartbeat;
        case Mode::SetupAP:
            return status_led::Pattern::SlowBlink;
        case Mode::Disconnected:
        case Mode::Off:
            break;
        }
        return status_led::Pattern::Off;
    }

    // The network task's quick jobs, every kTaskPeriodMs: the BOOT button, the firmware trial and the
    // status LED. The slow jobs (joining at start-up, scans, starting the setup WiFi) call it while they
    // wait, so the button and the LED keep working meanwhile.
    void housekeeping()
    {
        boot_button::update();
        firmware::checkTrial();
        status_led::set(patternFor(currentMode));
        status_led::update();
    }

    // Wait on the network task, keeping the housekeeping going. It sleeps between checks, so the effects
    // on the loop task (one priority below) get the processor meanwhile.
    void waitFor(uint32_t ms)
    {
        const uint32_t start = millis();
        do
        {
            housekeeping();
            vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
        } while (millis() - start < ms);
    }

    // --- Scanning ---

    // The library's scan results as a list: named networks only, one entry per name, strongest first
    std::vector<FoundNetwork> listResults(int16_t count)
    {
        std::vector<FoundNetwork> found;
        for (int16_t i = 0; i < count; i++)
        {
            const String ssid = WiFi.SSID(i);
            if (ssid.isEmpty())
                continue; // hidden network
            FoundNetwork here{ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN,
                              static_cast<uint8_t>(WiFi.channel(i)), {}};
            if (const uint8_t *bssid = WiFi.BSSID(i))
                memcpy(here.bssid, bssid, sizeof(here.bssid));
            auto same = std::find_if(found.begin(), found.end(),
                                     [&](const FoundNetwork &entry) { return entry.ssid == ssid; });
            if (same == found.end())
                found.push_back(here);
            else if (here.rssi > same->rssi)
                *same = here; // mesh or extra access point: keep the strongest
        }
        std::sort(found.begin(), found.end(),
                  [](const FoundNetwork &a, const FoundNetwork &b) { return a.rssi > b.rssi; });
        if (found.size() > kMaxFoundNetworks)
            found.erase(found.begin() + kMaxFoundNetworks, found.end());
        return found;
    }

    // With scanMutex held: if the setup page's scan has finished, keep its results and free the
    // library's copy
    void collectPageScan(uint32_t now)
    {
        if (!pageScanRunning)
            return;
        const int16_t count = WiFi.scanComplete();
        if (count >= 0)
        {
            pageScanResults = listResults(count);
            havePageScanResults = true;
            pageScanResultsMs = now;
            pageScanRunning = false;
            WiFi.scanDelete();
        }
        else if (now - pageScanStartedMs >= kPageScanGiveUpMs)
        {
            LOG("WIFI", "The setup page's WiFi scan didn't finish");
            pageScanRunning = false;
            pageScanFailed = true;
            WiFi.scanDelete();
        }
    }

    // With scanMutex held: wait for the scan just started, as WiFi.scanNetworks() does when it's told to
    // wait, but keeping the housekeeping going for the ~2 s it takes. The number of networks found, or
    // WIFI_SCAN_FAILED.
    int16_t waitForScan()
    {
        const uint32_t start = millis();
        while ((WiFi.waitStatusBits(WIFI_SCAN_DONE_BIT, kTaskPeriodMs) & WIFI_SCAN_DONE_BIT) == 0)
        {
            if (millis() - start >= kHomeScanGiveUpMs)
                return WIFI_SCAN_FAILED;
            housekeeping();
        }
        return WiFi.scanComplete();
    }

    // One look for the home WiFi: an active scan for its name, so it's found even if it's hidden.
    // Notes its strongest access point in homeHint. False if it isn't there, or the setup page's scan has
    // the radio just now. Network task only.
    bool scanForHome(int32_t &rssi)
    {
        Lock lock(scanMutex);
        collectPageScan(millis());
        if (pageScanRunning)
            return false;
        const int16_t found = startScan(savedSsid.c_str(), kScanMsPerChannel) ? waitForScan() : WIFI_SCAN_FAILED;
        bool seen = false;
        for (int16_t i = 0; i < found; i++)
        {
            if (WiFi.SSID(i) == savedSsid && (!seen || WiFi.RSSI(i) > rssi))
            {
                seen = true;
                rssi = WiFi.RSSI(i);
                homeHint.known = true;
                homeHint.channel = WiFi.channel(i);
                if (const uint8_t *bssid = WiFi.BSSID(i))
                    memcpy(homeHint.bssid, bssid, sizeof(homeHint.bssid));
            }
        }
        WiFi.scanDelete();
        if (found < 0)
            LOG("WIFI", "WiFi scan failed");
        return seen;
    }

    // Scribe's pre-scan: saves a 15 s wait when the home WiFi isn't there at all
    bool homeWifiVisible(int attempts)
    {
        for (int attempt = 0; attempt < attempts; attempt++)
        {
            int32_t rssi = 0;
            const bool seen = scanForHome(rssi);
            if (seen)
            {
                LOG("WIFI", "Found \"%s\" (signal %ld dBm, channel %ld)", savedSsid.c_str(), static_cast<long>(rssi),
                    static_cast<long>(homeHint.channel));
                return true;
            }
        }
        return false;
    }

    // --- Joining ---

    bool connectHome()
    {
        if (!setWifiMode(WIFI_STA))
        {
            setReason(Reason::JoinFailed, "Couldn't join \"" + savedSsid + "\"");
            return false;
        }

        LOG("WIFI", "Looking for \"%s\"", savedSsid.c_str());
        if (!homeWifiVisible(kBootScanAttempts))
        {
            LOG("WIFI", "\"%s\" isn't in range", savedSsid.c_str());
            setReason(Reason::NotFound, "Couldn't find \"" + savedSsid +
                                            "\" — is it a 2.4 GHz network, in range, with the router on?");
            return false;
        }

        LOG("WIFI", "Joining \"%s\" (up to %lu s)", savedSsid.c_str(),
            static_cast<unsigned long>(kWifiConnectTimeoutMs / 1000));
        FullSpeed fast;
        WiFi.setAutoReconnect(true);
        JoinHint hint = homeHint;
        bool started = startJoin(savedSsid, savedPassword, hint);
        uint32_t start = millis();
        while (started && WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs)
        {
            waitFor(kTaskPeriodMs);
            // Not where the scan saw it (the router changed channel?): try again without the hint
            if (hint.known && lastDisconnectReason == WIFI_REASON_NO_AP_FOUND && !sawAuthFailure)
            {
                LOG("WIFI", "Not on channel %ld after all: looking on every channel", static_cast<long>(hint.channel));
                hint.known = false;
                started = startJoin(savedSsid, savedPassword, hint);
                start = millis();
            }
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            const uint8_t why = lastDisconnectReason;
            const bool noAddress = (WiFi.getStatusBits() & STA_CONNECTED_BIT) != 0;
            if (sawAuthFailure)
            {
                LOG("WIFI", "Couldn't join \"%s\": %s, after it turned the password down (wrong password?)",
                    savedSsid.c_str(), reasonText(why));
                setReason(Reason::WrongPassword, "Couldn't join \"" + savedSsid + "\" — wrong password?");
            }
            else if (noAddress)
            {
                LOG("WIFI", "Joined \"%s\", but the router gave no address", savedSsid.c_str());
                setReason(Reason::JoinFailed, "Joined \"" + savedSsid + "\", but the router gave no address");
            }
            else
            {
                LOG("WIFI", "Couldn't join \"%s\": %s", savedSsid.c_str(), reasonText(why));
                setReason(Reason::JoinFailed, "Couldn't join \"" + savedSsid + "\"");
            }
            return false;
        }

        keepTxPowerDown();
        LOG("WIFI", "Connected to \"%s\" in %lu ms: IP %s, signal %d dBm, channel %d, transmit power %d (x0.25 dBm)",
            savedSsid.c_str(), msSinceJoin(), WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()),
            static_cast<int>(WiFi.channel()), static_cast<int>(WiFi.getTxPower()));
        return true;
    }

    // The channel of the strongest network around (most likely the home WiFi), or 0 if the scan finds
    // none. A brand-new pumpkin starts its setup WiFi there, so the setup page's WiFi check rarely has to
    // move it to another channel (which drops phones off it for a moment). Network task only.
    int32_t strongestNetworksChannel()
    {
        Lock lock(scanMutex);
        const int16_t found = startScan(nullptr, kScanMsPerChannel) ? waitForScan() : WIFI_SCAN_FAILED;
        int32_t channel = 0;
        int32_t best = INT32_MIN;
        for (int16_t i = 0; i < found; i++)
        {
            if (WiFi.RSSI(i) > best)
            {
                best = WiFi.RSSI(i);
                channel = WiFi.channel(i);
            }
        }
        WiFi.scanDelete();
        return channel;
    }

    // channel 0: the driver's default (1)
    bool startSetupAP(int32_t channel = 0)
    {
        // Stop the station side trying to join the home WiFi in the background: its channel hopping
        // would drop phones off the setup WiFi. AP+STA still lets the setup page scan for networks.
        WiFi.setAutoReconnect(false);
        WiFi.disconnect();

        for (int attempt = 1; attempt <= kSetupApAttempts; attempt++)
        {
            if (setWifiMode(WIFI_AP_STA) && WiFi.softAP(kSetupApName, kSetupApPassword, channel > 0 ? channel : 1))
            {
                const IPAddress ip = WiFi.softAPIP();
                // Captive portal: every name a phone looks up points at us, so its sign-in page opens ours
                if (!captive_dns::start(ip))
                    LOG("WIFI", "Captive portal DNS didn't start");
                currentMode = Mode::SetupAP;
                LOG("WIFI", "Setup WiFi \"%s\" is on (password \"%s\", channel %d), page at http://%s", kSetupApName,
                    kSetupApPassword, static_cast<int>(WiFi.channel()), ip.toString().c_str());
                return true;
            }
            LOG("WIFI", "Setup WiFi didn't start (attempt %d of %d)", attempt, kSetupApAttempts);
            WiFi.mode(WIFI_OFF);
            waitFor(kSetupApRetryMs);
        }
        return false;
    }

    // pumpkin.local (no check that the name's free first, as Scribe did: a house has one pumpkin, and the
    // check could hold up the BOOT button for 15 s)
    void startMdns()
    {
        if (!MDNS.begin(kHostname))
        {
            LOG("MDNS", "Couldn't register %s.local: use the IP address", kHostname);
            return;
        }
        MDNS.setInstanceName(settings::get().pumpkinName); // shown by Bonjour browsers
        MDNS.addService("http", "tcp", kWebServerPort);
        setRegisteredMdnsName(kHostname);
        LOG("MDNS", "Web address http://%s.local", kHostname);
    }

    void reportStack(const char *when)
    {
        LOG("NETWORK", "Task stack %s: %u bytes spare", when,
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }

    // The setup WiFi wouldn't start: the network task restarts to try again after kBrokenRestartMs
    void becomeBroken()
    {
        currentMode = Mode::Disconnected;
        brokenSinceMs = millis();
        LOG("WIFI", "No WiFi at all");
        role = Role::Broken;
    }

    // Home WiFi gone for kLostWifiFallbackMs: switch to the setup WiFi so phones nearby can still reach
    // the pumpkin. lookForHomeWifi() brings it back (by restarting) once the home WiFi is in range again.
    void fallBackToSetupWifi()
    {
        LOG("WIFI", "No \"%s\" for %lu minutes: starting the setup WiFi", savedSsid.c_str(),
            static_cast<unsigned long>(kLostWifiFallbackMs / 60000));
        MDNS.end();
        setRegisteredMdnsName("");
        setReason(Reason::Lost, "Lost \"" + savedSsid + "\"");
        if (startSetupAP(homeHint.known ? homeHint.channel : 0))
        {
            lastRecheckMs = millis();
            role = Role::Setup;
        }
        else
        {
            becomeBroken();
        }
        tellListener(currentMode);
        reportStack("after the fallback");
    }

    // Scribe's handleWiFiReconnection(), without its blocking delay. While it's connected the WiFi
    // driver's own auto-reconnect fixes blips at once. Once it's really lost, that's switched off
    // (it scans back-to-back, which flattens batteries) and this paces the tries instead: one every
    // kWifiConnectTimeoutMs + kWifiReconnectIntervalMs, until kLostWifiFallbackMs.
    void keepConnected(uint32_t now)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            if (currentMode != Mode::Connected)
            {
                currentMode = Mode::Connected;
                connectedSinceMs = now;
                WiFi.setAutoReconnect(true);
                keepTxPowerDown();
                LOG("WIFI", "Back on \"%s\": IP %s", savedSsid.c_str(), WiFi.localIP().toString().c_str());
                if (lostNoticeSent)
                {
                    lostNoticeSent = false;
                    tellListener(Mode::Connected);
                }
            }
            else if (retryRestartsSoFar != 0 && now - connectedSinceMs >= kHomeSettledMs)
            {
                retryRestartsSoFar = 0; // so a pumpkin taken out to the doorstep every night still comes back
            }
            return;
        }

        if (currentMode == Mode::Connected)
        {
            WiFi.setAutoReconnect(false);
            currentMode = Mode::Disconnected;
            lostSinceMs = now;
            nextAttemptMs = now + kWifiReconnectIntervalMs;
            LOG("WIFI", "Lost \"%s\": %s", savedSsid.c_str(), reasonText(lastDisconnectReason));
            return;
        }

        if (now - lostSinceMs >= kLostWifiFallbackMs)
        {
            fallBackToSetupWifi();
            return;
        }
        if (!lostNoticeSent && now - lostSinceMs >= kLostNoticeMs)
        {
            lostNoticeSent = true;
            tellListener(Mode::Disconnected);
        }

        if (currentMode == Mode::Connecting)
        {
            if (now - attemptStartMs < kWifiConnectTimeoutMs)
                return; // give this attempt its full time
            currentMode = Mode::Disconnected;
            nextAttemptMs = now + kWifiReconnectIntervalMs;
            return;
        }

        if (static_cast<int32_t>(now - nextAttemptMs) >= 0)
        {
            LOG("WIFI", "Trying to rejoin \"%s\"", savedSsid.c_str());
            startJoin(savedSsid, savedPassword, JoinHint{}); // every channel: the router may have moved
            currentMode = Mode::Connecting;
            attemptStartMs = now;
        }
    }

    // Setup mode although home WiFi is saved: it wasn't in range at boot (router off?), joining failed,
    // or it was lost later. Now and then look for it, and restart to join it if it's there. Only while
    // no phone is on the setup WiFi: the scan takes the radio off its channel for about 2 s.
    void lookForHomeWifi(uint32_t now)
    {
        if (savedSsid.isEmpty())
            return; // nothing to look for
        // (After a refusal too: a weak signal, on the doorstep say, can look like a wrong password.
        // kMaxRetryRestarts stops a really wrong one restarting the pumpkin all night.)
        if (retryRestartsSoFar >= kMaxRetryRestarts)
            return; // tried enough: stay in setup mode until switched off and on
        if (now - lastRecheckMs < kHomeRecheckMs)
            return;
        lastRecheckMs = now;
        if (WiFi.softAPgetStationNum() > 0)
            return;
        if (!homeWifiVisible(1))
            return;
        LOG("WIFI", "\"%s\" is in range again", savedSsid.c_str());
        retryRestart("to join the home WiFi");
    }

    // --- Checking a home WiFi for the setup page (Scribe's handleTestWiFi()) ---

    void finishWifiTest(network::WifiTestStatus outcome, int32_t rssi, const String &why)
    {
        Lock lock(testMutex);
        testOutcome = outcome;
        testRssi = rssi;
        testReason = why;
        testFinishedMs = millis();
        testRunning = false;
    }

    // Words for the family when the check fails (the page shows them by the password box)
    String testFailureText(const String &ssid, bool authFailed, uint8_t why)
    {
        if (authFailed)
            return "That password didn't work. Check it (capital letters too), or if the signal is weak, move the "
                   "pumpkin closer to the router.";
        if (why == WIFI_REASON_NO_AP_FOUND)
            return "Can't find " + ssid + ": is it a 2.4 GHz network, and in range?";
        return "Couldn't join " + ssid + ". Check the password, or move the pumpkin closer to the router.";
    }

    // Setup mode: if the page asked for a check (startWifiTest()), join that WiFi on the station side
    // while the setup WiFi stays up, note how it went, then leave it again. Nothing is saved. Network task
    // only. Under scanMutex, so it never meets the page's scan or the look for the home WiFi (one radio
    // user at a time). The captive DNS and the housekeeping carry on while it waits.
    //
    // It goes straight to the access point the page's scan saw (see JoinHint), so the radio doesn't have
    // to scan every channel while the setup WiFi is on. The radio has one channel: joining a router on
    // another channel moves the setup WiFi there too, so phones may drop off it for a moment and rejoin
    // (a brand-new pumpkin starts its setup WiFi on the strongest network's channel to make that rare). The result is
    // kept for kWifiTestKeepMs so they can still ask how it went.
    //
    // Joined (even before the router hands out an address): Passed. Refused with an authentication error,
    // or not found: Failed. Nothing heard either way in kWifiTestMaxMs: Unsure, and the page offers to save
    // it anyway (the join after the restart, without the setup WiFi, is quicker).
    void runWifiTest()
    {
        {
            Lock lock(testMutex);
            if (!testPending)
                return;
        }
        Lock radio(scanMutex);
        collectPageScan(millis());
        if (pageScanRunning)
            return; // the page's scan has the radio: try again next time round

        String ssid;
        String password;
        {
            Lock lock(testMutex);
            testPending = false;
            ssid = testSsid;
            password = testPassword;
            wipe(testPassword);
        }

        JoinHint hint;
        for (const FoundNetwork &net : pageScanResults)
        {
            if (net.ssid == ssid)
            {
                hint.known = true;
                hint.channel = net.channel;
                memcpy(hint.bssid, net.bssid, sizeof(hint.bssid));
            }
        }

        LOG("WIFI", "Checking \"%s\" for the setup page (up to %lu s)", ssid.c_str(),
            static_cast<unsigned long>(kWifiTestMaxMs / 1000));
        FullSpeed fast;
        WiFi.setAutoReconnect(false); // off in setup mode anyway: one try (and the library's one retry)
        uint8_t refusalsBefore = refusalCount;
        bool started = startJoin(ssid, password, hint);

        uint32_t start = millis();
        uint8_t refusals = 0;
        uint32_t lastRefusalMs = start;
        while (started && WiFi.status() != WL_CONNECTED && millis() - start < kWifiTestMaxMs)
        {
            housekeeping();
            captive_dns::serve(); // phones on the setup WiFi keep getting answers
            vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));

            const uint8_t seen = static_cast<uint8_t>(refusalCount - refusalsBefore);
            if (seen != refusals)
            {
                refusals = seen;
                lastRefusalMs = millis();
            }
            const bool joining = (WiFi.getStatusBits() & STA_CONNECTED_BIT) != 0;
            // Not where the page's scan saw it (the router changed channel?): once more, on every channel
            if (!joining && hint.known && lastDisconnectReason == WIFI_REASON_NO_AP_FOUND && !sawAuthFailure)
            {
                LOG("WIFI", "Not on channel %ld after all: looking on every channel", static_cast<long>(hint.channel));
                hint.known = false;
                refusalsBefore = refusalCount;
                refusals = 0;
                started = startJoin(ssid, password, hint);
                start = lastRefusalMs = millis();
                continue;
            }
            // The library tries once more after the first refusal since switch-on, and never again with
            // auto-reconnect off. So two refusals, or one and nothing since, means it has given up.
            if (!joining && (refusals >= 2 || (refusals == 1 && millis() - lastRefusalMs >= kWifiTestSettleMs)))
                break;
        }
        wipe(password);

        // Joined but still waiting for an address from the router counts: the password worked
        const bool joined = WiFi.status() == WL_CONNECTED || (WiFi.getStatusBits() & STA_CONNECTED_BIT) != 0;
        const int32_t rssi = joined ? WiFi.RSSI() : 0;
        const uint8_t why = lastDisconnectReason;
        const bool authFailed = sawAuthFailure;
        const unsigned long tookMs = msSinceJoin();
        // Leave it again (the station side only: the setup WiFi stays up). Stop the join first, then clear
        // the name and password out of the driver's memory (it won't take a new config mid-join), so a late
        // retry by the library has nothing to join.
        WiFi.disconnect();
        waitFor(kLeaveSettleMs);
        WiFi.disconnect(false, true);

        if (joined)
        {
            LOG("WIFI", "Check: joined \"%s\" (signal %ld dBm) in %lu ms", ssid.c_str(), static_cast<long>(rssi), tookMs);
            finishWifiTest(network::WifiTestStatus::Passed, rssi, "");
        }
        else if (authFailed || why == WIFI_REASON_NO_AP_FOUND)
        {
            LOG("WIFI", "Check: couldn't join \"%s\": %s%s (after %lu ms)", ssid.c_str(), reasonText(why),
                authFailed ? ", after it turned the password down" : "", tookMs);
            finishWifiTest(network::WifiTestStatus::Failed, 0, testFailureText(ssid, authFailed, why));
        }
        else
        {
            LOG("WIFI", "Check: no answer from \"%s\" (%s, after %lu ms)", ssid.c_str(),
                started ? reasonText(why) : "couldn't start", tookMs);
            finishWifiTest(network::WifiTestStatus::Unsure, 0,
                           ssid + " didn't answer in time, so the pumpkin couldn't tell whether the password "
                                  "works. Save it anyway, and the pumpkin will try it when it restarts.");
        }
    }

    // --- Saving batteries (kWifiIdleOffMs) ---

    // A WiFi check or a scan for the setup page under way. Their requests came only seconds ago, so the
    // idle time can't run out meanwhile: this is just in case.
    bool radioBusy()
    {
        {
            Lock lock(testMutex);
            if (testRunning)
                return true;
        }
        Lock lock(scanMutex);
        collectPageScan(millis()); // a scan nobody came back for still finishes
        return pageScanRunning;
    }

    // Off until a restart: the captive DNS, the web name and the radio. The web server stays, with nothing
    // to hear. Nothing turns the WiFi on again meanwhile: keepConnected() and lookForHomeWifi() only run in
    // the Home and Setup roles, and the driver's own reconnect is switched off first.
    void switchWifiOff()
    {
        LOG("WIFI", "Nobody has used the pumpkin for %lu minutes: WiFi off to save batteries (BOOT brings it back)",
            static_cast<unsigned long>(kWifiIdleOffMs / 60000));
        role = Role::Off; // first, so the setup page's scans and checks are turned away from now on
        captive_dns::stop();
        MDNS.end();
        setRegisteredMdnsName("");
        WiFi.setAutoReconnect(false);
        setWifiMode(WIFI_OFF);
        currentMode = Mode::Off; // the status LED goes off at the next housekeeping()
        tellListener(Mode::Off);
    }

    // Every kSaverCheckMs while the WiFi is on: switch it off if the setting's on and nobody has used the
    // pumpkin for kWifiIdleOffMs. A phone on the setup WiFi counts as using it (it may be about to open a
    // page); the web server notes requests and open event streams (network::noteActivity()). The
    // pumpkin's own WiFi traffic (DNS answers, mDNS, reconnects, scans for the home WiFi) doesn't count.
    void saveBatteries()
    {
        const uint32_t now = millis();
        if (now - lastSaverCheckMs < kSaverCheckMs)
            return;
        lastSaverCheckMs = now;
        if (role == Role::Setup)
        {
            const uint8_t phones = WiFi.softAPgetStationNum();
            if (phones != phonesOnSetupWifi)
            {
                LOG("WIFI", "Phones on the setup WiFi: %u", static_cast<unsigned>(phones)); // for setup trouble
                phonesOnSetupWifi = phones;
            }
            if (phones > 0)
                lastActivityMs = now;
        }
        // lastActivityMs is read after now, so a request noted meanwhile can't look 49 days old
        if (wifi_saver::timeToSwitchOff(settings::get().wifiSaver, radioBusy(), now, lastActivityMs.load(),
                                        kWifiIdleOffMs))
            switchWifiOff();
    }

    // While it's off, every kSaverCheckMs: if the driver has started the radio again by itself (its one
    // retry after a failed join could), switch it off again
    void keepWifiOff()
    {
        const uint32_t now = millis();
        if (now - lastSaverCheckMs < kSaverCheckMs)
            return;
        lastSaverCheckMs = now;
        if (WiFi.getMode() == WIFI_OFF)
            return;
        LOG("WIFI", "The WiFi came back on by itself: switching it off again");
        WiFi.setAutoReconnect(false);
        setWifiMode(WIFI_OFF);
    }

    // The first thing the network task does, while the show plays: join the home WiFi and register the
    // web name, or start the setup WiFi. Then it tells the mode listener how it went, so the eyes can say
    // how to reach the pumpkin. (Scribe did this in setup(), before anything else ran.)
    void connectAtStart()
    {
        bool joined = false;
        int32_t channel = 0;
        if (savedSsid.isEmpty())
        {
            LOG("WIFI", "No home WiFi saved yet");
            channel = strongestNetworksChannel();
        }
        else
        {
            joined = connectHome();
            channel = homeHint.known ? homeHint.channel : 0; // where a check of it would go anyway
        }

        lastRecheckMs = millis();
        if (joined)
        {
            currentMode = Mode::Connected; // the web page shows the address while the name is checked
            connectedSinceMs = millis();
            startMdns();
            role = Role::Home; // from now on, keepConnected() looks after reconnects
        }
        else if (startSetupAP(channel))
        {
            role = Role::Setup; // ...or DNS and looking for the home WiFi
        }
        else
        {
            becomeBroken();
        }
        tellListener(currentMode);
        reportStack("after connecting");
    }

    // Network housekeeping, in place of Scribe's loop() calls. It starts by connecting (connectAtStart()).
    // Priority 2: just above the Arduino loop task (1), where the effects run, so DNS replies and the
    // BOOT button's timing never wait for an effect to yield; below the async web server's task (3), lwIP
    // (18) and the WiFi driver. Each 10 ms wake-up is a few microseconds of work, so the effects' timing
    // isn't disturbed. The slow jobs (connecting at start-up, a 2 s scan, starting the setup WiFi) wait on
    // the WiFi driver or in waitFor(), never spinning, so the loop task keeps the processor meanwhile.
    // Stack: the deepest paths are connecting at start-up (the mDNS name check, or starting the setup
    // WiFi, then the mode listener, which builds messages for the eyes), the same after the doorstep
    // fallback, and the BOOT button's NVS write + restart, which can now come from inside those waits.
    // 8 KB is what the Arduino loop task had when it ran the start-up connect, and about twice the
    // deepest path as estimated from -fstack-usage. The spare amount is logged after connecting, a
    // minute after boot and after a fallback: under about 1.5 KB spare, make it bigger.
    void networkTask(void *)
    {
        const uint32_t startedMs = millis();
        if (role.load() != Role::Offline)
            connectAtStart();

        bool stackReported = false;
        for (;;)
        {
            const uint32_t now = millis();
            housekeeping();

            switch (role.load())
            {
            case Role::Starting:
                break;
            case Role::Home:
                keepConnected(now);
                saveBatteries();
                break;
            case Role::Setup:
                captive_dns::serve();
                runWifiTest();
                lookForHomeWifi(now);
                saveBatteries();
                break;
            case Role::Broken:
                if (retryRestartsSoFar < kMaxRetryRestarts && now - brokenSinceMs >= kBrokenRestartMs)
                    retryRestart("to try the WiFi again");
                saveBatteries();
                break;
            case Role::Off:
                keepWifiOff();
                break;
            case Role::Offline:
                break;
            }

            if (!stackReported && now - startedMs >= kStackReportMs)
            {
                stackReported = true;
                reportStack("after a minute");
            }
            vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
        }
    }

    // GB, channels 1-13, whatever routers say: under the driver's default (follow the router's country)
    // channels 12 and 13, which UK routers can pick, are only listened to, and a router's country can lift
    // the transmit power back up (see kTxPower). The four pumpkins are all in the UK.
    void setCountry()
    {
        const wifi_country_t gb = {"GB", 1, 13, 20, WIFI_COUNTRY_POLICY_MANUAL};
        if (esp_wifi_set_country(&gb) != ESP_OK)
            LOG("WIFI", "Couldn't set the WiFi country");
        keepTxPowerDown(); // the country's limit replaces it
    }

    bool startTask()
    {
        return xTaskCreate(networkTask, "network", kTaskStackBytes, nullptr, kTaskPriority, nullptr) == pdPASS;
    }
}

namespace network
{
    void begin()
    {
        static bool started = false;
        if (started)
            return;
        started = true;

        textMutex = xSemaphoreCreateMutex();
        scanMutex = xSemaphoreCreateMutex();
        testMutex = xSemaphoreCreateMutex();

        const Settings s = settings::get();
        savedSsid = s.wifiSsid;
        savedPassword = s.wifiPassword;
        retryRestartsSoFar = takeRetryCount();
        lastActivityMs = millis(); // switching on counts as using it: the idle time starts now

        currentMode = Mode::Connecting;
        status_led::begin();
        boot_button::begin();

        // Our settings hold the WiFi details: stop the driver keeping its own copy in flash, so "forget
        // WiFi" really forgets. Must come before the first WiFi.mode().
        WiFi.persistent(false);
        // Name shown in the router's list of devices. Applied when the station starts, so this must
        // also come before WiFi.mode().
        WiFi.setHostname(kHostname);
        // Homes with mesh or extra access points: check every channel and join the strongest signal
        // (Scribe took the first match). This matters more at reduced transmit power.
        WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
        WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
        WiFi.onEvent(onWifiEvent); // every event: it logs each step of a join
        // Start the radio here, not in the task: the first WiFi.mode() also starts the network stack
        // (lwIP), and web::begin() crashes without it. Station mode either way: if the setup WiFi is
        // needed, startSetupAP() adds it, name and password included.
        setWifiMode(WIFI_STA);
        setCountry();

        // Joining (or the setup WiFi), then the captive DNS, reconnects, status LED, BOOT button and
        // the firmware trial, all in the background
        if (!startTask())
        {
            // Out of memory at boot? Connect here instead, as Scribe does: the show waits, but there's
            // still WiFi (without the captive DNS, reconnects or BOOT button)
            LOG("NETWORK", "Couldn't start the network task: connecting before the show starts");
            connectAtStart();
        }
    }

    void beginOffline()
    {
        static bool started = false;
        if (started)
            return;
        started = true;
        textMutex = xSemaphoreCreateMutex();
        scanMutex = xSemaphoreCreateMutex();
        testMutex = xSemaphoreCreateMutex();
        status_led::begin();
        boot_button::begin();
        currentMode = Mode::Off;
        role = Role::Offline; // just the housekeeping: no radio, so nothing for keepWifiOff() to do
        if (!startTask())
            LOG("NETWORK", "Couldn't start the network task: no BOOT button");
    }

    Mode mode()
    {
        return currentMode;
    }

    bool isSetupAP()
    {
        return currentMode == Mode::SetupAP;
    }

    bool hasSavedWifi()
    {
        return !savedSsid.isEmpty();
    }

    String ipAddress()
    {
        switch (currentMode.load())
        {
        case Mode::SetupAP:
            return WiFi.softAPIP().toString();
        case Mode::Connected:
            return WiFi.localIP().toString();
        default:
            return "";
        }
    }

    String mdnsHostname()
    {
        Lock lock(textMutex);
        return registeredMdnsName;
    }

    String apSsid()
    {
        return kSetupApName;
    }

    String homeSsid()
    {
        return savedSsid;
    }

    String setupReason()
    {
        Lock lock(textMutex);
        return setupReasonText;
    }

    bool homeWifiOutOfReach()
    {
        const Reason why = reason;
        return currentMode == Mode::SetupAP && !savedSsid.isEmpty() && (why == Reason::NotFound || why == Reason::Lost);
    }

    int rssi()
    {
        return currentMode == Mode::Connected ? WiFi.RSSI() : 0;
    }

    void onModeChange(ModeListener listener)
    {
        modeListener = listener;
    }

    void noteActivity()
    {
        lastActivityMs = millis();
    }

    ScanStatus scan(std::vector<FoundNetwork> &found)
    {
        const Role current = role.load();
        if (scanMutex == nullptr || (current != Role::Home && current != Role::Setup))
            return ScanStatus::Failed; // still starting, or no WiFi at all
        // Never wait: if the network task is scanning, the page just asks again
        if (xSemaphoreTake(scanMutex, 0) != pdTRUE)
            return ScanStatus::Running;

        const uint32_t nowMs = millis();
        collectPageScan(nowMs);
        ScanStatus status;
        if (pageScanRunning)
        {
            status = ScanStatus::Running;
        }
        else if (pageScanFailed)
        {
            pageScanFailed = false;
            status = ScanStatus::Failed;
        }
        else if (havePageScanResults && nowMs - pageScanResultsMs < kPageScanKeepMs)
        {
            found = pageScanResults;
            status = ScanStatus::Ready;
        }
        else
        {
            // Active (asking each channel who's there): a passive listen of kScanMsPerChannel catches about
            // one beacon per router and can miss the home WiFi. The radio goes back to the setup WiFi's
            // channel between channels, so phones stay on it.
            havePageScanResults = false;
            if (startScan(nullptr, kScanMsPerChannel))
            {
                pageScanRunning = true;
                pageScanStartedMs = nowMs;
                status = ScanStatus::Running;
            }
            else
            {
                LOG("WIFI", "Couldn't start a WiFi scan for the setup page");
                status = ScanStatus::Failed;
            }
        }
        xSemaphoreGive(scanMutex);
        return status;
    }

    WifiTestStart startWifiTest(const String &ssid, const String &password)
    {
        // A check takes the station side off whatever it's doing: only while running the setup WiFi
        if (testMutex == nullptr || role.load() != Role::Setup || currentMode != Mode::SetupAP)
            return WifiTestStart::NotInSetup;
        Lock lock(testMutex);
        if (testRunning)
            return WifiTestStart::Busy;
        testSsid = ssid;
        testPassword = password;
        testPending = true;
        testRunning = true;
        testOutcome = WifiTestStatus::Idle; // the last result no longer counts
        testRssi = 0;
        testReason = "";
        return WifiTestStart::Started;
    }

    WifiTest wifiTest()
    {
        WifiTest result{WifiTestStatus::Idle, "", 0, ""};
        if (testMutex == nullptr)
            return result;
        Lock lock(testMutex);
        if (testRunning)
        {
            result.status = WifiTestStatus::Running;
            result.ssid = testSsid;
        }
        else if (testOutcome != WifiTestStatus::Idle && millis() - testFinishedMs < kWifiTestKeepMs)
        {
            result.status = testOutcome;
            result.ssid = testSsid;
            result.rssi = testRssi;
            result.reason = testReason;
        }
        return result;
    }
}
