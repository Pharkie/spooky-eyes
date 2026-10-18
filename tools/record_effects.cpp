// Records every effect in the table (src/effects/registry.cpp) the way the tests do, one file per effect,
// for tools/effects_index.py to draw as animations. Built and run by that script; no board needed.
//
//   record_effects <outdir>
//
// Writes <outdir>/<id>.txt (the event lines test/recording_engine.h makes) and prints one tab-separated
// line per effect: id, label, emoji, group name, group title, phrase, whether the surprise show picks it.
#include <cstdio>
#include <fstream>
#include <string>

#include "effects/engine.h"
#include "effects/registry.h"
#include "../test/recording_engine.h"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: record_effects <outdir>\n");
        return 2;
    }
    const std::string dir = argv[1];
    size_t count = 0;
    const EffectInfo *effects = allEffects(count);
    for (size_t i = 0; i < count; i++)
    {
        const EffectInfo &fx = effects[i];
        RecordingEngine engine; // the settings a new pumpkin has: brightness 2, eye gap 8, "Jack's Pumpkin"
        engine.eventLimit = 6000; // the surprise show never ends
        try
        {
            playEffect(engine, fx);
        }
        catch (const EffectInterrupted &)
        {
        }
        std::ofstream out(dir + "/" + fx.id + ".txt");
        for (const std::string &event : engine.events)
            out << event << "\n";
        std::printf("%s\t%s\t%s\t%s\t%s\t%s\t%d\n", fx.id, fx.label, fx.emoji, groupName(fx.group),
                    groupTitle(fx.group), fx.phrase ? fx.phrase : "", fx.inSurprise ? 1 : 0);
    }
    return 0;
}
