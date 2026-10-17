// The saved show, in LittleFS on the 116 KB "spiffs" partition (partitions.csv; see show_store.h).
// The file holds the same blocks as the web API:
//   {"version":1,"blocks":[{"type":"effect","id":"creeper"},{"type":"words","text":"Happy Halloween!"}]}
//
// Saving writes a temporary file, then renames it over the old one. LittleFS renames are atomic, so a
// power cut part-way through leaves either the old show or the new one, never half of each. The flash
// is only written when someone presses Save (or Reset) on the show page, and LittleFS spreads its
// writes over the partition, so wear is no worry.
//
// One mutex makes the web server task's save() and reset() and the loop task's load() take turns.

#include "core/show_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sys/stat.h>
#include <string>

#include "core/constants.h"
#include "core/logging.h"

namespace show_store
{
    namespace
    {
        constexpr const char *kPartitionLabel = "spiffs"; // the name in partitions.csv
        constexpr const char *kMountPoint = "/littlefs";
        constexpr uint8_t kMaxOpenFiles = 5;
        constexpr const char *kShowPath = "/show.json";
        constexpr const char *kTempPath = "/show.tmp";
        constexpr int kFileVersion = 1;

        // The biggest real show is about 26 KB (kMaxShowBlocks blocks of kMaxWordsLength quote marks,
        // each escaped). A bigger file isn't ours, so it isn't read into memory.
        constexpr size_t kMaxFileBytes = 32 * 1024;

        constexpr const char *kTypeEffect = "effect";
        constexpr const char *kTypeWords = "words";

        constexpr const char *kStorageBroken = "The pumpkin couldn't save the show (its storage isn't working)";

        bool mounted = false;              // set once in begin()
        SemaphoreHandle_t mutex = nullptr; // created in begin()

        // Holds the mutex until the end of the scope. Does nothing before begin() (only one task then).
        class Lock
        {
        public:
            Lock()
            {
                if (mutex != nullptr)
                    xSemaphoreTake(mutex, portMAX_DELAY);
            }
            ~Lock()
            {
                if (mutex != nullptr)
                    xSemaphoreGive(mutex);
            }
            Lock(const Lock &) = delete;
            Lock &operator=(const Lock &) = delete;
        };

        // The file's size, or -1 if it isn't there. Uses stat() because LittleFS.exists() logs an error
        // for a missing file, and no saved show is perfectly normal.
        long fileSize(const char *path)
        {
            struct stat info;
            if (stat((String(kMountPoint) + path).c_str(), &info) != 0)
                return -1;
            return static_cast<long>(info.st_size);
        }

        // Read the saved show into `show`. False if there isn't one, or it can't be used (logged).
        // Blocks the firmware no longer knows (an effect removed by an update, say) are left out.
        bool readSavedShow(Show &show)
        {
            const long size = fileSize(kShowPath);
            if (size < 0)
            {
                return false; // nothing saved (quietly: the show page asks every time it opens)
            }
            if (size == 0 || static_cast<size_t>(size) > kMaxFileBytes)
            {
                LOG("SHOW", "The saved show is the wrong size (%ld bytes): playing the original show", size);
                return false;
            }

            // Parsed straight from the file: no buffer the size of the show (up to 32 KB in one piece, which a
            // fragmented heap may not have after hours on WiFi)
            File file = LittleFS.open(kShowPath, "r");
            if (!file)
            {
                LOG("SHOW", "Couldn't read the saved show: playing the original show");
                return false;
            }
            JsonDocument doc;
            const DeserializationError parseError = deserializeJson(doc, file);
            file.close();
            if (parseError || !doc["blocks"].is<JsonArray>())
            {
                LOG("SHOW", "The saved show is damaged (%s): playing the original show",
                    parseError ? parseError.c_str() : "no blocks");
                return false;
            }

            size_t number = 0;
            for (JsonVariant item : doc["blocks"].as<JsonArray>())
            {
                number++;
                if (show.size() >= kMaxShowBlocks)
                {
                    LOG("SHOW", "The saved show is too long: only its first %u blocks will play",
                        static_cast<unsigned>(kMaxShowBlocks));
                    break;
                }
                const char *type = item["type"] | "";
                ShowBlock block;
                if (strcmp(type, kTypeEffect) == 0 && item["id"].is<const char *>())
                {
                    block = {ShowBlock::Kind::Effect, item["id"].as<const char *>()};
                }
                else if (strcmp(type, kTypeWords) == 0 && item["text"].is<const char *>())
                {
                    block = {ShowBlock::Kind::Words, item["text"].as<const char *>()};
                }
                else
                {
                    LOG("SHOW", "Saved show: left out block %u (not an effect or words)", static_cast<unsigned>(number));
                    continue;
                }
                std::string problem;
                if (!checkBlock(block, problem))
                {
                    LOG("SHOW", "Saved show: left out block %u (%s)", static_cast<unsigned>(number), problem.c_str());
                    continue;
                }
                show.push_back(std::move(block));
            }
            if (show.empty())
            {
                LOG("SHOW", "The saved show has nothing in it the pumpkin can play: playing the original show");
                return false;
            }
            return true;
        }

        // Write the show to the temporary file, then rename it over the saved show
        bool writeShow(const JsonDocument &doc, String &error)
        {
            const size_t expected = measureJson(doc);
            File file = LittleFS.open(kTempPath, "w");
            if (!file)
            {
                LOG("SHOW", "Couldn't create %s", kTempPath);
                error = kStorageBroken;
                return false;
            }
            serializeJson(doc, file);
            file.close();

            // Check what reached the flash: a full partition only shows up when the file is closed
            if (fileSize(kTempPath) != static_cast<long>(expected))
            {
                LOG("SHOW", "Couldn't write the show (%u bytes): storage full?", static_cast<unsigned>(expected));
                LittleFS.remove(kTempPath);
                error = kStorageBroken;
                return false;
            }
            if (!LittleFS.rename(kTempPath, kShowPath))
            {
                LOG("SHOW", "Couldn't rename %s to %s", kTempPath, kShowPath);
                LittleFS.remove(kTempPath);
                error = kStorageBroken;
                return false;
            }
            return true;
        }
    }

    void begin()
    {
        mutex = xSemaphoreCreateMutex();
        if (mutex == nullptr)
        {
            LOG("SHOW", "Couldn't create the show mutex");
        }

        // A board that's never had a filesystem here (a new one is erased before flashing) won't mount,
        // so LittleFS formats the partition and tries again. That takes a second or two, once.
        const uint32_t started = millis();
        mounted = LittleFS.begin(false, kMountPoint, kMaxOpenFiles, kPartitionLabel);
        if (!mounted)
        {
            mounted = LittleFS.begin(true, kMountPoint, kMaxOpenFiles, kPartitionLabel);
            // LittleFS.format() leaves the idle task under the task watchdog, unlike every other boot:
            // put that back, so a first boot and the rest behave the same
            disableCore0WDT();
        }
        const uint32_t tookMs = millis() - started;
        if (!mounted)
        {
            LOG("SHOW", "Couldn't mount or format the \"%s\" partition: the original show will play, and "
                        "saving shows won't work",
                kPartitionLabel);
            return;
        }
        LOG("SHOW", "Storage ready: %u of %u bytes used, %s (took %lu ms)", static_cast<unsigned>(LittleFS.usedBytes()),
            static_cast<unsigned>(LittleFS.totalBytes()), fileSize(kShowPath) >= 0 ? "a show is saved" : "no saved show",
            static_cast<unsigned long>(tookMs));

        // A save cut short by a power cut leaves its temporary file behind (the saved show is untouched)
        if (fileSize(kTempPath) >= 0)
        {
            LittleFS.remove(kTempPath);
        }
    }

    Show load(bool &isDefault)
    {
        Show show;
        {
            Lock lock;
            if (mounted && readSavedShow(show))
            {
                isDefault = false;
                return show;
            }
        }
        isDefault = true;
        return defaultShow();
    }

    bool save(Show &show, String &error)
    {
        std::string problem;
        if (!checkShow(show, problem))
        {
            error = problem.c_str();
            return false;
        }

        // Build the file before taking the lock, so load() isn't kept waiting
        JsonDocument doc;
        doc["version"] = kFileVersion;
        JsonArray list = doc["blocks"].to<JsonArray>();
        for (const ShowBlock &block : show)
        {
            JsonObject item = list.add<JsonObject>();
            if (block.kind == ShowBlock::Kind::Effect)
            {
                item["type"] = kTypeEffect;
                item["id"] = block.value;
            }
            else
            {
                item["type"] = kTypeWords;
                item["text"] = block.value;
            }
        }
        if (doc.overflowed())
        {
            LOG("SHOW", "Not enough memory to save the show");
            error = "The pumpkin ran out of memory saving the show. Try a shorter show.";
            return false;
        }

        Lock lock;
        if (!mounted)
        {
            LOG("SHOW", "Can't save the show: storage isn't mounted");
            error = kStorageBroken;
            return false;
        }
        if (!writeShow(doc, error))
        {
            return false;
        }
        LOG("SHOW", "Saved a show of %u blocks (%u bytes)", static_cast<unsigned>(show.size()),
            static_cast<unsigned>(measureJson(doc)));
        return true;
    }

    void reset()
    {
        Lock lock;
        if (!mounted || fileSize(kShowPath) < 0)
        {
            return; // nothing saved: it's the original show already
        }
        if (LittleFS.remove(kShowPath))
        {
            LOG("SHOW", "Forgot the saved show: back to the original show");
        }
        else
        {
            LOG("SHOW", "Couldn't delete the saved show");
        }
    }

    bool storageInfo(StorageInfo &info)
    {
        info = {0, 0, false, -1};
        Lock lock;
        if (!mounted)
        {
            return false;
        }
        info.usedBytes = LittleFS.usedBytes();
        info.totalBytes = LittleFS.totalBytes();
        info.showBytes = fileSize(kShowPath);
        info.showSaved = info.showBytes >= 0;
        return true;
    }
}
