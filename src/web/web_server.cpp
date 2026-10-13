// The web server: pages from web/ (gzipped into the firmware by scripts/embed_web.py), the JSON API,
// server-sent events for the talking pumpkin, and the captive portal used in setup mode.
//
// Route setup, the captive portal and onNotFound are ported from Scribe Evolution (src/web/web_server.cpp
// there). Scribe picks its routes once at boot; here each request checks the network mode instead,
// because the pumpkin can drop into setup mode later (home WiFi gone) and come back again.
//
// Every request, and every page listening for news, tells network::noteActivity() someone's using the
// pumpkin, so the WiFi stays on (it switches off to save batteries when nobody is).

#include "web/web_server.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <freertos/timers.h>
#include "core/constants.h"
#include "core/logging.h"
#include "core/network.h"
#include "generated/web_assets.h"
#include "player/player.h"
#include "web/api_handlers.h"
#include "web/web_utils.h"

namespace web
{
    namespace
    {
        AsyncWebServer server(kWebServerPort);
        AsyncEventSource events("/events");

        constexpr const char *kSetupApAddress = "192.168.4.1"; // the ESP32's standard setup WiFi address

        // Phones and computers fetch one of these to check for internet. In setup mode they're redirected
        // to one of our pages, which makes the phone pop up its "sign in to network" sheet showing it.
        constexpr const char *kProbePaths[] = {
            "/generate_204",              // Android, Chrome
            "/gen_204",                   // Android
            "/hotspot-detect.html",       // Apple
            "/library/test/success.html", // Apple (older)
            "/connecttest.txt",           // Windows 10+
            "/ncsi.txt",                  // Windows
            "/canonical.html",            // Firefox
            "/success.txt",               // Firefox
            "/redirect",                  // Windows
        };

        // --- Server-sent events ---
        //
        // The player calls onSay/onStatusChange from the effects task. They only queue a message (never
        // wait), and a small task sends it. AsyncEventSource's list of phones has no lock: async_tcp
        // (priority 3 in AsyncTCP-esphome) adds and drops phones, and this task walks the list to send.
        // So on the single-core C3:
        // - the sender runs one step above async_tcp, so async_tcp can't drop a phone halfway through
        //   a send;
        // - the sender is only ever woken by a lower-priority task (the effects task at 1, and the
        //   FreeRTOS timer task at 1 for the keep-alive ping). Those can only run while async_tcp is
        //   blocked, never halfway through changing the list. A queue timeout would wake it from the
        //   tick interrupt instead, which can preempt async_tcp anywhere (use-after-free), so the
        //   sender waits with no timeout.

        enum class EventKind : uint8_t
        {
            Say,
            Status,
            Ping,
        };

        struct EventMessage
        {
            EventKind kind;
            char words[kMaxWordsLength + 56]; // effect phrases and typed words are shorter than this
        };

        constexpr UBaseType_t kEventTaskPriority = 4; // one above async_tcp: see above
        constexpr uint32_t kEventTaskStack = 4096;
        constexpr UBaseType_t kEventQueueLength = 6;
        constexpr uint32_t kKeepAliveMs = PumpkinTime::Seconds(25); // a ping this often lets dead connections time out
        constexpr uint32_t kReconnectMs = PumpkinTime::Seconds(3);  // browsers retry this soon after losing the connection

        QueueHandle_t eventQueue = nullptr;
        TimerHandle_t keepAliveTimer = nullptr;
        std::atomic<bool> statusQueued{false};
        std::atomic<bool> pingQueued{false};

        String playerStatusJson()
        {
            JsonDocument doc;
            addPlayerStatus(doc.to<JsonObject>());
            String json;
            serializeJson(doc, json);
            return json;
        }

        void onSay(const char *words)
        {
            if (eventQueue == nullptr || words == nullptr)
            {
                return;
            }
            EventMessage message;
            message.kind = EventKind::Say;
            strlcpy(message.words, words, sizeof(message.words));
            xQueueSend(eventQueue, &message, 0); // dropped if phones can't keep up
        }

        void onStatusChange()
        {
            // One queued status is enough: the latest status is read when it's sent
            if (eventQueue == nullptr || statusQueued.exchange(true))
            {
                return;
            }
            EventMessage message;
            message.kind = EventKind::Status;
            message.words[0] = '\0';
            if (xQueueSend(eventQueue, &message, 0) != pdTRUE)
            {
                statusQueued = false;
            }
        }

        // Runs on the FreeRTOS timer task (priority 1, below async_tcp): queue a ping, never wait
        void onKeepAliveTimer(TimerHandle_t)
        {
            if (eventQueue == nullptr || pingQueued.exchange(true))
            {
                return;
            }
            static EventMessage ping; // static: the timer task's stack is small
            ping.kind = EventKind::Ping;
            ping.words[0] = '\0';
            if (xQueueSend(eventQueue, &ping, 0) != pdTRUE)
            {
                pingQueued = false;
            }
        }

        void eventTask(void *)
        {
            EventMessage message;
            for (;;)
            {
                // No timeout, on purpose: see "Server-sent events" above
                if (xQueueReceive(eventQueue, &message, portMAX_DELAY) != pdTRUE)
                {
                    continue;
                }
                if (message.kind == EventKind::Ping)
                {
                    pingQueued = false;
                    if (events.count() > 0)
                    {
                        // A page listening for news counts as using the pumpkin. Counted here because
                        // only this task may walk the list of phones (see above).
                        network::noteActivity();
                        events.send("", "ping");
                    }
                }
                else if (message.kind == EventKind::Status)
                {
                    statusQueued = false;
                    if (events.count() > 0)
                    {
                        const String json = playerStatusJson();
                        events.send(json.c_str(), "status");
                    }
                }
                else if (events.count() > 0)
                {
                    events.send(message.words, "say");
                }
            }
        }

        // --- Pages ---

        const web_assets::Asset *findAsset(const char *path)
        {
            for (size_t i = 0; i < web_assets::kAssetCount; i++)
            {
                if (strcmp(web_assets::kAssets[i].path, path) == 0)
                {
                    return &web_assets::kAssets[i];
                }
            }
            return nullptr;
        }

        void sendAsset(AsyncWebServerRequest *request, const web_assets::Asset &asset)
        {
            // The phone checks each time (pages change with firmware updates), but an unchanged file
            // costs only a tiny "304 Not Modified"
            if (request->hasHeader("If-None-Match") && request->header("If-None-Match") == asset.etag)
            {
                AsyncWebServerResponse *notModified = request->beginResponse(304);
                notModified->addHeader("ETag", asset.etag);
                request->send(notModified);
                return;
            }
            AsyncWebServerResponse *response = request->beginResponse_P(200, asset.contentType, asset.data, asset.length);
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("ETag", asset.etag);
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
        }

        void sendPage(AsyncWebServerRequest *request, const char *path)
        {
            const web_assets::Asset *asset = findAsset(path);
            if (asset == nullptr)
            {
                request->send(500, "text/plain", "This page is missing from the firmware (run scripts/embed_web.py)");
                return;
            }
            sendAsset(request, *asset);
        }

        // Setup mode, never set up: there's nothing to do but choose the home WiFi
        bool needsSetup()
        {
            return network::isSetupAP() && !network::hasSavedWifi();
        }

        // --- Captive portal ---

        // Where phones on the setup WiFi are sent: the buttons if the pumpkin has just lost its home WiFi
        // (on the doorstep, say: people want to play), otherwise the setup page (never set up, or the
        // home WiFi turned the pumpkin away, e.g. a wrong password)
        String portalUrl()
        {
            String address = network::ipAddress();
            if (address.length() == 0)
            {
                address = kSetupApAddress;
            }
            return "http://" + address + (network::homeWifiOutOfReach() ? "/" : "/setup");
        }

        bool isProbePath(const String &url)
        {
            for (const char *probe : kProbePaths)
            {
                if (url == probe)
                {
                    return true;
                }
            }
            return false;
        }

        // Was this request meant for the pumpkin, or for some website the setup WiFi's DNS sent here?
        bool isOurHost(const String &hostHeader)
        {
            if (hostHeader.length() == 0)
            {
                return true; // very old clients send no Host
            }
            String host = hostHeader;
            const int colon = host.indexOf(':');
            if (colon >= 0)
            {
                host = host.substring(0, colon);
            }
            host.toLowerCase();
            if (host == kSetupApAddress || host == network::ipAddress())
            {
                return true;
            }
            String mdns = network::mdnsHostname();
            mdns.toLowerCase();
            return mdns.length() > 0 && host == mdns + ".local";
        }

        // Checked first: notes that someone's using the pumpkin, then lets the other handlers have the
        // request. The one place requests are counted, so the WiFi stays on while they come (a firmware
        // upload, one long request, also notes each piece as it arrives).
        class ActivityHandler : public AsyncWebHandler
        {
        public:
            bool canHandle(AsyncWebServerRequest *) override
            {
                network::noteActivity();
                return false;
            }
        };

        // Checked before every other route. In setup mode, anything for another website and the OS
        // internet checks go to portalUrl() (Scribe redirects these in its onNotFound).
        class CaptivePortalHandler : public AsyncWebHandler
        {
        public:
            bool canHandle(AsyncWebServerRequest *request) override
            {
                return network::isSetupAP() && (isProbePath(request->url()) || !isOurHost(request->host()));
            }

            void handleRequest(AsyncWebServerRequest *request) override
            {
                const String target = portalUrl();
                if (isProbePath(request->url())) // the phone's "is there internet?" check: worth a line in the log
                    LOG("WEB", "Sign-in check http://%s%s -> %s", request->host().c_str(), request->url().c_str(),
                        target.c_str());
                request->redirect(target);
            }
        };

        void handleNotFound(AsyncWebServerRequest *request)
        {
            const bool api = request->url().startsWith("/api/");
            if (!api && network::isSetupAP() && request->method() == HTTP_GET)
            {
                request->redirect(portalUrl());
                return;
            }
            if (api)
            {
                sendError(request, 404, "There's no " + String(request->methodToString()) + " " + request->url());
                return;
            }
            request->send(404, "text/html",
                          "<!doctype html><meta name=viewport content=\"width=device-width\">"
                          "<p style=\"font:20px system-ui\">Nothing here. <a href=\"/\">Back to the pumpkin</a></p>");
        }

        void addPageRoutes()
        {
            server.on("/", HTTP_GET,
                      [](AsyncWebServerRequest *request)
                      {
                          // "?play" is the setup page's "Just play with the pumpkin" link
                          if (needsSetup() && !request->hasParam("play"))
                          {
                              request->redirect("/setup");
                              return;
                          }
                          sendPage(request, "/index.html");
                      });
            server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) { sendPage(request, "/setup.html"); });
            server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) { sendPage(request, "/settings.html"); });
            server.on("/show", HTTP_GET, [](AsyncWebServerRequest *request) { sendPage(request, "/show.html"); });

            // Every file in web/ at its own path: /style.css, /common.js, /index.html...
            for (size_t i = 0; i < web_assets::kAssetCount; i++)
            {
                const web_assets::Asset *asset = &web_assets::kAssets[i];
                server.on(asset->path, HTTP_GET, [asset](AsyncWebServerRequest *request) { sendAsset(request, *asset); });
            }
        }
    }

    void begin()
    {
        server.addHandler(new ActivityHandler());      // first two, so they see every request
        server.addHandler(new CaptivePortalHandler());
        addPageRoutes();
        registerApiRoutes(server);
        registerShowRoutes(server);
        registerWifiRoutes(server);
        registerUpdateRoute(server);

        events.onConnect(
            [](AsyncEventSourceClient *client)
            {
                const String json = playerStatusJson();
                client->send(json.c_str(), "status", 0, kReconnectMs);
            });
        server.addHandler(&events);
        server.onNotFound(handleNotFound);

        eventQueue = xQueueCreate(kEventQueueLength, sizeof(EventMessage));
        if (eventQueue == nullptr ||
            xTaskCreate(eventTask, "web_events", kEventTaskStack, nullptr, kEventTaskPriority, nullptr) != pdPASS)
        {
            LOG("WEB", "Couldn't start the events task: phones won't hear the talking pumpkin");
        }
        else
        {
            player::onSay(onSay);
            player::onStatusChange(onStatusChange);
            keepAliveTimer = xTimerCreate("web_ping", pdMS_TO_TICKS(kKeepAliveMs), pdTRUE, nullptr, onKeepAliveTimer);
            if (keepAliveTimer == nullptr || xTimerStart(keepAliveTimer, 0) != pdPASS)
            {
                LOG("WEB", "Couldn't start the keep-alive timer: dead phone connections may linger");
            }
        }

        server.begin();
        LOG("WEB", "Web server on port %u (%u files)", static_cast<unsigned>(kWebServerPort),
            static_cast<unsigned>(web_assets::kAssetCount));
    }
}
