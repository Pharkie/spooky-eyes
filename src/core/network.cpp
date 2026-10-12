// WiFi, closely ported from Scribe Evolution's src/core/network.cpp:
//   connectToWiFi()          -> connectHome(): look for the home WiFi first, then join with a timeout
//   startFallbackAP()        -> startSetupAP(): AP+STA (so the setup page can scan) plus captive DNS
//   setupmDNS()              -> startMdns(): ping each candidate name, then claim the first free one
//   handleWiFiReconnection() -> keepConnected(), now without blocking
//   handleDNSServer() and updateStatusLED(), which Scribe called from loop() -> networkTask()
//
// Changes for the pumpkin:
// - the setup WiFi is named after the pumpkin, and so is the web address ("jacks-pumpkin.local")
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
// - every WiFi scan, the setup page's included, goes through scanMutex
// - left out: printAPDetailsOnStartup() (the eyes scroll the address instead), MQTT, NTP, LogManager and
//   the custom PCB's RGB status LED

#include "core/network.h"

#include <ESP32Ping.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <algorithm>
#include <atomic>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "core/captive_dns.h"
#include "core/constants.h"
#include "core/firmware.h"
#include "core/logging.h"
#include "core/settings.h"
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
    // The setup page's scan. The library gives up after 20 x kScanMsPerChannel, which can come before
    // the scan really ends while the setup WiFi is on (the radio keeps hopping back to its channel), so
    // we keep asking for longer than that.
    constexpr uint32_t kPageScanGiveUpMs = PumpkinTime::Seconds(10);
    constexpr uint32_t kPageScanKeepMs = PumpkinTime::Seconds(10); // hand the same results out again
    constexpr size_t kMaxFoundNetworks = 30;

    constexpr int kMdnsNameAttempts = 3; // jacks-pumpkin, jacks-pumpkin-2, jacks-pumpkin-3
    constexpr uint8_t kMdnsPingCount = 2;
    constexpr int kSetupApAttempts = 3;

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

    // What the network task looks after
    enum class Role : uint8_t
    {
        Starting, // network::begin() is still connecting: status LED and BOOT button only
        Home,     // joined the home WiFi at boot: keep it connected
        Setup,    // running the setup WiFi: answer DNS
        Broken,   // the setup WiFi wouldn't start
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
    std::atomic<bool> sawAuthFailure{false};      // ditto: sticky, cleared when the boot join starts
    std::atomic<network::ModeListener> modeListener{nullptr};

    // Set by begin() before the role leaves Starting, then only read (from any task)
    String savedSsid;
    String savedPassword;
    String setupApName;
    String wantedHostname;

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

    // Network task only (begin() sets some before the task takes over)
    uint32_t retryRestartsSoFar = 0;
    uint32_t attemptStartMs = 0;
    uint32_t nextAttemptMs = 0;
    uint32_t lostSinceMs = 0;
    bool lostNoticeSent = false;
    uint32_t connectedSinceMs = 0;
    uint32_t lastRecheckMs = 0;
    uint32_t brokenSinceMs = 0;

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

    // No watchdog watches the loop task during setup() on this core: arduino-esp32 2.0.17 subscribes
    // neither the loop task nor (on the C3) the idle task. So this normally does nothing; it keeps
    // Scribe's feeding in case something subscribes the loop task later (enableLoopWDT()).
    void feedWatchdogIfWatched()
    {
        if (esp_task_wdt_status(nullptr) == ESP_OK)
            esp_task_wdt_reset();
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

    void onWifiEvent(arduino_event_t *event)
    {
        const uint8_t why = event->event_info.wifi_sta_disconnected.reason;
        if (why == WIFI_REASON_ASSOC_LEAVE)
            return; // our own disconnect (WiFi.begin() and WiFi.reconnect() start with one)
        lastDisconnectReason = why;
        // A router can answer later tries with "not found" after turning down a wrong password, so the
        // last reason alone can't be trusted: remember that it happened at all
        if (isAuthFailure(why))
            sawAuthFailure = true;
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
            const FoundNetwork here{ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN,
                                    static_cast<uint8_t>(WiFi.channel(i))};
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

    // One look for the home WiFi: an active scan for its name, so it's found even if it's hidden.
    // False if it isn't there, or the setup page's scan has the radio just now.
    bool scanForHome(int32_t &rssi)
    {
        Lock lock(scanMutex);
        collectPageScan(millis());
        if (pageScanRunning)
            return false;
        const int16_t found = WiFi.scanNetworks(false, true, false, kScanMsPerChannel, 0, savedSsid.c_str());
        bool seen = false;
        for (int16_t i = 0; i < found; i++)
        {
            if (WiFi.SSID(i) == savedSsid && (!seen || WiFi.RSSI(i) > rssi))
            {
                seen = true;
                rssi = WiFi.RSSI(i);
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
            feedWatchdogIfWatched();
            if (seen)
            {
                LOG("WIFI", "Found \"%s\" (signal %ld dBm)", savedSsid.c_str(), static_cast<long>(rssi));
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
        lastDisconnectReason = 0;
        sawAuthFailure = false;
        WiFi.setAutoReconnect(true);
        WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs)
        {
            delay(100);
            feedWatchdogIfWatched();
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            const uint8_t why = lastDisconnectReason;
            if (sawAuthFailure)
            {
                LOG("WIFI", "Couldn't join \"%s\": %s, after it turned the password down (wrong password?)",
                    savedSsid.c_str(), reasonText(why));
                setReason(Reason::WrongPassword, "Couldn't join \"" + savedSsid + "\" — wrong password?");
            }
            else
            {
                LOG("WIFI", "Couldn't join \"%s\": %s", savedSsid.c_str(), reasonText(why));
                setReason(Reason::JoinFailed, "Couldn't join \"" + savedSsid + "\"");
            }
            return false;
        }

        LOG("WIFI", "Connected to \"%s\": IP %s, signal %d dBm", savedSsid.c_str(),
            WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()));
        return true;
    }

    bool startSetupAP()
    {
        // Stop the station side trying to join the home WiFi in the background: its channel hopping
        // would drop phones off the setup WiFi. AP+STA still lets the setup page scan for networks.
        WiFi.setAutoReconnect(false);
        WiFi.disconnect();

        for (int attempt = 1; attempt <= kSetupApAttempts; attempt++)
        {
            if (setWifiMode(WIFI_AP_STA) && WiFi.softAP(setupApName.c_str(), kSetupApPassword))
            {
                const IPAddress ip = WiFi.softAPIP();
                // Captive portal: every name a phone looks up points at us, so its sign-in page opens ours
                if (!captive_dns::start(ip))
                    LOG("WIFI", "Captive portal DNS didn't start");
                currentMode = Mode::SetupAP;
                LOG("WIFI", "Setup WiFi \"%s\" is on (password \"%s\"), page at http://%s", setupApName.c_str(),
                    kSetupApPassword, ip.toString().c_str());
                return true;
            }
            LOG("WIFI", "Setup WiFi didn't start (attempt %d of %d)", attempt, kSetupApAttempts);
            WiFi.mode(WIFI_OFF);
            delay(500);
        }
        return false;
    }

    void startMdns()
    {
        for (int attempt = 1; attempt <= kMdnsNameAttempts; attempt++)
        {
            const String candidate = (attempt == 1) ? wantedHostname : wantedHostname + "-" + String(attempt);
            const String fqdn = candidate + ".local";

            // Does anything already answer to this name? (The lookup goes over mDNS. A free name takes
            // about 7 s to time out.)
            const bool taken = Ping.ping(fqdn.c_str(), kMdnsPingCount);
            feedWatchdogIfWatched();
            if (taken)
            {
                LOG("MDNS", "%s is already taken", fqdn.c_str());
                continue;
            }

            if (MDNS.begin(candidate.c_str()))
            {
                MDNS.setInstanceName(setupApName); // the pumpkin's name, shown by Bonjour browsers
                MDNS.addService("http", "tcp", kWebServerPort);
                setRegisteredMdnsName(candidate);
                LOG("MDNS", "Web address http://%s", fqdn.c_str());
                return;
            }
            MDNS.end(); // so the next name starts from scratch
            LOG("MDNS", "Couldn't register %s", fqdn.c_str());
        }
        LOG("MDNS", "No web name: use the IP address");
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
        if (startSetupAP())
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
            if (!WiFi.reconnect())
                WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
            currentMode = Mode::Connecting;
            attemptStartMs = now;
        }
    }

    // Setup mode although home WiFi is saved: it wasn't in range at boot (router off?), joining failed,
    // or it was lost later. Now and then look for it, and restart to join it if it's there. Only while
    // no phone is on the setup WiFi: the scan takes the radio off its channel for about 2 s.
    void lookForHomeWifi(uint32_t now)
    {
        if (savedSsid.isEmpty() || reason == Reason::WrongPassword)
            return; // nothing to look for, or the password needs typing again
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
            break;
        }
        return status_led::Pattern::Off;
    }

    // Network housekeeping, in place of Scribe's loop() calls.
    // Priority 2: just above the Arduino loop task (1), where the effects run, so DNS replies and the
    // BOOT button's timing never wait for an effect to yield; below the async web server's task (3), lwIP
    // (18) and the WiFi driver. Each 10 ms wake-up is a few microseconds of work, so the effects' timing
    // isn't disturbed; the rare slow jobs (a 2 s scan in setup mode, starting the setup WiFi after the
    // home WiFi is lost) wait on the WiFi driver rather than using the CPU.
    // Stack: the deepest paths are the doorstep fallback (starting the setup WiFi, then the mode
    // listener, which builds messages for the eyes), the BOOT button's NVS write + restart, and a WiFi
    // scan. The spare amount is logged a minute after boot and after a fallback.
    void networkTask(void *)
    {
        const uint32_t startedMs = millis();
        bool stackReported = false;
        for (;;)
        {
            const uint32_t now = millis();
            boot_button::update();
            firmware::checkTrial();

            switch (role.load())
            {
            case Role::Starting:
                break;
            case Role::Home:
                keepConnected(now);
                break;
            case Role::Setup:
                captive_dns::serve();
                lookForHomeWifi(now);
                break;
            case Role::Broken:
                if (retryRestartsSoFar < kMaxRetryRestarts && now - brokenSinceMs >= kBrokenRestartMs)
                    retryRestart("to try the WiFi again");
                break;
            }

            status_led::set(patternFor(currentMode));
            status_led::update();

            if (!stackReported && now - startedMs >= kStackReportMs)
            {
                stackReported = true;
                reportStack("after a minute");
            }
            vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
        }
    }

    void startTask()
    {
        if (xTaskCreate(networkTask, "network", kTaskStackBytes, nullptr, kTaskPriority, nullptr) != pdPASS)
            LOG("NETWORK", "Couldn't start the network task: no captive portal, reconnects or BOOT button");
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

        const Settings s = settings::get();
        savedSsid = s.wifiSsid;
        savedPassword = s.wifiPassword;
        setupApName = apNameFor(s.pumpkinName);
        wantedHostname = settings::hostnameFor(s.pumpkinName);
        retryRestartsSoFar = takeRetryCount();

        currentMode = Mode::Connecting;
        status_led::begin();
        boot_button::begin();
        startTask(); // status LED, BOOT button and the firmware trial from now on

        // Our settings hold the WiFi details: stop the driver keeping its own copy in flash, so "forget
        // WiFi" really forgets. Must come before the first WiFi.mode().
        WiFi.persistent(false);
        // Name shown in the router's list of devices. Applied when the station starts, so this must
        // also come before WiFi.mode().
        WiFi.setHostname(wantedHostname.c_str());
        // Homes with mesh or extra access points: check every channel and join the strongest signal
        // (Scribe took the first match). This matters more at reduced transmit power.
        WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
        WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
        WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

        bool setupWifi = true;
        if (savedSsid.isEmpty())
        {
            LOG("WIFI", "No home WiFi saved yet");
        }
        else if (connectHome())
        {
            currentMode = Mode::Connected;
            connectedSinceMs = millis();
            startMdns();
            setupWifi = false;
        }

        lastRecheckMs = millis();
        if (!setupWifi)
            role = Role::Home; // the network task takes over reconnects
        else if (startSetupAP())
            role = Role::Setup; // ...or DNS and looking for the home WiFi
        else
            becomeBroken();
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
        return setupApName;
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

    // The setup WiFi is named after the pumpkin. WiFi names are 32 bytes at most.
    String apNameFor(const String &pumpkinName)
    {
        String name = pumpkinName;
        name.trim();
        if (name.length() > kMaxSsidBytes)
        {
            name.remove(kMaxSsidBytes);
            name.trim();
        }
        return name.isEmpty() ? String(kDefaultPumpkinName) : name;
    }

    int rssi()
    {
        return currentMode == Mode::Connected ? WiFi.RSSI() : 0;
    }

    void onModeChange(ModeListener listener)
    {
        modeListener = listener;
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
            // Passive, as Scribe does, so the setup WiFi keeps working while it looks
            havePageScanResults = false;
            if (WiFi.scanNetworks(true, false, true, kScanMsPerChannel) == WIFI_SCAN_RUNNING)
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
}
