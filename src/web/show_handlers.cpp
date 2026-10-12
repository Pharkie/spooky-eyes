// The show creator's API. show_store:: keeps the saved show, and checkShow() (effects/show.cpp, called
// by show_store::save()) is the only judge of what may go in one: these handlers just turn JSON into
// blocks and back.
//
//   GET  /api/show        {"blocks":[...],"isDefault":bool,"limits":{"maxBlocks":..,"maxWordsLength":..}}
//   POST /api/show        {"blocks":[...]} -> {"ok":true,"blocks":[...tidied]}, or 400 {"error":"Block 3: ..."}
//   POST /api/show/reset  -> {"ok":true,"blocks":[...the original show]}
//
// A block is {"type":"effect","id":"creeper"} or {"type":"words","text":"Happy Halloween!"}.

#include "web/api_handlers.h"

#include "core/constants.h"
#include "core/show_store.h"
#include "effects/show.h"
#include "player/player.h"
#include "web/web_utils.h"

namespace web
{
    namespace
    {
        constexpr const char *kTypeEffect = "effect";
        constexpr const char *kTypeWords = "words";

        void addBlocks(JsonArray list, const Show &show)
        {
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
        }

        // {"ok":true,"blocks":[...]}
        void sendBlocks(AsyncWebServerRequest *request, const Show &show)
        {
            JsonDocument doc;
            doc["ok"] = true;
            addBlocks(doc["blocks"].to<JsonArray>(), show);
            sendJson(request, 200, doc);
        }

        // Turn {"blocks":[...]} into a show. Only checks the shape: checkShow() checks the rest.
        bool readBlocks(JsonDocument &body, Show &show, String &error)
        {
            JsonVariant blocks = body["blocks"];
            if (!blocks.is<JsonArray>())
            {
                error = "The show should be a list of blocks";
                return false;
            }
            size_t number = 0;
            for (JsonVariant item : blocks.as<JsonArray>())
            {
                number++;
                const String where = "Block " + String(number) + ": ";
                const char *type = item["type"] | "";
                if (strcmp(type, kTypeEffect) == 0)
                {
                    if (!item["id"].is<const char *>())
                    {
                        error = where + "which effect?";
                        return false;
                    }
                    show.push_back({ShowBlock::Kind::Effect, item["id"].as<const char *>()});
                }
                else if (strcmp(type, kTypeWords) == 0)
                {
                    if (!item["text"].is<const char *>())
                    {
                        error = where + "the words should be text";
                        return false;
                    }
                    show.push_back({ShowBlock::Kind::Words, item["text"].as<const char *>()});
                }
                else
                {
                    error = where + "should be an effect or some words";
                    return false;
                }
            }
            return true;
        }

        // GET /api/show
        void handleGetShow(AsyncWebServerRequest *request)
        {
            bool isDefault = true;
            const Show show = show_store::load(isDefault);
            JsonDocument doc;
            addBlocks(doc["blocks"].to<JsonArray>(), show);
            doc["isDefault"] = isDefault; // nothing saved: it's the original show
            JsonObject limits = doc["limits"].to<JsonObject>();
            limits["maxBlocks"] = kMaxShowBlocks;
            limits["maxWordsLength"] = kMaxWordsLength;
            sendJson(request, 200, doc);
        }

        // POST /api/show {"blocks":[...]}: save it, and play it from now on
        void handleSaveShow(AsyncWebServerRequest *request, JsonDocument &body)
        {
            Show show;
            String error;
            if (!readBlocks(body, show, error))
            {
                sendError(request, 400, error);
                return;
            }
            body.clear(); // a long show is several KB: don't keep two copies

            if (!show_store::save(show, error))
            {
                sendError(request, 400, error);
                return;
            }
            player::showChanged();
            sendBlocks(request, show); // as saved: words tidied
        }

        // POST /api/show/reset: forget the saved show, back to the original
        void handleResetShow(AsyncWebServerRequest *request, JsonDocument &)
        {
            show_store::reset();
            bool isDefault = false;
            const Show show = show_store::load(isDefault);
            if (!isDefault)
            {
                sendError(request, 500, "The pumpkin couldn't forget the saved show");
                return;
            }
            player::showChanged();
            sendBlocks(request, show);
        }
    }

    void registerShowRoutes(AsyncWebServer &server)
    {
        server.on("/api/show", HTTP_GET, handleGetShow);
        // The library also matches a route's sub-paths, so /api/show/reset goes first
        onJsonPost(server, "/api/show/reset", handleResetShow);
        onJsonPost(server, "/api/show", handleSaveShow, kMaxShowBodyBytes);
    }
}
