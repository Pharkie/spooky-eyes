// WiFi setup API: scan for networks, check a WiFi's password, save the home WiFi, forget it. Ported from
// Scribe Evolution (handleWiFiScan and handleTestWiFi in src/web/api_system_handlers.cpp, and
// handleSetupPost in api_config_handlers.cpp).
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

        const char *testStatusName(network::WifiTestStatus status)
        {
            switch (status)
            {
            case network::WifiTestStatus::Running:
                return "running";
            case network::WifiTestStatus::Passed:
                return "passed";
            case network::WifiTestStatus::Failed:
                return "failed";
            case network::WifiTestStatus::Unsure:
                return "unsure";
            case network::WifiTestStatus::Idle:
                break;
            }
            return "idle";
        }

        // POST /api/wifi/test {"ssid","password"}: check the pumpkin can join this WiFi before it's saved
        // (Scribe Evolution's /api/test-wifi). The network task tries it in the background:
        //   202 {"testing":true}   started: ask GET /api/wifi/test for the result
        //   200 {"skipped":true,"reason":"..."}   not on the setup WiFi (on home WiFi, say), where a
        //       check would drop the pumpkin's connection: the page saves without one
        //   409 a check is already running, 400 bad WiFi name or password (or pumpkin's name, with
        //   "field":"name"), 403 on the doorstep without a BOOT press (as for saving)
        // The pumpkin's name ("name", optional) is only checked here, so a bad one is said before the wait,
        // not after. Nothing is saved and the password is never sent back.
        void handleTestStart(AsyncWebServerRequest *request, JsonDocument &body)
        {
            String ssid;
            String password; // no password sent = an open network
            String error;
            if (!readText(body, "ssid", "WiFi name", 1, kMaxSsidBytes, true, ssid, error) ||
                !readText(body, "password", "WiFi password", 0, kMaxWifiPasswordLength, false, password, error) ||
                !settings::checkWifi(ssid, password, error))
            {
                sendError(request, 400, error);
                return;
            }
            String name;
            // (Any length here, as for saving: settings::checkName() says if it's too long)
            if (!readText(body, "name", "pumpkin's name", 0, kMaxJsonBodyBytes, false, name, error) ||
                (!name.isEmpty() && !settings::checkName(name, error)))
            {
                JsonDocument doc;
                doc["error"] = error;
                doc["field"] = "name"; // so the page can put the cursor there
                sendJson(request, 400, doc);
                return;
            }
            if (!network::isSetupAP())
            {
                JsonDocument doc;
                doc["skipped"] = true;
                doc["reason"] = "The pumpkin is on its home WiFi, and checking another would cut it off. "
                                "It saves without a check.";
                sendJson(request, 200, doc);
                return;
            }
            // Strangers on the setup WiFi mustn't use the pumpkin to try passwords on the neighbours' WiFi
            if (strangersMayBeConnected() && !requireBootPress(request))
            {
                return;
            }
            switch (network::startWifiTest(ssid, password))
            {
            case network::WifiTestStart::Started:
            {
                LOG("WEB", "Checking WiFi \"%s\"", ssid.c_str());
                JsonDocument doc;
                doc["testing"] = true;
                sendJson(request, 202, doc);
                return;
            }
            case network::WifiTestStart::Busy:
                sendError(request, 409, "A check is already running");
                return;
            case network::WifiTestStart::NotInSetup:
                break;
            }
            // The setup WiFi went (or isn't ready) between the two questions
            sendError(request, 503, "The pumpkin can't check WiFi just now. Try again in a few seconds.");
        }

        // GET /api/wifi/test: {"status":"idle|running|passed|failed","ssid","rssi","reason"}. A finished
        // check is kept for a minute.
        void handleTestResult(AsyncWebServerRequest *request)
        {
            const network::WifiTest test = network::wifiTest();
            JsonDocument doc;
            doc["status"] = testStatusName(test.status);
            doc["ssid"] = test.ssid;
            doc["rssi"] = test.rssi;
            doc["reason"] = test.reason;
            sendJson(request, 200, doc);
        }

        // POST /api/wifi {"ssid","password","name"(optional)}: save, reply, then restart to join it.
        // On the doorstep (strangers may be on the setup WiFi) it needs a BOOT press first.
        void handleSave(AsyncWebServerRequest *request, JsonDocument &body)
        {
            Settings updated = settings::get();
            const String oldName = updated.pumpkinName;
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
            LOG("WEB", "Saved home WiFi \"%s\"; restarting to join it", saved.wifiSsid.c_str());
            if (saved.pumpkinName != oldName)
            {
                settings::noteRenamed(); // the eyes say the new name once it's back
                LOG("WEB", "Renamed to \"%s\"", saved.pumpkinName.c_str());
            }

            JsonDocument doc;
            doc["ok"] = true;
            doc["ssid"] = saved.wifiSsid;
            doc["name"] = saved.pumpkinName;
            doc["address"] = String("http://") + kHostname + ".local";
            doc["setupSsid"] = network::apSsid();
            doc["message"] = "Saved. The pumpkin is restarting and joining " + saved.wifiSsid + ".";
            sendJson(request, 200, doc);
            // A moment for the reply to reach the phone, whose page then waits for the pumpkin on that WiFi
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
            const String setupSsid = network::apSsid();
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
        server.on("/api/wifi/test", HTTP_GET, handleTestResult);
        onJsonPost(server, "/api/wifi/test", handleTestStart);
        onJsonPost(server, "/api/wifi/forget", handleForget);
        onJsonPost(server, "/api/wifi", handleSave);
    }
}
