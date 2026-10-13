#pragma once
// Text from phones and web forms, made safe for the eyes' font, and WiFi password rules. Portable (no
// Arduino), so it's unit-tested on the computer. The one place these rules live: settings, the player
// and the web server all use it.

#include <cstddef>

namespace text
{
    // Copy `in` to `out` as plain printable ASCII: curly quotes and apostrophes become ' and ", dashes
    // become -, … becomes ..., accented letters lose their accents (é -> e, ß -> ss), odd spaces and
    // control characters become spaces, and anything else outside ASCII (emoji) is dropped. Leading and
    // trailing spaces are trimmed. Always NUL-terminates. Returns the length written.
    size_t plain(const char *in, char *out, size_t outSize);

    // True if this is a usable WiFi password: empty (open network), 8-63 characters, or 64 hex digits
    bool validWifiPassword(const char *password);
}
