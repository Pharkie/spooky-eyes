// POST /api/update: firmware upload from the Settings page (multipart form, one file), written to the
// spare app slot with the Update library, then a restart into it. The first bytes are checked before
// anything is written, so choosing the wrong file gives a clear message rather than a failed update.
// Always needs a BOOT press first: anyone on the setup WiFi could otherwise replace the firmware.

#include "web/api_handlers.h"

#include <Update.h>
#include <esp_app_format.h>
#include "core/logging.h"
#include "core/network.h"
#include "hardware/boot_button.h"
#include "web/web_utils.h"

namespace web
{
    namespace
    {
        // Progress of one upload. Lives in the request's _tempObject, which the library free()s.
        struct UploadState
        {
            uint8_t head[48];  // the first bytes, held back until they've been checked
            size_t headLength;
            bool started;      // Update.begin() succeeded
            bool finished;     // Update.end() succeeded
            bool needsBoot;    // refused because BOOT wasn't pressed
            char error[160];
        };

        // App image header, first segment header, then the app description's magic word
        constexpr size_t kHeadCheckBytes =
            sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(uint32_t);
        static_assert(kHeadCheckBytes <= sizeof(UploadState::head), "head buffer too small");

        constexpr size_t kMultipartSlackBytes = 4096; // form boundaries and headers around the file

        // Every handler runs on the async_tcp task, so these need no lock.
        // Only one update at a time:
        AsyncWebServerRequest *uploadOwner = nullptr;
        // Set once a new firmware is written, until the restart. A second upload (a double tap) would
        // start by erasing the slot that was just written.
        bool updateFinished = false;

        // nullptr if this looks like an app for the pumpkin's chip, otherwise what's wrong with it
        const char *checkFirmwareHead(const uint8_t *data, size_t length)
        {
            if (length < kHeadCheckBytes)
            {
                return "That file is too small to be pumpkin firmware.";
            }
            esp_image_header_t header;
            memcpy(&header, data, sizeof(header));
            if (header.magic != ESP_IMAGE_HEADER_MAGIC)
            {
                return "That isn't a firmware file. Choose the file ending -ota.bin.";
            }
            if (header.chip_id != ESP_CHIP_ID_ESP32C3)
            {
                return "That firmware is for a different kind of board, not the pumpkin's ESP32-C3.";
            }
            uint32_t appMagic = 0;
            memcpy(&appMagic, data + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(appMagic));
            if (appMagic != ESP_APP_DESC_MAGIC_WORD)
            {
                // A merged image starts with the bootloader, which has no app description
                return "That's the full image for new boards (-full.bin). Choose the file ending -ota.bin instead.";
            }
            return nullptr;
        }

        void fail(UploadState *state, const char *message)
        {
            if (state->error[0] == '\0')
            {
                strlcpy(state->error, message, sizeof(state->error));
                LOG("WEB", "Firmware update failed: %s", message);
            }
            if (state->started && !state->finished)
            {
                Update.abort();
                state->started = false;
            }
        }

        void failWithUpdateError(UploadState *state, const char *what)
        {
            char message[sizeof(state->error)];
            snprintf(message, sizeof(message), "%s (%s). Try again.", what, Update.errorString());
            fail(state, message);
        }

        // A new upload is arriving: check it's allowed before accepting any data
        UploadState *beginUpload(AsyncWebServerRequest *request, const String &filename)
        {
            UploadState *state = static_cast<UploadState *>(calloc(1, sizeof(UploadState)));
            if (state == nullptr)
            {
                return nullptr; // reported when the request completes
            }
            request->_tempObject = state;
            if (!hasCsrfHeader(request))
            {
                fail(state, "Missing X-Pumpkin header");
                return state;
            }
            if (updateFinished)
            {
                fail(state, "The pumpkin already has the new firmware and is restarting. Wait for it to come back.");
                return state;
            }
            if (!boot_button::unlocked())
            {
                state->needsBoot = true;
                fail(state, kBootPressMessage);
                return state;
            }
            if (uploadOwner != nullptr)
            {
                fail(state, "Another update is already running. Wait for it to finish.");
                return state;
            }
            if (request->contentLength() > ESP.getFreeSketchSpace() + kMultipartSlackBytes)
            {
                fail(state, "That file is too big for the pumpkin. Choose the file ending -ota.bin.");
                return state;
            }
            uploadOwner = request;
            request->onDisconnect(
                [request]()
                {
                    // Phone went away mid-upload: throw the half-written firmware away
                    if (uploadOwner == request)
                    {
                        if (Update.isRunning())
                        {
                            Update.abort();
                        }
                        uploadOwner = nullptr;
                    }
                });
            LOG("WEB", "Firmware upload started: %s (%u bytes)", filename.c_str(),
                static_cast<unsigned>(request->contentLength()));
            return state;
        }

        void handleUpload(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
                          size_t len, bool final)
        {
            // An upload is one long request: keep the WiFi on while it arrives, however slowly
            network::noteActivity();
            UploadState *state = static_cast<UploadState *>(request->_tempObject);
            if (index == 0)
            {
                if (state == nullptr)
                {
                    state = beginUpload(request, filename);
                }
                else if (state->headLength > 0)
                {
                    fail(state, "Please choose just one file.");
                }
            }
            if (state == nullptr || state->error[0] != '\0')
            {
                return;
            }

            // Hold back the first bytes until there are enough to check (pieces can be tiny)
            size_t used = 0;
            if (!state->started)
            {
                while (state->headLength < kHeadCheckBytes && used < len)
                {
                    state->head[state->headLength++] = data[used++];
                }
                if (state->headLength < kHeadCheckBytes && !final)
                {
                    return;
                }
                const char *problem = checkFirmwareHead(state->head, state->headLength);
                if (problem != nullptr)
                {
                    fail(state, problem);
                    return;
                }
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
                {
                    failWithUpdateError(state, "Couldn't start the update");
                    return;
                }
                state->started = true;
                if (Update.write(state->head, state->headLength) != state->headLength)
                {
                    failWithUpdateError(state, "Couldn't write the update");
                    return;
                }
            }

            if (len > used && Update.write(data + used, len - used) != len - used)
            {
                failWithUpdateError(state, "Couldn't write the update");
                return;
            }

            if (final)
            {
                if (Update.end(true))
                {
                    state->finished = true;
                    updateFinished = true;
                    LOG("WEB", "Firmware update written (%u bytes)", static_cast<unsigned>(index + len));
                    // Restart even if the phone goes away before the reply
                    restartSoon("Firmware updated from the Settings page");
                }
                else
                {
                    failWithUpdateError(state, "The new firmware didn't check out");
                }
            }
        }

        // The whole form has arrived
        void handleUpdateDone(AsyncWebServerRequest *request)
        {
            UploadState *state = static_cast<UploadState *>(request->_tempObject);
            const bool owner = uploadOwner == request;
            if (!hasCsrfHeader(request))
            {
                sendError(request, 403, "Missing X-Pumpkin header");
                return;
            }
            if ((state != nullptr && state->needsBoot) || (state == nullptr && !boot_button::unlocked()))
            {
                sendBootNeeded(request);
                return;
            }
            if (state == nullptr)
            {
                sendError(request, 400, "No firmware file arrived. Choose the file ending -ota.bin and try again.");
                return;
            }
            if (state->error[0] == '\0' && !state->finished)
            {
                fail(state, "The upload didn't finish. Try again.");
            }
            if (state->error[0] != '\0')
            {
                if (owner)
                {
                    uploadOwner = nullptr;
                }
                sendError(request, 400, state->error);
                return;
            }
            if (owner)
            {
                uploadOwner = nullptr;
            }
            sendOk(request, "Updated! The pumpkin is restarting with the new firmware.");
        }
    }

    void registerUpdateRoute(AsyncWebServer &server)
    {
        server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpload);
    }
}
