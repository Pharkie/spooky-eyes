// Shared web helpers. Body collection and the JSON checks are ported from Scribe Evolution
// (handleChunkedUpload in src/web/web_server.cpp, and src/web/validation.cpp), with two changes:
// bodies are capped, and they're kept in a malloc'd buffer because the library free()s _tempObject.

#include "web/web_utils.h"

#include <atomic>
#include <cstring>
#include <memory>
#include "core/firmware.h"
#include "core/logging.h"
#include "core/network.h"
#include "core/text.h"
#include "hardware/boot_button.h"

namespace web
{
    namespace
    {
        // Collect a request body that may arrive in several pieces. Bodies over maxBytes are
        // dropped here and rejected when the request completes.
        void collectBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total,
                         size_t maxBytes)
        {
            if (total > maxBytes || index + len > total)
            {
                return;
            }
            if (index == 0)
            {
                free(request->_tempObject);
                request->_tempObject = malloc(total + 1);
            }
            char *buffer = static_cast<char *>(request->_tempObject);
            if (buffer == nullptr)
            {
                return;
            }
            memcpy(buffer + index, data, len);
            buffer[index + len] = '\0';
        }

        constexpr uint32_t kRestartTaskStack = 3072; // logging, and marking the firmware as good (flash)

        // Set once by the first restartSoon(), then only read by the restart task
        std::atomic<bool> restartScheduled{false};
        const char *restartReason = "";
        uint32_t restartDelayMs = 0;

        void restartTask(void *)
        {
            vTaskDelay(pdMS_TO_TICKS(restartDelayMs));
            firmware::restart(restartReason);
        }
    }

    bool hasCsrfHeader(AsyncWebServerRequest *request)
    {
        return request->hasHeader(kCsrfHeader) && request->header(kCsrfHeader) == kCsrfValue;
    }

    void onJsonPost(AsyncWebServer &server, const char *path, JsonPostHandler handler, size_t maxBodyBytes)
    {
        server.on(
            path, HTTP_POST,
            [path, handler, maxBodyBytes](AsyncWebServerRequest *request)
            {
                if (request->url() != path)
                {
                    sendError(request, 404, "There's no POST " + request->url());
                    return;
                }
                if (!hasCsrfHeader(request))
                {
                    sendError(request, 403, "Missing X-Pumpkin header");
                    return;
                }
                if (request->contentLength() > maxBodyBytes)
                {
                    sendError(request, 413, "That's too much to send");
                    return;
                }
                JsonDocument body;
                const char *text = static_cast<const char *>(request->_tempObject);
                if (request->contentLength() > 0)
                {
                    if (text == nullptr)
                    {
                        // Form-encoded bodies never reach collectBody, and malloc can fail
                        sendError(request, 400, "The pumpkin couldn't read that request (it expects JSON)");
                        return;
                    }
                    DeserializationError parseError = deserializeJson(body, text);
                    // The document has its own copy now: free the body (up to 16 KB) before the handler runs
                    free(request->_tempObject);
                    request->_tempObject = nullptr;
                    if (parseError || !body.is<JsonObject>())
                    {
                        sendError(request, 400, "That request didn't make sense (bad JSON)");
                        return;
                    }
                }
                handler(request, body);
            },
            nullptr,
            [maxBodyBytes](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            { collectBody(request, data, len, index, total, maxBodyBytes); });
    }

    void sendJson(AsyncWebServerRequest *request, int code, const JsonDocument &doc)
    {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->setCode(code);
        response->addHeader("Cache-Control", "no-store");
        serializeJson(doc, *response);
        request->send(response);
    }

    void sendOk(AsyncWebServerRequest *request, const String &message)
    {
        JsonDocument doc;
        doc["ok"] = true;
        if (message.length() > 0)
        {
            doc["message"] = message;
        }
        sendJson(request, 200, doc);
    }

    void sendError(AsyncWebServerRequest *request, int code, const String &message)
    {
        LOG("WEB", "%s %s -> %d %s", request->methodToString(), request->url().c_str(), code, message.c_str());
        JsonDocument doc;
        doc["error"] = message;
        sendJson(request, code, doc);
    }

    bool readText(JsonDocument &body, const char *key, const char *what, size_t minLength, size_t maxLength,
                  bool required, String &value, String &error)
    {
        JsonVariant field = body[key];
        if (field.isNull())
        {
            if (required)
            {
                error = String("Please fill in the ") + what;
                return false;
            }
            return true;
        }
        if (!field.is<const char *>())
        {
            error = String("The ") + what + " should be text";
            return false;
        }
        // Reject a NUL inside the text ("\u0000" in the JSON): stored as a C string it would cut the rest off.
        // (Not String::indexOf('\0'), which finds the terminator at the end of every string.)
        const JsonString raw = field.as<JsonString>();
        if (memchr(raw.c_str(), '\0', raw.size()) != nullptr)
        {
            error = String("The ") + what + " has a strange character in it";
            return false;
        }
        String text = raw.c_str();
        if (text.length() < minLength)
        {
            error = minLength <= 1 ? String("Please fill in the ") + what
                                   : String("The ") + what + " needs at least " + minLength + " characters";
            return false;
        }
        if (text.length() > maxLength)
        {
            error = String("The ") + what + " is too long (up to " + maxLength + " characters)";
            return false;
        }
        value = text;
        return true;
    }

    bool readInt(JsonDocument &body, const char *key, const char *what, int minValue, int maxValue,
                 bool required, int &value, bool &present, String &error)
    {
        JsonVariant field = body[key];
        present = !field.isNull();
        if (!present)
        {
            if (required)
            {
                error = String("Please choose the ") + what;
                return false;
            }
            return true;
        }
        if (!field.is<int>())
        {
            error = String("The ") + what + " should be a whole number";
            return false;
        }
        int number = field.as<int>();
        if (number < minValue || number > maxValue)
        {
            error = String("The ") + what + " should be from " + minValue + " to " + maxValue;
            return false;
        }
        value = number;
        return true;
    }

    String plainText(const String &text)
    {
        // Plain text is never longer than what came in
        const size_t size = text.length() + 1;
        std::unique_ptr<char[]> buffer(new (std::nothrow) char[size]);
        if (!buffer)
        {
            return String();
        }
        text::plain(text.c_str(), buffer.get(), size);
        return String(buffer.get());
    }

    bool strangersMayBeConnected()
    {
        return network::isSetupAP() && network::hasSavedWifi();
    }

    void sendBootNeeded(AsyncWebServerRequest *request)
    {
        LOG("WEB", "%s %s -> 403 (needs a BOOT press)", request->methodToString(), request->url().c_str());
        JsonDocument doc;
        doc["error"] = kBootPressMessage;
        doc["needsBoot"] = true;
        sendJson(request, 403, doc);
    }

    bool requireBootPress(AsyncWebServerRequest *request)
    {
        if (boot_button::unlocked())
        {
            return true;
        }
        sendBootNeeded(request);
        return false;
    }

    void restartSoon(const char *reason, uint32_t delayMs)
    {
        if (restartScheduled.exchange(true))
        {
            return;
        }
        restartReason = reason;
        restartDelayMs = delayMs;
        LOG("WEB", "Restarting in %lu ms (%s)", static_cast<unsigned long>(delayMs), reason);
        if (xTaskCreate(restartTask, "restart", kRestartTaskStack, nullptr, 1, nullptr) != pdPASS)
        {
            firmware::restart(reason);
        }
    }
}
