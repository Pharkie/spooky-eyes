// WiFi setup API: scan for networks, save the home WiFi, forget it. Ported from Scribe Evolution
// (handleWiFiScan in src/web/api_system_handlers.cpp and handleSetupPost in api_config_handlers.cpp).
// Scribe scans synchronously inside the request; here network::scan() runs it in the background and the
// page polls, so the web server never stalls for the second or two a scan takes.

#include "web/api_handlers.h"

#include <vector>
#include "core/constants.h"
#include "core/logging.h"
#include "core/network.h"
#include "core/settings.h"
#include "player/player.h"
#include "web/web_utils.h"

namespace web
{
    namespace
    {
        constexpr size_t kMaxScanResults = 30; // keeps the reply small; the page shows the strongest first

        // GET /api/wifi/scan: 202 {"scanning":true} while looking (ask again in a second), then
        // 200 {"networks":[{"ssid","rssi","secure","channel"}]}, strongest first, one entry per name
        void handleScan(AsyncWebServerRequest *request)
        {
            std::vector<network::FoundNetwork> found;
            switch (network::scan(found))
            {
            case network::ScanStatus::Running:
            {
                JsonDocument doc;
                doc["scanning"] = true;
                sendJson(request, 202, doc);
                return;
            }
            case network::ScanStatus::Failed:
                sendError(request, 503, "The pumpkin couldn't look for WiFi just now. Try again in a few seconds.");
                return;
            case network::ScanStatus::Ready:
                break;
            }

            JsonDocument doc;
            JsonArray list = doc["networks"].to<JsonArray>();
            for (const network::FoundNetwork &net : found)
            {
                if (list.size() >= kMaxScanResults)
                {
                    break;
                }
                if (net.ssid.length() == 0)
                {
                    continue; // hidden network
                }
                JsonObject item = list.add<JsonObject>();
                item["ssid"] = net.ssid;
                item["rssi"] = net.rssi;
                item["secure"] = net.secure;
                item["channel"] = net.channel;
            }
            sendJson(request, 200, doc);
        }

        // POST /api/wifi {"ssid","password","name"(optional)}: save, reply, then restart to join it.
        // On the doorstep (strangers may be on the setup WiFi) it needs a BOOT press first.
        void handleSave(AsyncWebServerRequest *request, JsonDocument &body)
        {
            Settings updated = settings::get();
            updated.wifiPassword = ""; // no password sent = an open network
            String error;
            // Rough limits here; settings::save() has the final say (8-63 characters, or 64 hex digits)
            if (!readText(body, "ssid", "WiFi name", 1, kMaxSsidBytes, true, updated.wifiSsid, error) ||
                !readText(body, "password", "WiFi password", 0, kMaxWifiPasswordLength, false, updated.wifiPassword,
                          error) ||
                !readNewName(body, updated, error))
            {
                sendError(request, 400, error);
                return;
            }
            if (strangersMayBeConnected() && !requireBootPress(request))
            {
                return;
            }
            if (!settings::save(updated, error))
            {
                sendError(request, 400, error);
                return;
            }
            player::settingsChanged();

            // save() tidies the name, so read back what it kept
            const Settings saved = settings::get();
            const String hostname = settings::hostnameFor(saved.pumpkinName);
            LOG("WEB", "Saved home WiFi \"%s\"; restarting to join it", saved.wifiSsid.c_str());

            JsonDocument doc;
            doc["ok"] = true;
            doc["ssid"] = saved.wifiSsid;
            doc["name"] = saved.pumpkinName;
            doc["hostname"] = hostname;
            doc["address"] = "http://" + hostname + ".local";
            // The setup WiFi is named after the pumpkin, so a rename changes it too
            doc["setupSsid"] = network::apNameFor(saved.pumpkinName);
            doc["message"] = "Saved. The pumpkin is restarting and joining " + saved.wifiSsid + ".";
            sendJson(request, 200, doc);
            // Longer than usual: the phone shows the "what next" page until the setup WiFi goes
            restartSoon("Saved the home WiFi", kWifiSavedRestartDelayMs);
        }

        // POST /api/wifi/forget: back to setup mode after a restart. Needs a BOOT press on the doorstep.
        void handleForget(AsyncWebServerRequest *request, JsonDocument &)
        {
            if (strangersMayBeConnected() && !requireBootPress(request))
            {
                return;
            }
            settings::forgetWifi();
            // Named after the current name: a rename that's waiting for a restart counts
            const String setupSsid = network::apNameFor(settings::get().pumpkinName);
            LOG("WEB", "Forgot the home WiFi; restarting into setup mode");

            JsonDocument doc;
            doc["ok"] = true;
            doc["setupSsid"] = setupSsid;
            doc["message"] = "Done. The pumpkin is restarting and will make its own WiFi called \"" + setupSsid +
                             "\" (password: " + kSetupApPassword + "). Join it to choose a new home WiFi.";
            sendJson(request, 200, doc);
            restartSoon("Forgot the home WiFi");
        }
    }

    void registerWifiRoutes(AsyncWebServer &server)
    {
        // Longer paths first: the server also matches "/api/wifi" for anything under "/api/wifi/"
        server.on("/api/wifi/scan", HTTP_GET, handleScan);
        onJsonPost(server, "/api/wifi/forget", handleForget);
        onJsonPost(server, "/api/wifi", handleSave);
    }
}
