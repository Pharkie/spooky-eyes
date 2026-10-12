#pragma once
// Web pages and JSON API, on ESPAsyncWebServer (as in Scribe Evolution).
//   /          the button page (a pumpkin with no home WiFi saved sends phones to /setup instead;
//              "/?play=1" shows the buttons anyway)
//   /setup     choose the home WiFi
//   /settings  name, brightness, mouth LED, WiFi, firmware update
// On the setup WiFi, a phone's "sign in to network" sheet opens /setup, or / when the pumpkin has just
// lost its home WiFi (out of range, e.g. on the doorstep), so people can play straight away.
// The pages are compiled into the firmware from web/ by scripts/embed_web.py.

namespace web
{
    // Start the server. Call after network::begin() and player::begin().
    void begin();
}
