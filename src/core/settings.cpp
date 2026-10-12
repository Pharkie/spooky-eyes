// User settings in NVS (Preferences namespace "pumpkin").
// Loading with defaults for missing or out-of-range values, and saving only what changed (to spare
// the flash), are adapted from Scribe Evolution's src/core/config_loader.cpp. The text rules (curly
// quotes, web address names, WiFi passwords) live in text.cpp.
//
// Two mutexes: stateMutex guards the in-memory copy and is only held while copying it, so the effects
// task is never kept waiting for a flash write; saveMutex makes NVS writes take turns.

#include "core/settings.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

#include "core/constants.h"
#include "core/logging.h"
#include "core/text.h"

namespace
{
    constexpr const char *kNamespace = "pumpkin";

    // NVS keys (15 characters at most)
    constexpr const char *kKeyName = "name";
    constexpr const char *kKeySsid = "wifi_ssid";
    constexpr const char *kKeyPassword = "wifi_pass";
    constexpr const char *kKeyBrightness = "brightness";
    constexpr const char *kKeyEyeGap = "eye_gap";
    constexpr const char *kKeyMouthLed = "mouth_led";

    Settings defaults()
    {
        Settings s;
        s.pumpkinName = kDefaultPumpkinName;
        s.wifiSsid = "";
        s.wifiPassword = "";
        s.brightness = kDefaultBrightness;
        s.eyeGap = kDefaultEyeGap;
        s.mouthLed = kDefaultMouthLed;
        return s;
    }

    Settings current = defaults();          // guarded by stateMutex
    SemaphoreHandle_t stateMutex = nullptr; // created in begin()
    SemaphoreHandle_t saveMutex = nullptr;

    // Holds a mutex until the end of the scope. Does nothing before begin() (only one task then).
    class Lock
    {
    public:
        explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex)
        {
            if (mutex_ != nullptr)
                xSemaphoreTake(mutex_, portMAX_DELAY);
        }
        ~Lock()
        {
            if (mutex_ != nullptr)
                xSemaphoreGive(mutex_);
        }
        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;

    private:
        SemaphoreHandle_t mutex_;
    };

    // --- Checking ---

    // text::plain() as a String. Plain text is never longer than what it came from.
    String plainText(const String &in)
    {
        std::vector<char> out(in.length() + 1);
        text::plain(in.c_str(), out.data(), out.size());
        return String(out.data());
    }

    // True if text::plain() would drop any character (emoji, accented letters...). Each character is
    // checked on its own between two plain ones, so trimming can't hide one.
    bool hasUnshowable(const String &in)
    {
        const size_t n = in.length();
        size_t start = 0;
        while (start < n)
        {
            size_t end = start + 1;
            while (end < n && (static_cast<uint8_t>(in[end]) & 0xC0) == 0x80)
                end++; // the rest of this UTF-8 character
            if (plainText("|" + in.substring(start, end) + "|").length() <= 2)
                return true;
            start = end;
        }
        return false;
    }

    bool hasLetterOrDigit(const String &text)
    {
        for (size_t i = 0; i < text.length(); i++)
        {
            const char c = text[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
                return true;
        }
        return false;
    }

    // Tidies the name (phones' curly quotes made plain, trimmed) and checks it. Emoji and accents are
    // turned away rather than quietly dropped: the name becomes the setup WiFi's name, so it should be
    // what the family typed. It needs a letter or digit, or the web address would just be the
    // fallback "pumpkin.local", which doesn't match the name.
    bool checkName(String &name, String &error)
    {
        if (hasUnshowable(name))
        {
            error = "Please use letters, numbers and punctuation in the name (the eyes can't show emoji)";
            return false;
        }
        const String tidy = plainText(name);
        if (tidy.isEmpty())
        {
            error = "Please give the pumpkin a name";
            return false;
        }
        if (tidy.length() > kMaxNameLength)
        {
            error = "That name is too long (up to " + String(kMaxNameLength) + " characters)";
            return false;
        }
        if (!hasLetterOrDigit(tidy))
        {
            error = "The name needs at least one letter or number";
            return false;
        }
        name = tidy;
        return true;
    }

    // The WiFi name and password are kept exactly as typed: spaces, curly quotes and emoji can all be
    // part of either. No WiFi name means "not set up", so any password goes too.
    bool checkWifi(const String &ssid, String &password, String &error)
    {
        if (ssid.length() > kMaxSsidBytes)
        {
            error = "The WiFi name is too long (" + String(kMaxSsidBytes) + " characters at most)";
            return false;
        }
        if (ssid.isEmpty())
        {
            password = "";
            return true;
        }
        if (!text::validWifiPassword(password.c_str()))
        {
            if (password.length() < kMinWifiPasswordLength)
                error = "WiFi passwords have at least " + String(kMinWifiPasswordLength) +
                        " characters (leave it empty if the WiFi has no password)";
            else
                error = "That WiFi password is too long (" + String(kMaxWifiPasswordLength - 1) +
                        " characters at most)";
            return false;
        }
        return true;
    }

    bool validate(Settings &s, String &error)
    {
        if (!checkName(s.pumpkinName, error) || !checkWifi(s.wifiSsid, s.wifiPassword, error))
            return false;
        if (s.brightness > kMaxBrightness)
        {
            error = "Brightness must be 0 to " + String(kMaxBrightness);
            return false;
        }
        if (s.eyeGap > kMaxEyeGap)
        {
            error = "The eye gap must be 0 to " + String(kMaxEyeGap);
            return false;
        }
        return true;
    }

    // --- NVS ---

    String readString(Preferences &prefs, const char *key, const String &fallback)
    {
        // getString() logs an error for a missing key, so check first
        return prefs.isKey(key) ? prefs.getString(key, fallback) : fallback;
    }

    // Anything missing or out of range (say, saved by a firmware with different limits) gets its default
    Settings loadFromNvs()
    {
        Settings s = defaults();
        Preferences prefs;
        // Read-write so the namespace is created on first boot (read-only would fail and log an error)
        if (!prefs.begin(kNamespace, false))
        {
            LOG("SETTINGS", "Couldn't open NVS: using defaults");
            return s;
        }
        s.pumpkinName = readString(prefs, kKeyName, s.pumpkinName);
        s.wifiSsid = readString(prefs, kKeySsid, "");
        s.wifiPassword = readString(prefs, kKeyPassword, "");
        s.brightness = prefs.getUChar(kKeyBrightness, s.brightness);
        s.eyeGap = prefs.getUChar(kKeyEyeGap, s.eyeGap);
        s.mouthLed = prefs.getBool(kKeyMouthLed, s.mouthLed);
        prefs.end();

        const Settings fallback = defaults();
        String error;
        if (!checkName(s.pumpkinName, error))
        {
            LOG("SETTINGS", "Saved name ignored (%s)", error.c_str());
            s.pumpkinName = fallback.pumpkinName;
        }
        if (!checkWifi(s.wifiSsid, s.wifiPassword, error))
        {
            LOG("SETTINGS", "Saved WiFi details ignored (%s)", error.c_str());
            s.wifiSsid = "";
            s.wifiPassword = "";
        }
        if (s.brightness > kMaxBrightness)
            s.brightness = fallback.brightness;
        if (s.eyeGap > kMaxEyeGap)
            s.eyeGap = fallback.eyeGap;
        return s;
    }

    // Each put...() writes only if the value changed and returns false if the write failed

    bool putString(Preferences &prefs, const char *key, const String &value)
    {
        if (value.isEmpty())
        {
            // putString() reports 0 bytes written for "", success or not, so remove the key instead.
            // Loading treats a missing key as empty.
            return !prefs.isKey(key) || prefs.remove(key);
        }
        if (prefs.isKey(key) && prefs.getString(key) == value)
            return true;
        return prefs.putString(key, value) == value.length();
    }

    bool putUChar(Preferences &prefs, const char *key, uint8_t value)
    {
        if (prefs.isKey(key) && prefs.getUChar(key) == value)
            return true;
        return prefs.putUChar(key, value) == sizeof(value);
    }

    bool putBool(Preferences &prefs, const char *key, bool value)
    {
        if (prefs.isKey(key) && prefs.getBool(key) == value)
            return true;
        return prefs.putBool(key, value) == sizeof(uint8_t);
    }

    bool saveToNvs(const Settings &s)
    {
        Preferences prefs;
        if (!prefs.begin(kNamespace, false))
            return false;
        bool ok = true;
        ok &= putString(prefs, kKeyName, s.pumpkinName);
        ok &= putString(prefs, kKeySsid, s.wifiSsid);
        ok &= putString(prefs, kKeyPassword, s.wifiPassword);
        ok &= putUChar(prefs, kKeyBrightness, s.brightness);
        ok &= putUChar(prefs, kKeyEyeGap, s.eyeGap);
        ok &= putBool(prefs, kKeyMouthLed, s.mouthLed);
        prefs.end();
        return ok;
    }
}

namespace settings
{
    void begin()
    {
        if (stateMutex == nullptr)
            stateMutex = xSemaphoreCreateMutex();
        if (saveMutex == nullptr)
            saveMutex = xSemaphoreCreateMutex();

        const Settings loaded = loadFromNvs();
        {
            Lock lock(stateMutex);
            current = loaded;
        }
        LOG("SETTINGS", "\"%s\", home WiFi %s%s%s, brightness %u, eye gap %u, mouth LED %s",
            loaded.pumpkinName.c_str(),
            loaded.wifiSsid.isEmpty() ? "not set up" : "\"",
            loaded.wifiSsid.c_str(),
            loaded.wifiSsid.isEmpty() ? "" : "\"",
            loaded.brightness, loaded.eyeGap, loaded.mouthLed ? "on" : "off");
    }

    Settings get()
    {
        Lock lock(stateMutex);
        return current;
    }

    bool save(const Settings &updated, String &error)
    {
        Settings tidy = updated;
        if (!validate(tidy, error))
        {
            LOG("SETTINGS", "Not saved: %s", error.c_str());
            return false;
        }

        Lock saving(saveMutex);
        if (!saveToNvs(tidy))
        {
            // Some values may have been written: reload so memory matches the flash
            const Settings reloaded = loadFromNvs();
            {
                Lock lock(stateMutex);
                current = reloaded;
            }
            error = "Couldn't save the settings (flash storage problem)";
            LOG("SETTINGS", "%s", error.c_str());
            return false;
        }
        {
            Lock lock(stateMutex);
            current = tidy;
        }
        LOG("SETTINGS", "Saved");
        return true;
    }

    void forgetWifi()
    {
        Lock saving(saveMutex);
        Preferences prefs;
        if (prefs.begin(kNamespace, false))
        {
            if (prefs.isKey(kKeySsid))
                prefs.remove(kKeySsid);
            if (prefs.isKey(kKeyPassword))
                prefs.remove(kKeyPassword);
            prefs.end();
        }
        {
            Lock lock(stateMutex);
            current.wifiSsid = "";
            current.wifiPassword = "";
        }
        LOG("SETTINGS", "Home WiFi forgotten");
    }

    String hostnameFor(const String &pumpkinName)
    {
        char host[kMaxHostnameLength + 1];
        text::hostname(pumpkinName.c_str(), host, sizeof(host));
        return String(host);
    }
}
