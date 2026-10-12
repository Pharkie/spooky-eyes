// Spooky Pumpkin Eyes: ESP32-C3 SuperMini firmware.
// Boot order: settings -> saved show -> eyes -> WiFi (home, or the pumpkin's own setup network) -> web
// server -> show. WiFi joins in the background, so the show starts at once; the eyes say how to reach the
// pumpkin when it's done. To save batteries, the WiFi switches off when nobody has used it for a while
// (kWifiIdleOffMs); a short BOOT press brings it back.

#include <Arduino.h>
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
    // http:// in front, or phone browsers search for "jacks-pumpkin.local" instead of opening it
    return " " + (host.length() ? "http://" + host + ".local  or  " + ip : "http://" + ip) + " ";
}

// Tell people on the eyes. Setup instructions repeat at the start of every show pass for a while (then
// stop, so they don't scroll the password at trick-or-treaters all night); the web address scrolls once.
// `now` also scrolls the setup instructions straight away, rather than at the next show pass.
static void tellHowToConnect(bool now)
{
    if (network::mode() == network::Mode::Off)
    {
        // Nobody was using it, so the WiFi is off to save batteries: say so once, and stop the setup
        // instructions (there's no WiFi to join now)
        player::setShowMessage("");
        player::announce(" WiFi off to save batteries ");
    }
    else if (network::isSetupAP())
    {
        player::setShowMessage(howToConnect(), kSetupMessageMs);
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
// over. If that's why it last restarted, run the eyes without WiFi until it's switched off and on again.
static bool batteriesTooWeakForWifi()
{
    return esp_reset_reason() == ESP_RST_BROWNOUT;
}

void setup()
{
    // Slower processor, longer batteries (kCpuMhz). The peripherals' clock (APB) stays at 80 MHz, so the
    // eyes' SPI, the mouth LED's PWM and the serial port keep their speeds; effects time in milliseconds.
    const bool slowed = setCpuFrequencyMhz(kCpuMhz);
    Serial.begin(115200);
    LOG("BOOT", "Spooky Pumpkin Eyes %s", FIRMWARE_VERSION);
    if (!slowed)
        LOG("BOOT", "Couldn't slow the processor to %lu MHz", static_cast<unsigned long>(kCpuMhz));

    settings::begin();
    show_store::begin(); // the saved show, which the player loads
    player::begin();     // eyes show a picture straight away; loop() starts the show

    if (batteriesTooWeakForWifi())
    {
        LOG("BOOT", "Restarted by a brownout: no WiFi until the batteries are changed");
        player::setShowMessage(" New batteries please ");
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
    // A short press on the BOOT button shows how to reach it again, or brings the WiFi back if it was
    // switched off to save batteries (a restart joins the home WiFi or starts the setup WiFi afresh)
    boot_button::onShortPress(
        []
        {
            if (network::mode() == network::Mode::Off)
                firmware::restart("BOOT pressed: WiFi back on");
            tellHowToConnect(true);
        });

    network::begin(); // quick: WiFi joins in the background
    web::begin();     // after network::begin(), which starts the network stack the server needs
}

void loop()
{
    player::loop();
    firmware::checkTrial(); // the network task does this too, but it doesn't run after a brownout
}
