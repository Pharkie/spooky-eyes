// JSON API: effects and buttons, status, settings and restart. WiFi and firmware updates are in
// wifi_handlers.cpp and update_handler.cpp. Requests are checked for shape here (Scribe Evolution's
// validation.cpp); settings::save() decides whether the values themselves will do.

#include "web/api_handlers.h"

#include "core/constants.h"
#include "core/logging.h"
#include "core/network.h"
#include "core/settings.h"
#include "effects/registry.h"
#include "player/player.h"
#include "web/web_utils.h"

namespace web
{
    namespace
    {
        constexpr size_t kMaxEffectIdLength = 32;
        constexpr size_t kUtf8BytesPerChar = 4; // allow for emoji etc. before tidying

        const char *networkModeName()
        {
            switch (network::mode())
            {
            case network::Mode::Connecting:
                return "connecting";
            case network::Mode::Connected:
                return "connected";
            case network::Mode::SetupAP:
                return "setup";
            case network::Mode::Disconnected:
                return "disconnected";
            }
            return "unknown";
        }

        void addSettings(JsonObject target, const Settings &current)
        {
            target["name"] = current.pumpkinName;
            target["brightness"] = current.brightness;
            target["mouthLed"] = current.mouthLed;
            target["eyeGap"] = current.eyeGap;
            target["hostname"] = settings::hostnameFor(current.pumpkinName);
        }

        // GET /api/effects: the buttons, in registry order
        void handleEffects(AsyncWebServerRequest *request)
        {
            size_t count = 0;
            const EffectInfo *effects = allEffects(count);
            JsonDocument doc;
            JsonArray list = doc["effects"].to<JsonArray>();
            for (size_t i = 0; i < count; i++)
            {
                JsonObject item = list.add<JsonObject>();
                item["id"] = effects[i].id;
                item["label"] = effects[i].label;
                item["emoji"] = effects[i].emoji;
                item["group"] = groupName(effects[i].group);
            }
            sendJson(request, 200, doc);
        }

        // POST /api/play {"id":"creeper"}
        void handlePlay(AsyncWebServerRequest *request, JsonDocument &body)
        {
            String id;
            String error;
            if (!readText(body, "id", "effect", 1, kMaxEffectIdLength, true, id, error))
            {
                sendError(request, 400, error);
                return;
            }
            if (!player::play(id))
            {
                sendError(request, 404, "The pumpkin doesn't know an effect called " + id);
                return;
            }
            sendOk(request);
        }

        // POST /api/words {"words":"Hello there"}
        void handleWords(AsyncWebServerRequest *request, JsonDocument &body)
        {
            String words;
            String error;
            if (!readText(body, "words", "words", 1, kMaxWordsLength * kUtf8BytesPerChar, true, words, error))
            {
                sendError(request, 400, error);
                return;
            }

            // The eyes' font only has plain letters and symbols, so emoji are dropped (as the Perl page
            // did). Check what's left, so an all-emoji message gets a helpful answer.
            const String plain = plainText(words);
            if (plain.length() == 0)
            {
                sendError(request, 400, "Type some words first (the eyes can show letters, numbers and symbols)");
                return;
            }
            if (plain.length() > kMaxWordsLength)
            {
                sendError(request, 400, String("That's too long: up to ") + kMaxWordsLength + " letters");
                return;
            }
            player::scrollWords(plain);
            sendOk(request);
        }

        // POST /api/show: back to the normal show
        void handleShow(AsyncWebServerRequest *request, JsonDocument &)
        {
            player::backToShow();
            sendOk(request);
        }

        // GET /api/status
        void handleStatus(AsyncWebServerRequest *request)
        {
            const Settings current = settings::get();
            JsonDocument doc;
            doc["name"] = current.pumpkinName;
            doc["defaultName"] = kDefaultPumpkinName; // a pumpkin still called this hasn't been named yet
            doc["version"] = FIRMWARE_VERSION;
            doc["uptime"] = millis() / 1000;
            doc["freeHeap"] = ESP.getFreeHeap();
            doc["mode"] = networkModeName();
            doc["hasSavedWifi"] = network::hasSavedWifi();
            doc["homeSsid"] = network::homeSsid();
            doc["ip"] = network::ipAddress();
            doc["hostname"] = network::mdnsHostname();
            doc["rssi"] = network::rssi();
            doc["setupSsid"] = network::apSsid();
            doc["setupReason"] = network::setupReason(); // always says why in setup mode with home WiFi saved
            doc["homeWifiOutOfReach"] = network::homeWifiOutOfReach(); // e.g. carried out to the doorstep
            doc["wifiNeedsBoot"] = strangersMayBeConnected(); // WiFi and name changes need a BOOT press
            addPlayerStatus(doc["player"].to<JsonObject>());
            sendJson(request, 200, doc);
        }

        // GET /api/settings
        void handleGetSettings(AsyncWebServerRequest *request)
        {
            JsonDocument doc;
            addSettings(doc.to<JsonObject>(), settings::get());
            JsonObject limits = doc["limits"].to<JsonObject>();
            limits["maxBrightness"] = kMaxBrightness;
            limits["maxEyeGap"] = kMaxEyeGap;
            limits["maxNameLength"] = kMaxNameLength;
            limits["maxWordsLength"] = kMaxWordsLength;
            limits["maxSsidBytes"] = kMaxSsidBytes;
            limits["minWifiPassword"] = kMinWifiPasswordLength;
            limits["maxWifiPassword"] = kMaxWifiPasswordLength; // 64 only as hex digits
            sendJson(request, 200, doc);
        }

        // POST /api/settings: any of {"name","brightness","mouthLed","eyeGap"}
        void handlePostSettings(AsyncWebServerRequest *request, JsonDocument &body)
        {
            Settings updated = settings::get();
            const String oldName = updated.pumpkinName;
            String error;
            if (!readNewName(body, updated, error))
            {
                sendError(request, 400, error);
                return;
            }

            int brightness = updated.brightness;
            int eyeGap = updated.eyeGap;
            bool present = false;
            if (!readInt(body, "brightness", "brightness", 0, kMaxBrightness, false, brightness, present, error) ||
                !readInt(body, "eyeGap", "gap between the eyes", 0, kMaxEyeGap, false, eyeGap, present, error))
            {
                sendError(request, 400, error);
                return;
            }
            updated.brightness = static_cast<uint8_t>(brightness);
            updated.eyeGap = static_cast<uint8_t>(eyeGap);

            JsonVariant mouth = body["mouthLed"];
            if (!mouth.isNull())
            {
                if (!mouth.is<bool>())
                {
                    sendError(request, 400, "The mouth light setting should be on or off");
                    return;
                }
                updated.mouthLed = mouth.as<bool>();
            }

            // The name also names the setup WiFi, so a stranger on the doorstep mustn't change it
            const bool renaming = plainText(updated.pumpkinName) != oldName;
            if (renaming && strangersMayBeConnected() && !requireBootPress(request))
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
            const bool renamed = saved.pumpkinName != oldName;
            JsonDocument doc;
            doc["ok"] = true;
            doc["restartNeeded"] = renamed;
            if (renamed)
            {
                const String address = "http://" + settings::hostnameFor(saved.pumpkinName) + ".local";
                const String setupSsid = network::apNameFor(saved.pumpkinName);
                doc["address"] = address;     // the web address after the restart
                doc["setupSsid"] = setupSsid; // the setup WiFi's name after the restart
                doc["message"] = "Saved. After a restart, the pumpkin's web address will be " + address +
                                 " and its setup WiFi will be called \"" + setupSsid + "\".";
                LOG("WEB", "Renamed to \"%s\"", saved.pumpkinName.c_str());
            }
            else
            {
                doc["message"] = "Saved";
            }
            addSettings(doc["settings"].to<JsonObject>(), saved);
            sendJson(request, 200, doc);
        }

        // GET /api/hostname?name=Jack's Pumpkin -> {"hostname":"jacks-pumpkin"}, for the setup page's hint
        void handleHostname(AsyncWebServerRequest *request)
        {
            String name = request->hasParam("name") ? request->getParam("name")->value() : String();
            if (name.length() > kMaxNameLength * kUtf8BytesPerChar)
            {
                name = name.substring(0, kMaxNameLength * kUtf8BytesPerChar);
            }
            JsonDocument doc;
            doc["hostname"] = settings::hostnameFor(plainText(name));
            sendJson(request, 200, doc);
        }

        // POST /api/restart
        void handleRestart(AsyncWebServerRequest *request, JsonDocument &)
        {
            sendOk(request, "The pumpkin is restarting. It'll be back in about 20 seconds.");
            restartSoon("Restart button on the Settings page");
        }
    }

    void addPlayerStatus(JsonObject target)
    {
        const player::Status status = player::status();
        target["inShow"] = status.inShow;
        target["effectId"] = status.effectId;
        target["label"] = status.label;
    }

    bool readNewName(JsonDocument &body, Settings &updated, String &error)
    {
        // Any length will do here: the body is capped, and settings::save() says if it's too long
        return readText(body, "name", "pumpkin's name", 0, kMaxJsonBodyBytes, false, updated.pumpkinName, error);
    }

    void registerApiRoutes(AsyncWebServer &server)
    {
        server.on("/api/effects", HTTP_GET, handleEffects);
        server.on("/api/status", HTTP_GET, handleStatus);
        server.on("/api/settings", HTTP_GET, handleGetSettings);
        server.on("/api/hostname", HTTP_GET, handleHostname);
        onJsonPost(server, "/api/play", handlePlay);
        onJsonPost(server, "/api/words", handleWords);
        onJsonPost(server, "/api/show", handleShow);
        onJsonPost(server, "/api/settings", handlePostSettings);
        onJsonPost(server, "/api/restart", handleRestart);
    }
}
