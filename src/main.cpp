// Spooky Pumpkin Eyes: ESP32-C3 SuperMini firmware.
// Boot order: settings -> saved show -> eyes -> WiFi (home, or the pumpkin's own setup network) -> web
// server -> show. WiFi joins in the background, so the show starts at once; the eyes say how to reach the
// pumpkin when it's done. To save batteries, the WiFi switches off when nobody has used it for a while
// (kWifiIdleOffMs); a short BOOT press brings it back.

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <esp_system.h>
#include "core/constants.h"
#include "core/firmware.h"
#include "core/logging.h"
#include "core/network.h"
#include "core/settings.h"
#include "core/show_store.h"
#include "hardware/boot_button.h"
#include "player/player.h"
#include "web/web_server.h"

// Set (on the network task) once WiFi has finished starting up: home WiFi joined, or the setup WiFi on
static std::atomic<bool> wifiReady{false};

// How to reach the pumpkin, for scrolling on the eyes: its web address, or how to set it up
static String howToConnect()
{
    if (!wifiReady)
        return " Joining WiFi "; // a BOOT press in the first few seconds: the address comes soon
    // (Why the home WiFi didn't work goes on the setup page, not here: the eyes keep it short.)
    if (network::isSetupAP())
        return " Join WiFi: " + network::apSsid() + "  Password: " + kSetupApPassword + "  then open 192.168.4.1 ";

    const String ip = network::ipAddress();
    if (ip.isEmpty())
        return " No WiFi ";
    const String host = network::mdnsHostname();
    // http:// in front, or phone browsers search for "pumpkin.local" instead of opening it
    return " " + (host.length() ? "http://" + host + ".local  or  " + ip : "http://" + ip) + " ";
}

// Tell people on the eyes. The setup instructions scroll now (if `now`) and at the start of the next show
// pass (kSetupMessageRepeats), then the show plays on by itself: WiFi is optional. The web address
// scrolls once.
static void tellHowToConnect(bool now)
{
    if (network::mode() == network::Mode::Off)
    {
        // Nobody was using it, so the WiFi is off to save batteries: stop the setup instructions (there's
        // no WiFi to join now) and let the show play on without a word about it
        player::setShowMessage("");
    }
    else if (network::isSetupAP())
    {
        player::setShowMessage(howToConnect(), kSetupMessageRepeats);
        if (now)
            player::announce(howToConnect());
    }
    else
    {
        player::setShowMessage("");
        player::announce(howToConnect());
    }
}

// Tired batteries sag during WiFi's current bursts and brown the board out, so it would restart over and
// over. But one brownout can also be a knock to the battery box or a bouncing switch, so only two in a row
// (without kBrownoutForgetMs of running in between) mean "no WiFi until switched off and on". The count
// lives in NVS: it has to survive the brownout.
static constexpr const char *kPowerNamespace = "power";
static constexpr uint32_t kBrownoutForgetMs = PumpkinTime::Minutes(2);
static bool brownoutsToForget = false;

static bool batteriesTooWeakForWifi()
{
    Preferences power;
    if (!power.begin(kPowerNamespace, false))
        return esp_reset_reason() == ESP_RST_BROWNOUT; // no NVS: as before
    const uint8_t before = power.getUChar("brownouts", 0);
    const uint8_t count = esp_reset_reason() == ESP_RST_BROWNOUT ? before + 1 : 0;
    if (count != before)
        power.putUChar("brownouts", count);
    power.end();
    brownoutsToForget = count > 0;
    if (count == 1)
        LOG("BOOT", "Restarted by a brownout: trying WiFi anyway (a second one in a row turns it off)");
    return count >= 2;
}

// Running fine for a while since a brownout: it was a one-off
static void forgetBrownouts()
{
    if (!brownoutsToForget || millis() < kBrownoutForgetMs)
        return;
    brownoutsToForget = false;
    Preferences power;
    if (power.begin(kPowerNamespace, false))
    {
        power.putUChar("brownouts", 0);
        power.end();
    }
}

void setup()
{
    // Slower processor, longer batteries (kCpuMhz). The peripherals' clock (APB) stays at 80 MHz, so the
    // eyes' SPI, the mouth LED's PWM and the serial port keep their speeds; effects time in milliseconds.
    const bool slowed = setCpuFrequencyMhz(kCpuMhz);
    Serial.begin(115200);
    // Plugged into a computer that isn't reading the USB port, a log line waits at most this long before
    // it's dropped (100 ms by default). Not 0: in this core that means wait for ever.
    Serial.setTxTimeoutMs(10);
    LOG("BOOT", "Spooky Pumpkin Eyes %s", FIRMWARE_VERSION);
    if (!slowed)
        LOG("BOOT", "Couldn't slow the processor to %lu MHz", static_cast<unsigned long>(kCpuMhz));

    settings::begin();
    show_store::begin(); // the saved show, which the player loads
    player::begin();     // eyes show a picture straight away; loop() starts the show

    // A short press on the BOOT button shows how to reach it again, or brings the WiFi back if it's off
    // (to save batteries, or after brownouts): a restart joins the home WiFi or starts the setup WiFi
    // afresh. Runs on the network task, so set it before that starts.
    boot_button::onShortPress(
        []
        {
            if (network::mode() == network::Mode::Off)
                firmware::restart("BOOT pressed: WiFi back on");
            tellHowToConnect(true);
        });

    if (batteriesTooWeakForWifi())
    {
        LOG("BOOT", "Two brownouts in a row: no WiFi until the batteries are changed (or BOOT is pressed)");
        player::setShowMessage(" New batteries please ");
        network::beginOffline(); // the BOOT button still works
        return;
    }

    // Both run on the network task, so set them before it starts. The first call says how WiFi started
    // (the show is already playing, so scroll it now); later ones mean the home WiFi was lost or found.
    network::onModeChange(
        [](network::Mode)
        {
            wifiReady = true;
            tellHowToConnect(true);
        });
    network::begin(); // quick: WiFi joins in the background
    web::begin();     // after network::begin(), which starts the network stack the server needs
}

void loop()
{
    player::loop();
    firmware::checkTrial(); // the network task does this too
    forgetBrownouts();
}
