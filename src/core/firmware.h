#pragma once
// Firmware safety: a firmware update is only kept once it has run for kFirmwareTrialMs without crashing,
// otherwise the bootloader rolls back to the previous one at the next restart (arduino-esp32 normally
// accepts a new image before setup() even runs). Every deliberate restart goes through restart().

namespace firmware
{
    // Call from the network task (or any periodic task): accepts the running firmware once it has been
    // up for kFirmwareTrialMs. Cheap after the first time.
    void checkTrial();

    // Accept the running firmware now, log why, and restart
    [[noreturn]] void restart(const char *reason);
}
