#pragma once
// When to switch the WiFi off to save batteries (kWifiIdleOffMs). Portable, so the native tests check
// it: network.cpp keeps the times and does the switching.

#include <cstdint>

namespace wifi_saver
{
    // True if it's time: the setting is on, nothing that needs the radio is under way (a WiFi check or
    // a scan for the setup page), and nobody has used the pumpkin for limitMs. The times are millis()
    // readings, so they wrap round after 49 days. Someone noted on another task just after nowMs was
    // read counts as just now, not as 49 days ago.
    constexpr bool timeToSwitchOff(bool saverOn, bool busy, uint32_t nowMs, uint32_t lastUsedMs, uint32_t limitMs)
    {
        return saverOn && !busy && static_cast<int32_t>(nowMs - lastUsedMs) >= static_cast<int32_t>(limitMs);
    }
}
