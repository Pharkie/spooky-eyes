#pragma once
// The saved show, kept in a small LittleFS filesystem on the flash partition labelled "spiffs"
// (/show.json), so it survives power cuts and firmware updates (OTA only rewrites the app). If there's
// no saved show, or it can't be read, the default show is used. Thread-safe: the web server task saves,
// the loop task loads.

#include <Arduino.h>
#include "effects/show.h"

namespace show_store
{
    // Mount the filesystem, formatting the partition if it's blank (a freshly erased board). Call once
    // at boot, before player::begin().
    void begin();

    // The show to play. isDefault says whether it's the original show (nothing saved).
    Show load(bool &isDefault);

    // Check (checkShow) and save. False, with a reason, if it can't be used; nothing is saved then.
    // The tidied show is written back into `show`.
    bool save(Show &show, String &error);

    // Forget the saved show: back to the default
    void reset();
}
