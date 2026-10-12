#pragma once
// The SuperMini's BOOT button (kBootButtonPin). Press it briefly and the eyes scroll the web address again
// (main.cpp's handler). Hold it for kForgetWifiHoldMs (5 s) to forget the home WiFi: the status LED
// flickers and the pumpkin restarts in setup mode. Polled by the network task.

namespace boot_button
{
    // Set the pin up. If the button is already held, it's ignored until let go. Call once at boot.
    void begin();

    // Debounce and time the hold: call every 10-20 ms
    void update();

    // Called (from the network task) when the button is pressed and let go before the hold time
    using ShortPressHandler = void (*)();
    void onShortPress(ShortPressHandler handler);

    // True if there was a short press in the last kBootUnlockMs (safe to call from any task). The web
    // server asks this before risky actions, so only someone holding the pumpkin can do them.
    bool unlocked();
}
