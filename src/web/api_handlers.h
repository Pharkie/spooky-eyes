#pragma once
// The JSON API behind the pages. Handlers run on the async web server's task and only call player::,
// settings::, network::, show_store:: and boot_button::unlocked() (all thread-safe). They never touch
// the eyes.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "core/settings.h"

namespace web
{
    // effects, play, words, mode, status, settings, hostname, restart (api_handlers.cpp)
    void registerApiRoutes(AsyncWebServer &server);

    // show (GET and POST), show/reset: the show creator (show_handlers.cpp)
    void registerShowRoutes(AsyncWebServer &server);

    // wifi/scan, wifi/test, wifi/forget, wifi (wifi_handlers.cpp)
    void registerWifiRoutes(AsyncWebServer &server);

    // update: firmware upload (update_handler.cpp)
    void registerUpdateRoute(AsyncWebServer &server);

    // {"mode":"show"|"buttons","effectId":..,"label":..,"block":n}, used by /api/status and the
    // "status" server-sent event. block is the show block playing (0-based), -1 in Buttons mode.
    void addPlayerStatus(JsonObject target);

    // Copy an optional "name" from a request body into updated.pumpkinName (/api/settings and /api/wifi).
    // Only checks it's text: settings::save() tidies and checks it, and its error goes back to the page.
    bool readNewName(JsonDocument &body, Settings &updated, String &error);
}
