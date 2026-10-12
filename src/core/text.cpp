// Text rules (see text.h). Portable: no Arduino, so the native tests build it too.
//
// Input is UTF-8 from phones, read a byte at a time and never past the terminating NUL. Broken or
// overlong sequences are dropped like any other character the eyes can't show, so a crafted sequence
// can never sneak a plain character (say an overlong "<") through.

#include "core/text.h"

#include <cstdint>
#include <cstring>

#include "core/constants.h"

namespace
{
    constexpr uint32_t kBroken = 0xFFFFFFFF; // not a valid UTF-8 character
    constexpr const char *kFallbackHostname = "pumpkin";

    // Read one UTF-8 character from p. Sets `used` to the bytes it took: just 1 for a byte that doesn't
    // start a valid sequence (the bytes after it are then read on their own).
    uint32_t nextCharacter(const unsigned char *p, size_t &used)
    {
        const unsigned char lead = p[0];
        used = 1;
        if (lead < 0x80)
            return lead;

        size_t extra;
        uint32_t code;
        uint32_t smallest; // anything below this was written with too many bytes (overlong)
        if ((lead & 0xE0) == 0xC0)
        {
            extra = 1;
            code = lead & 0x1F;
            smallest = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            extra = 2;
            code = lead & 0x0F;
            smallest = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            extra = 3;
            code = lead & 0x07;
            smallest = 0x10000;
        }
        else
        {
            return kBroken; // a stray continuation byte, or not UTF-8 at all
        }

        for (size_t i = 1; i <= extra; i++)
        {
            // The NUL at the end isn't a continuation byte, so this never reads past it
            const unsigned char next = p[i];
            if ((next & 0xC0) != 0x80)
                return kBroken;
            code = (code << 6) | (next & 0x3F);
        }
        used = 1 + extra;
        if (code < smallest || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
            return kBroken;
        return code;
    }

    // Accented Latin letters (U+00C0 to U+017F) folded to plain ones, so "Zoë" becomes "Zoe" rather than
    // "Zo". Generated with Python's unicodedata (NFD base letter), plus the letters that have no base.
    constexpr uint32_t kFoldFirst = 0xC0;
    constexpr uint32_t kFoldLast = 0x17F;
    const char kLatinFold[kFoldLast - kFoldFirst + 1][3] = {
        "A", "A", "A", "A", "A", "A", "AE", "C", // À Á Â Ã Ä Å Æ Ç
        "E", "E", "E", "E", "I", "I", "I", "I", // È É Ê Ë Ì Í Î Ï
        "D", "N", "O", "O", "O", "O", "O", "", // Ð Ñ Ò Ó Ô Õ Ö ×
        "O", "U", "U", "U", "U", "Y", "Th", "ss", // Ø Ù Ú Û Ü Ý Þ ß
        "a", "a", "a", "a", "a", "a", "ae", "c", // à á â ã ä å æ ç
        "e", "e", "e", "e", "i", "i", "i", "i", // è é ê ë ì í î ï
        "d", "n", "o", "o", "o", "o", "o", "", // ð ñ ò ó ô õ ö ÷
        "o", "u", "u", "u", "u", "y", "th", "y", // ø ù ú û ü ý þ ÿ
        "A", "a", "A", "a", "A", "a", "C", "c", // Ā ā Ă ă Ą ą Ć ć
        "C", "c", "C", "c", "C", "c", "D", "d", // Ĉ ĉ Ċ ċ Č č Ď ď
        "D", "d", "E", "e", "E", "e", "E", "e", // Đ đ Ē ē Ĕ ĕ Ė ė
        "E", "e", "E", "e", "G", "g", "G", "g", // Ę ę Ě ě Ĝ ĝ Ğ ğ
        "G", "g", "G", "g", "H", "h", "H", "h", // Ġ ġ Ģ ģ Ĥ ĥ Ħ ħ
        "I", "i", "I", "i", "I", "i", "I", "i", // Ĩ ĩ Ī ī Ĭ ĭ Į į
        "I", "i", "IJ", "ij", "J", "j", "K", "k", // İ ı Ĳ ĳ Ĵ ĵ Ķ ķ
        "k", "L", "l", "L", "l", "L", "l", "L", // ĸ Ĺ ĺ Ļ ļ Ľ ľ Ŀ
        "l", "L", "l", "N", "n", "N", "n", "N", // ŀ Ł ł Ń ń Ņ ņ Ň
        "n", "n", "N", "n", "O", "o", "O", "o", // ň ŉ Ŋ ŋ Ō ō Ŏ ŏ
        "O", "o", "OE", "oe", "R", "r", "R", "r", // Ő ő Œ œ Ŕ ŕ Ŗ ŗ
        "R", "r", "S", "s", "S", "s", "S", "s", // Ř ř Ś ś Ŝ ŝ Ş ş
        "S", "s", "T", "t", "T", "t", "T", "t", // Š š Ţ ţ Ť ť Ŧ ŧ
        "U", "u", "U", "u", "U", "u", "U", "u", // Ũ ũ Ū ū Ŭ ŭ Ů ů
        "U", "u", "U", "u", "W", "w", "Y", "y", // Ű ű Ų ų Ŵ ŵ Ŷ ŷ
        "Y", "Z", "z", "Z", "z", "Z", "z", "s", // Ÿ Ź ź Ż ż Ž ž ſ
    };

    // Write the plain version of one character into `out` (room for 3). Returns how many characters
    // that is: 0 = drop it. Never more than the character's own UTF-8 length, so plain text is never
    // longer than what it came from.
    size_t plainFor(uint32_t code, char *out)
    {
        if (code >= 0x20 && code < 0x7F)
        {
            out[0] = static_cast<char>(code);
            return 1;
        }
        // Control characters (C0, DEL and C1) become spaces
        if (code < 0x20 || (code >= 0x7F && code < 0xA0))
        {
            out[0] = ' ';
            return 1;
        }
        if (code >= kFoldFirst && code <= kFoldLast)
        {
            // 2 bytes in, at most 2 out
            const char *folded = kLatinFold[code - kFoldFirst];
            size_t n = 0;
            while (folded[n] != '\0')
            {
                out[n] = folded[n];
                n++;
            }
            return n;
        }
        switch (code)
        {
        // Phones "smarten" punctuation as you type (an iPhone turns ' into ’ and ... into …)
        case 0x2018: // ‘
        case 0x2019: // ’
        case 0x201A: // ‚
        case 0x201B: // ‛
        case 0x2032: // ′ prime
            out[0] = '\'';
            return 1;
        case 0x201C: // “
        case 0x201D: // ”
        case 0x201E: // „
        case 0x201F: // ‟
        case 0x2033: // ″ double prime
            out[0] = '"';
            return 1;
        case 0x2010: // hyphen
        case 0x2011: // non-breaking hyphen
        case 0x2012: // figure dash
        case 0x2013: // – en dash
        case 0x2014: // — em dash
        case 0x2015: // horizontal bar
        case 0x2212: // − minus sign
            out[0] = '-';
            return 1;
        case 0x2026: // … (3 bytes in, 3 out)
            out[0] = out[1] = out[2] = '.';
            return 3;
        // Spaces of other widths, so dropping them doesn't join two words
        case 0x00A0: // no-break space
        case 0x2000:
        case 0x2001:
        case 0x2002:
        case 0x2003:
        case 0x2004:
        case 0x2005:
        case 0x2006:
        case 0x2007:
        case 0x2008:
        case 0x2009:
        case 0x200A:
        case 0x202F: // narrow no-break space
        case 0x205F:
        case 0x3000:
            out[0] = ' ';
            return 1;
        default:
            return 0; // emoji and the like: the eyes' font can't show them
        }
    }

    // ' itself, or anything plain() turns into one
    bool isApostrophe(uint32_t code)
    {
        char plain[3];
        return code != kBroken && plainFor(code, plain) == 1 && plain[0] == '\'';
    }

    bool isHexDigit(char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
}

namespace text
{
    size_t plain(const char *in, char *out, size_t outSize)
    {
        if (out == nullptr || outSize == 0)
            return 0;
        const size_t room = outSize - 1;
        size_t length = 0;
        const unsigned char *p = reinterpret_cast<const unsigned char *>(in != nullptr ? in : "");
        while (*p != 0 && length < room)
        {
            size_t used;
            const uint32_t code = nextCharacter(p, used);
            p += used;
            if (code == kBroken)
                continue;
            char piece[3];
            const size_t count = plainFor(code, piece);
            for (size_t i = 0; i < count && length < room; i++)
            {
                if (piece[i] == ' ' && length == 0)
                    continue; // leading space
                out[length++] = piece[i];
            }
        }
        while (length > 0 && out[length - 1] == ' ')
            length--; // trailing spaces
        out[length] = '\0';
        return length;
    }

    size_t hostname(const char *name, char *out, size_t outSize)
    {
        if (out == nullptr || outSize == 0)
            return 0;
        size_t limit = outSize - 1;
        if (limit > kMaxHostnameLength)
            limit = kMaxHostnameLength;

        size_t length = 0;
        bool gap = false; // something other than a letter or digit since the last one kept
        const unsigned char *p = reinterpret_cast<const unsigned char *>(name != nullptr ? name : "");
        while (*p != 0)
        {
            size_t used;
            const uint32_t code = nextCharacter(p, used);
            p += used;
            if (isApostrophe(code))
                continue; // "Jack's" and "Jack’s" -> "jacks"
            char piece[3];
            const size_t pieceLength = code == kBroken ? 0 : plainFor(code, piece); // "Zoë" -> "Zoe"
            if (pieceLength == 0)
                gap = true;
            bool full = false;
            for (size_t i = 0; i < pieceLength && !full; i++)
            {
                char c = piece[i];
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
                {
                    gap = true;
                    continue;
                }
                // Hyphens only go between kept characters, so there's never one at either end
                const size_t needed = (gap && length > 0) ? 2 : 1;
                if (length + needed > limit)
                {
                    full = true;
                    break;
                }
                if (needed == 2)
                    out[length++] = '-';
                out[length++] = c;
                gap = false;
            }
            if (full)
                break;
        }

        if (length == 0)
        {
            length = strlen(kFallbackHostname);
            if (length > limit)
                length = limit;
            memcpy(out, kFallbackHostname, length);
        }
        out[length] = '\0';
        return length;
    }

    bool validWifiPassword(const char *password)
    {
        if (password == nullptr)
            return true;
        const size_t length = strlen(password);
        if (length == 0)
            return true; // open network
        if (length >= kMinWifiPasswordLength && length < kMaxWifiPasswordLength)
            return true; // a WPA2 passphrase: 8-63 characters
        if (length != kMaxWifiPasswordLength)
            return false;
        for (size_t i = 0; i < length; i++)
        {
            if (!isHexDigit(password[i]))
                return false;
        }
        return true; // a raw 64-hex-digit key
    }
}
