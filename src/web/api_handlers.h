#pragma once
// The JSON API behind the pages. Handlers run on the async web server's task and only call player::,
// settings::, network:: and boot_button::unlocked() (all thread-safe). They never touch the eyes.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "core/settings.h"

namespace web
{
    // effects, play, words, show, status, settings, hostname, restart (api_handlers.cpp)
    void registerApiRoutes(AsyncWebServer &server);

    // wifi/scan, wifi/forget, wifi (wifi_handlers.cpp)
    void registerWifiRoutes(AsyncWebServer &server);

    // update: firmware upload (update_handler.cpp)
    void registerUpdateRoute(AsyncWebServer &server);

    // {"inShow":..,"effectId":..,"label":..}, used by /api/status and the "status" server-sent event
    void addPlayerStatus(JsonObject target);

    // Copy an optional "name" from a request body into updated.pumpkinName (/api/settings and /api/wifi).
    // Only checks it's text: settings::save() tidies and checks it, and its error goes back to the page.
    bool readNewName(JsonDocument &body, Settings &updated, String &error);
}
