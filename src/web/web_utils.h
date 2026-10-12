#pragma once
// Small helpers shared by the web handlers: request bodies, the CSRF header, JSON replies, input checks,
// the BOOT button gate and a delayed restart. Ported and simplified from Scribe Evolution
// (src/web/web_server.cpp and src/web/validation.cpp there).

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "core/constants.h"

namespace web
{
    // Every POST must carry this header. A form on another website can't add custom headers without a
    // CORS preflight, and we never answer preflights, so this blocks cross-site requests cheaply.
    constexpr const char *kCsrfHeader = "X-Pumpkin";
    constexpr const char *kCsrfValue = "1";

    // Largest JSON body we accept (words are capped at kMaxWordsLength characters, so this is generous)
    constexpr size_t kMaxJsonBodyBytes = 1024;
    // ...except for saving a show: kMaxShowBlocks blocks of kMaxWordsLength letters is about 13 KB
    constexpr size_t kMaxShowBodyBytes = 16 * 1024;

    using JsonPostHandler = void (*)(AsyncWebServerRequest *request, JsonDocument &body);

    // Register a POST route that checks the CSRF header, collects and parses a JSON body of up to
    // maxBodyBytes (an empty body gives an empty document), then calls handler. Only the exact path
    // counts (the library would also send "/api/show/anything" here). Bad requests get a JSON error.
    void onJsonPost(AsyncWebServer &server, const char *path, JsonPostHandler handler,
                    size_t maxBodyBytes = kMaxJsonBodyBytes);

    bool hasCsrfHeader(AsyncWebServerRequest *request);

    // Replies: {"ok":true,...} or {"error":"..."}
    void sendJson(AsyncWebServerRequest *request, int code, const JsonDocument &doc);
    void sendOk(AsyncWebServerRequest *request, const String &message = String());
    void sendError(AsyncWebServerRequest *request, int code, const String &message);

    // --- Input checks (from Scribe Evolution's validation.cpp) ---

    // A text field from a JSON body. Fails if it's missing (when required), not a string, or its
    // length in bytes is outside minLength..maxLength. Missing and not required leaves value alone.
    // error says why, in words a parent understands.
    bool readText(JsonDocument &body, const char *key, const char *what, size_t minLength, size_t maxLength,
                  bool required, String &value, String &error);

    // An integer field in min..max. Missing is fine unless required (value is left alone).
    bool readInt(JsonDocument &body, const char *key, const char *what, int minValue, int maxValue,
                 bool required, int &value, bool &present, String &error);

    // text::plain() for a String: curly quotes and dashes made plain, accents folded, control characters
    // turned into spaces, emoji dropped, trimmed
    String plainText(const String &text);

    // --- The BOOT button gate ---
    //
    // The setup WiFi's password is printed in the instructions and the same on every pumpkin, so anyone
    // nearby can join it. A short press of the BOOT button (boot_button::unlocked()) proves someone is
    // holding the pumpkin.

    // True on the setup WiFi with home WiFi already saved: the pumpkin is probably on the doorstep and
    // strangers may be connected, so changing the WiFi or the name needs a BOOT press. A brand-new
    // pumpkin stays open for setup, and on the home WiFi (a trusted network) these changes stay open.
    bool strangersMayBeConnected();

    constexpr const char *kBootPressMessage = "Press the small BOOT button on the pumpkin's board, then try again";

    // 403 {"error":kBootPressMessage,"needsBoot":true}
    void sendBootNeeded(AsyncWebServerRequest *request);

    // True if BOOT was pressed lately. Otherwise sends the 403 above and returns false.
    bool requireBootPress(AsyncWebServerRequest *request);

    // --- Restarting ---

    // Restart the board through firmware::restart() after delayMs, so the reply (and the page's next
    // steps) get out first. reason must be a string literal (it's logged after this returns). Only the
    // first call counts.
    void restartSoon(const char *reason, uint32_t delayMs = kRestartDelayMs);
}
