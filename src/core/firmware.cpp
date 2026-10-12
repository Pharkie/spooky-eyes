// Firmware trial and restarts (see firmware.h).
//
// How the rollback works: after an update, the bootloader starts the new firmware once on trial
// (ESP-IDF's "pending verify" state; CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is on in arduino-esp32's
// ESP32-C3 build). If it restarts for any reason before it's marked good (a crash, the watchdog, a
// brownout, even being switched off), the bootloader goes back to the previous firmware.

#include "core/firmware.h"

#include <Arduino.h>
#include <atomic>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "core/constants.h"
#include "core/logging.h"

// arduino-esp32's initArduino() marks a new firmware good before setup() even runs, unless this
// returns true. Then checkTrial() decides instead.
extern "C" bool verifyRollbackLater()
{
    return true;
}

namespace
{
    std::atomic<bool> trialChecked{false};

    // Mark the running firmware good if it's on trial. Does nothing otherwise, e.g. after flashing over
    // USB, or once it's already been accepted.
    void acceptRunningFirmware(const char *why)
    {
        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_ota_img_states_t state;
        if (running == nullptr || esp_ota_get_state_partition(running, &state) != ESP_OK ||
            state != ESP_OTA_IMG_PENDING_VERIFY)
            return;
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
            LOG("FIRMWARE", "Keeping the new firmware %s (%s)", FIRMWARE_VERSION, why);
        else
            LOG("FIRMWARE", "Couldn't mark the new firmware as good: it may be rolled back at the next restart");
    }
}

namespace firmware
{
    void checkTrial()
    {
        if (trialChecked.load(std::memory_order_relaxed) || millis() < kFirmwareTrialMs)
            return;
        trialChecked = true;
        acceptRunningFirmware("it ran without crashing");
    }

    void restart(const char *reason)
    {
        LOG("FIRMWARE", "Restarting: %s", reason);
        // A deliberate restart (new WiFi saved, BOOT held...) isn't a crash, so don't roll back for it
        acceptRunningFirmware("restarting on purpose");
        delay(50); // let the log line out over USB
        esp_restart();
    }
}
