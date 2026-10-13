// Effect tests: the C++ effects against the MicroPython originals, and every effect at the extremes.
//
// Golden tests: tools/capture_python_frames.py ran the old main.py with fake hardware and wrote down
// every frame, wait, brightness change, SAY line and mouth colour change into test/golden/<id>.txt.
// Here each effect runs on a RecordingEngine and the two recordings are compared line by line. Waits may
// differ by 1 ms (Python rounds seconds to milliseconds). The old pumpkin is frozen, so effects added
// since have no golden file; that's fine.
//
// Smoke tests: every effect, old and new, plays at the lowest and highest brightness and eye gap, and
// must finish, stay within the MAX7219's brightness range, leave brightness at maxBright() and give
// every frame a moment on the eyes. New effects (no golden file) also play at the golden settings. Every
// effect that can go in a show also plays as a show block (playBlock), phrase first.
//
// Registry tests: ids, labels, emoji and phrases are usable by the web page, the player and the font.
//
// Fruit machine tests: the odds of winning, and the reels land on the symbols they were meant to.
//
//   pio test -e native        run them (add -v to see the side-by-side context for a mismatch)
//
// Set PUMPKIN_ACTUAL_DIR=/some/folder to also save what the C++ recorded, for diffing by hand.

#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "../recording_engine.h"
#include "core/constants.h"
#include "display/font.h"
#include "display/fonts.h"
#include "display/frame.h"
#include "display/sprites.h"
#include "effects/animations.h"
#include "effects/effects.h"
#include "effects/primitives.h"
#include "effects/registry.h"
#include "effects/show.h"

void setUp()
{
}

void tearDown()
{
}

// ---- Golden files ----

// Golden recordings that aren't buttons, and the C++ that should do the same thing. The Python side
// of each is in EXTRA in tools/capture_python_frames.py. Every other id is looked up with findEffect().
using Runner = void (*)(Engine &);

struct ExtraCase
{
    const char *id;
    Runner run;
};

static const ExtraCase kExtraCases[] = {
    // One pass of the default show, block by block, as the player plays it
    {"default_show",
     [](Engine &e) {
         for (const ShowBlock &block : defaultShow())
             playBlock(e, block);
     }},
    // The old button page padded typed words with a space each side
    {"words", [](Engine &e) { scrollWords(e, "Hello Jack! $5 ok?"); }},
    {"anim_stareAndBlink", [](Engine &e) { runAnimation(e, Animations::stareAndBlink); }},
    {"anim_downLeftABit", [](Engine &e) { runAnimation(e, Animations::downLeftABit); }},
    {"anim_downRightABit", [](Engine &e) { runAnimation(e, Animations::downRightABit); }},
    {"anim_roll", [](Engine &e) { runAnimation(e, Animations::roll); }},
    {"anim_growEyes", [](Engine &e) { runAnimation(e, Animations::growEyes); }},
    {"anim_ghosts1", [](Engine &e) { runAnimation(e, Animations::ghosts1); }},
    {"anim_winkLeft", [](Engine &e) { runAnimation(e, Animations::winkLeft); }},
    {"anim_winkRight", [](Engine &e) { runAnimation(e, Animations::winkRight); }},
    {"anim_minecraft", [](Engine &e) { runAnimation(e, Animations::minecraft); }},
    {"prim_scroll_empty", [](Engine &e) { scrollMessage(e, textFont, ""); }},
    {"prim_scroll_short", [](Engine &e) { scrollMessage(e, textFont, "x;", 10); }},
    {"prim_fly_uneven", [](Engine &e) { fly(e, batFont, {" a ", " ab ", "b"}, 20, 2); }},
    {"prim_slide_back",
     [](Engine &e) { slide(e, {Shapes::fireball1, Shapes::fireball2, Shapes::star3}, 20, -4, Shapes::target, kBlank, 10); }},
    {"prim_slide_still", [](Engine &e) { slide(e, {Shapes::dirt}, 5, 5, kBlank, Shapes::heart1); }},
    {"prim_flip_odd", [](Engine &e) { flip(e, Shapes::cat1, Shapes::cat2, 3, 10); }},
    {"prim_sparkles_keep",
     [](Engine &e) { sparkles(e, 3, 1, EyeSetting::keep(Shapes::heart1), EyeSetting::sparkle(), 10); }},
    {"prim_explode_one_eye", [](Engine &e) { explode(e, EyeSetting::sparkle(), EyeSetting::blank()); }},
};

static const ExtraCase *findExtraCase(const std::string &id)
{
    for (const ExtraCase &c : kExtraCases)
        if (id == c.id)
            return &c;
    return nullptr;
}

// An id that is only in the golden files: anim_..., prim_..., default_show and words
static bool looksExtra(const std::string &id)
{
    return id == "default_show" || id == "words" || id.rfind("anim_", 0) == 0 || id.rfind("prim_", 0) == 0;
}

static std::string gGoldenDir;

struct GoldenCase
{
    std::string id;
    std::string testName;
};

static std::vector<GoldenCase> gCases;
static size_t gCurrent = 0;

// Ids of the golden recordings of buttons: the effects the old pumpkin had
static std::set<std::string> gOldIds;

// The surprise show carries on until it's interrupted
static bool isSurpriseShow(Runner run)
{
    return run == surpriseShow;
}

// The old surprise show only knew the old effects. Effects added since would change which effect each
// random number picks, so the golden test replays the show with just the old ones.
static bool isOldEffect(const EffectInfo &effect)
{
    return gOldIds.count(effect.id) > 0;
}

static void oldSurpriseShow(Engine &engine)
{
    surpriseShowFrom(engine, isOldEffect);
}

static bool fileExists(const std::string &path)
{
    std::ifstream in(path);
    return in.good();
}

// test/golden/, found from this file's own path (or the project folder, where pio runs tests)
static std::string findGoldenDir()
{
    std::vector<std::string> candidates;
    std::string here = __FILE__;
    size_t cut = here.rfind("test_effects");
    if (cut != std::string::npos)
        candidates.push_back(here.substr(0, cut) + "golden/");
    candidates.push_back("test/golden/");
    candidates.push_back("../golden/");
    for (const std::string &dir : candidates)
        if (fileExists(dir + "index.txt"))
            return dir;
    return "";
}

// The event lines of a golden file (comment lines start with #)
static bool readLines(const std::string &path, std::vector<std::string> &lines)
{
    std::ifstream in(path);
    if (!in.good())
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        lines.push_back(line);
    }
    return true;
}

static bool isWait(const std::string &event, long &ms)
{
    if (event.size() < 3 || event[0] != 'W' || event[1] != ' ')
        return false;
    ms = std::strtol(event.c_str() + 2, nullptr, 10);
    return true;
}

// Same event, allowing waits to differ by 1 ms
static bool sameEvent(const std::string &a, const std::string &b)
{
    if (a == b)
        return true;
    long x, y;
    if (isWait(a, x) && isWait(b, y))
        return std::labs(x - y) <= 1;
    return false;
}

// Index of the first event that differs, or -1 if the recordings match
static long firstDifference(const std::vector<std::string> &expected, const std::vector<std::string> &actual)
{
    size_t n = expected.size() < actual.size() ? expected.size() : actual.size();
    for (size_t i = 0; i < n; i++)
        if (!sameEvent(expected[i], actual[i]))
            return static_cast<long>(i);
    if (expected.size() != actual.size())
        return static_cast<long>(n);
    return -1;
}

static std::vector<std::string> withoutMouth(const std::vector<std::string> &events)
{
    std::vector<std::string> out;
    for (const std::string &e : events)
        if (e != "M")
            out.push_back(e);
    return out;
}

static std::vector<std::string> eyesSwapped(const std::vector<std::string> &events)
{
    std::vector<std::string> out;
    for (const std::string &e : events)
    {
        if (e.size() == 35 && e[0] == 'F')
            out.push_back("F " + e.substr(19, 16) + " " + e.substr(2, 16));
        else
            out.push_back(e);
    }
    return out;
}

static std::string eventAt(const std::vector<std::string> &events, long i)
{
    if (i < 0 || i >= static_cast<long>(events.size()))
        return "(end)";
    return events[i];
}

// Both eyes of a frame line as dots, one row per line
static void printFramePair(const std::string &python, const std::string &cpp)
{
    auto rowOf = [](const std::string &event, int eye, int row) -> int {
        if (event.size() != 35 || event[0] != 'F')
            return -1;
        return static_cast<int>(std::strtol(event.substr(2 + eye * 17 + row * 2, 2).c_str(), nullptr, 16));
    };
    auto dots = [](int value) -> std::string {
        if (value < 0)
            return "        ";
        std::string s;
        for (int bit = 7; bit >= 0; bit--)
            s += (value >> bit) & 1 ? '#' : '.';
        return s;
    };
    std::printf("      python (left right)      c++ (left right)\n");
    for (int row = 0; row < 8; row++)
        std::printf("      %s %s        %s %s\n", dots(rowOf(python, 0, row)).c_str(), dots(rowOf(python, 1, row)).c_str(),
                    dots(rowOf(cpp, 0, row)).c_str(), dots(rowOf(cpp, 1, row)).c_str());
}

static void printContext(const std::string &id, const std::vector<std::string> &expected,
                         const std::vector<std::string> &actual, long diff)
{
    std::printf("\n---- %s: first difference at event %ld (python has %zu events, c++ has %zu) ----\n", id.c_str(), diff,
                expected.size(), actual.size());
    std::printf("      %-40s %s\n", "python (golden)", "c++");
    for (long i = diff - 4; i <= diff + 4; i++)
    {
        if (i < 0)
            continue;
        if (i >= static_cast<long>(expected.size()) && i >= static_cast<long>(actual.size()))
            break;
        std::printf("%s%5ld %-40s %s\n", i == diff ? ">>" : "  ", i, eventAt(expected, i).c_str(), eventAt(actual, i).c_str());
    }
    std::string a = eventAt(expected, diff), b = eventAt(actual, diff);
    if (a[0] == 'F' || b[0] == 'F')
        printFramePair(a, b);
}

static void saveActual(const std::string &id, const std::vector<std::string> &events)
{
    const char *dir = std::getenv("PUMPKIN_ACTUAL_DIR");
    if (!dir || !*dir)
        return;
    std::ofstream out(std::string(dir) + "/" + id + ".txt");
    for (const std::string &e : events)
        out << e << "\n";
}

// Runs one golden case. Returns "" if it matches, or what went wrong.
static std::string runCase(const GoldenCase &c)
{
    std::vector<std::string> expected;
    if (!readLines(gGoldenDir + c.id + ".txt", expected))
        return "can't read " + gGoldenDir + c.id + ".txt";

    bool neverEnds = false;
    Runner run = nullptr;
    if (const ExtraCase *extra = findExtraCase(c.id))
    {
        run = extra->run;
    }
    else if (looksExtra(c.id))
    {
        return "no C++ call for golden file " + c.id + ".txt: add it to kExtraCases in test_main.cpp";
    }
    else
    {
        const EffectInfo *fx = findEffect(c.id.c_str());
        if (!fx)
            return "findEffect(\"" + c.id + "\") returned nullptr: no such effect in registry.cpp";
        run = fx->run;
        if (isSurpriseShow(run))
        {
            run = oldSurpriseShow;
            neverEnds = true;
        }
    }

    // The surprise show never ends: compare as many events as the Python recording has, then stop it
    RecordingEngine engine;
    engine.eventLimit = neverEnds ? expected.size() : expected.size() * 2 + 1000;
    try
    {
        run(engine);
    }
    catch (const EffectInterrupted &)
    {
        if (!engine.hitLimit)
            return "the effect threw EffectInterrupted, but nobody interrupted it";
    }
    catch (...)
    {
        return "the effect threw an exception";
    }
    saveActual(c.id, engine.events);

    if (neverEnds && !engine.hitLimit)
        return "stopped by itself after " + std::to_string(engine.events.size()) + " events (it should go on forever)";
    if (!neverEnds && engine.hitLimit)
        return "never stopped: still going after " + std::to_string(engine.eventLimit) + " events (python has " +
               std::to_string(expected.size()) + ")";

    long diff = firstDifference(expected, engine.events);
    if (diff < 0)
        return "";

    printContext(c.id, expected, engine.events, diff);

    std::string msg = c.id + ": event " + std::to_string(diff) + " differs: python \"" + eventAt(expected, diff) +
                      "\", c++ \"" + eventAt(engine.events, diff) + "\" (python " + std::to_string(expected.size()) +
                      " events, c++ " + std::to_string(engine.events.size()) + ")";

    // Hints for the usual suspects
    if (firstDifference(withoutMouth(expected), withoutMouth(engine.events)) < 0)
        msg += ". Hint: only the mouth (M) events differ";
    else if (firstDifference(expected, eyesSwapped(engine.events)) < 0)
        msg += ". Hint: matches if the eyes are swapped";
    return msg;
}

static void testGoldenCase()
{
    static std::string failure;
    failure = runCase(gCases[gCurrent]);
    if (!failure.empty())
        TEST_FAIL_MESSAGE(failure.c_str());
}

// Every golden recording of a button is still a button in the registry, and every extra C++ call has
// its recording. New effects needn't have a golden file (the old pumpkin never had them): the smoke
// tests check those instead.
static void test_golden_files_match_registry()
{
    static std::string failure;
    failure.clear();
    if (gCases.empty())
        TEST_FAIL_MESSAGE("can't read test/golden/index.txt. Run: python3 tools/capture_python_frames.py");
    for (const std::string &id : gOldIds)
        if (!findEffect(id.c_str()))
            failure += " " + id + " (golden file, but no such effect in registry.cpp)";
    static std::set<std::string> golden; // static: Unity fails a test with longjmp
    golden.clear();
    for (const GoldenCase &c : gCases)
        golden.insert(c.id);
    for (const ExtraCase &c : kExtraCases)
        if (!golden.count(c.id))
            failure += std::string(" ") + c.id + " (in kExtraCases, but not in test/golden/index.txt)";
    if (!failure.empty())
    {
        failure = "Mismatched ids:" + failure;
        TEST_FAIL_MESSAGE(failure.c_str());
    }
}

// The old surprise show picked from every effect except the long spell (and itself). Only the old
// effects are checked: new ones choose for themselves with inSurprise.
static void test_surprise_mix_matches_python()
{
    static std::string failure;
    failure.clear();
    for (const std::string &id : gOldIds)
    {
        const EffectInfo *fx = findEffect(id.c_str());
        if (!fx)
            continue; // test_golden_files_match_registry reports missing effects
        bool expected = id != "spell" && id != "surprise";
        if (fx->inSurprise != expected)
            failure += " " + id + (expected ? " (should be in it)" : " (should not be in it)");
    }
    if (!failure.empty())
    {
        failure = "Surprise mix differs from pumpkin-buttons.pl:" + failure;
        TEST_FAIL_MESSAGE(failure.c_str());
    }
}

// The talking pumpkin says the same phrase as the old button page when each effect starts
static void test_phrases_match_python()
{
    static std::string failure;
    failure.clear();
    std::vector<std::string> lines;
    if (gGoldenDir.empty() || !readLines(gGoldenDir + "phrases.txt", lines))
        TEST_FAIL_MESSAGE("can't read test/golden/phrases.txt");
    for (const std::string &line : lines)
    {
        size_t space = line.find(' ');
        std::string id = line.substr(0, space);
        std::string expected = space == std::string::npos ? "" : line.substr(space + 1);
        const EffectInfo *fx = findEffect(id.c_str());
        if (!fx)
            continue; // test_golden_files_match_registry reports missing effects
        std::string actual = fx->phrase ? fx->phrase : "";
        if (actual != expected)
            failure += " " + id + ": \"" + actual + "\" (python \"" + expected + "\");";
    }
    if (!failure.empty())
    {
        failure = "Phrases differ from pumpkin-buttons.pl:" + failure;
        TEST_FAIL_MESSAGE(failure.c_str());
    }
}

// ---- The registry (effects/registry.cpp) ----

// Every effect's id is unique, non-empty, safe in a URL (/api/play/<id>) and not "words", which the
// player and the web page use for typed words
static void test_registry_ids()
{
    static std::string failure;
    failure.clear();
    size_t count = 0;
    const EffectInfo *effects = allEffects(count);
    static std::set<std::string> seen; // static: Unity fails a test with longjmp
    seen.clear();
    for (size_t i = 0; i < count; i++)
    {
        const char *id = effects[i].id;
        if (!id || !*id)
        {
            failure += " effect " + std::to_string(i) + " has no id;";
            continue;
        }
        if (!seen.insert(id).second)
            failure += std::string(" ") + id + " is used twice;";
        if (std::strcmp(id, "words") == 0)
            failure += " \"words\" is reserved for typed words;";
        for (const char *c = id; *c; c++)
        {
            if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-'))
            {
                failure += std::string(" ") + id + " has '" + *c + "' (only a-z, 0-9 and - are allowed);";
                break;
            }
        }
    }
    if (!failure.empty())
    {
        failure = "Bad effect ids:" + failure;
        TEST_FAIL_MESSAGE(failure.c_str());
    }
}

// True if s is well-formed UTF-8 with no control characters (the web page gets it as JSON)
static bool isCleanUtf8(const char *s)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
    while (*p)
    {
        if (*p < 0x20 || *p == 0x7f)
            return false;
        const int more = *p < 0x80 ? 0 : (*p >> 5) == 0x06 ? 1 : (*p >> 4) == 0x0e ? 2 : (*p >> 3) == 0x1e ? 3 : -1;
        if (more < 0)
            return false;
        p++;
        for (int i = 0; i < more; i++, p++)
            if ((*p & 0xc0) != 0x80)
                return false;
    }
    return true;
}

// Every effect has a button: a label, an emoji, a section and something to run
static void test_registry_buttons()
{
    static std::string failure;
    failure.clear();
    size_t count = 0;
    const EffectInfo *effects = allEffects(count);
    for (size_t i = 0; i < count; i++)
    {
        const EffectInfo &fx = effects[i];
        const std::string id = fx.id ? fx.id : "effect " + std::to_string(i);
        if (!fx.label || !*fx.label)
            failure += " " + id + " has no label;";
        else if (!isCleanUtf8(fx.label))
            failure += " " + id + "'s label isn't clean UTF-8;";
        if (!fx.emoji || !*fx.emoji)
            failure += " " + id + " has no emoji;";
        else if (!isCleanUtf8(fx.emoji))
            failure += " " + id + "'s emoji isn't clean UTF-8;";
        if (!*groupName(fx.group))
            failure += " " + id + " has no section;";
        if (!fx.run)
            failure += " " + id + " has no function;";
    }
    if (!failure.empty())
    {
        failure = "Bad buttons:" + failure;
        TEST_FAIL_MESSAGE(failure.c_str());
    }
}

// Whether the font has a picture for c (a missing character scrolls as a gap)
static bool fontHas(const Font &font, char c)
{
    for (size_t i = 0; i < font.count; i++)
        if (font.glyphs[i].ch == c)
            return true;
    return false;
}

// Phrases are plain printable ASCII that the eyes' font can show
static void test_registry_phrases()
{
    static std::string failure;
    failure.clear();
    size_t count = 0;
    const EffectInfo *effects = allEffects(count);
    for (size_t i = 0; i < count; i++)
    {
        const EffectInfo &fx = effects[i];
        if (!fx.phrase)
            continue;
        const std::string id = fx.id ? fx.id : "effect " + std::to_string(i);
        if (!*fx.phrase)
            failure += " " + id + " has an empty phrase (use nullptr for none);";
        for (const char *c = fx.phrase; *c; c++)
        {
            const unsigned char u = static_cast<unsigned char>(*c);
            if (u < 0x20 || u > 0x7e)
            {
                failure += " " + id + "'s phrase has byte " + std::to_string(u) + " (not printable ASCII);";
                break;
            }
            if (*c != ' ' && !fontHas(textFont, *c))
            {
                failure += " " + id + "'s phrase has '" + *c + "', which textFont can't show;";
                break;
            }
        }
    }
    if (!failure.empty())
    {
        failure = "Bad phrases:" + failure;
        TEST_FAIL_MESSAGE(failure.c_str());
    }
}

// ---- Smoke tests: every effect at the extremes of the settings ----

// Generous: the default show, the longest, is under 5,000 events at the widest eye gap
constexpr size_t kSmokeEventLimit = 20000;
// The surprise show never ends, so only this much of it is played
constexpr size_t kSurprisePrefixEvents = 5000;
// A longer wait is probably seconds mixed up with milliseconds
constexpr long kLongestWaitMs = 10000;
// Every dot on both eyes draws a lot of battery current (CLAUDE.md: avoid long all-on frames)
constexpr long kLongestAllOnMs = 500;

struct SmokeSettings
{
    uint8_t maxBright;
    uint8_t eyeGap;
    std::string name;
};

// The settings each smoke test plays at: lowest and highest brightness and eye gap, with the longest
// name allowed. Effects without a golden file also play at the golden settings.
static std::vector<SmokeSettings> smokeSettings(bool hasGolden)
{
    std::string longName = "Sam & Alex's Pumpkin";
    longName.resize(kMaxNameLength, '!');
    std::vector<SmokeSettings> out;
    if (!hasGolden)
        out.push_back({RecordingEngine::kGoldenMaxBright, RecordingEngine::kGoldenEyeGap, RecordingEngine::kGoldenName});
    for (uint8_t bright : {static_cast<uint8_t>(0), kMaxBrightness})
        for (uint8_t gap : {static_cast<uint8_t>(0), kMaxEyeGap})
            out.push_back({bright, gap, longName});
    return out;
}

struct SmokeCase
{
    std::string id;
    Runner run;
    bool hasGolden; // already compared with Python at the golden settings
    std::string testName;
    bool asBlock = false; // play effect `id` as a show block (playBlock) instead of calling run
};

static std::vector<SmokeCase> gSmokeCases;
static size_t gSmokeCurrent = 0;

static bool isAllOn(const std::string &event)
{
    static const std::string allOn = "F " + std::string(16, 'f') + " " + std::string(16, 'f');
    return event == allOn;
}

// Checks one smoke recording. Returns "" if it's fine, or the first problem (and sets `at` to its event).
static std::string checkRecording(const std::vector<std::string> &events, uint8_t maxBright, bool finished, long &at)
{
    long brightness = maxBright; // the player sets maxBright() before every effect
    bool waitedSinceFrame = true;
    bool allOn = false;
    long allOnMs = 0;
    size_t frames = 0;
    for (size_t i = 0; i < events.size(); i++)
    {
        const std::string &e = events[i];
        at = static_cast<long>(i);
        long value = 0;
        if (e.size() == 35 && e[0] == 'F')
        {
            if (!waitedSinceFrame)
                return "a frame straight after another with no wait, so the first is never seen";
            waitedSinceFrame = false;
            allOn = isAllOn(e);
            allOnMs = 0;
            frames++;
        }
        else if (isWait(e, value))
        {
            if (value < 0 || value > kLongestWaitMs)
                return "a wait of " + std::to_string(value) + " ms (more than " + std::to_string(kLongestWaitMs / 1000) +
                       " s: seconds mixed up with milliseconds?)";
            waitedSinceFrame = true;
            allOnMs += allOn ? value : 0;
            if (allOnMs > kLongestAllOnMs)
                return "every dot on both eyes lit for more than " + std::to_string(kLongestAllOnMs) +
                       " ms (a lot of battery current)";
        }
        else if (e.size() > 2 && e[0] == 'B' && e[1] == ' ')
        {
            brightness = std::strtol(e.c_str() + 2, nullptr, 10);
            if (brightness > kChipMaxBrightness)
                return "brightness " + std::to_string(brightness) + ", above the MAX7219's top setting of " +
                       std::to_string(kChipMaxBrightness);
        }
        else if (e[0] == 'S')
        {
            if (e.size() <= 2)
                return "says nothing (an empty say())";
        }
        else if (e != "M")
        {
            return "an event the recording engine doesn't make";
        }
    }
    at = -1;
    if (frames == 0)
        return "shows nothing";
    if (finished && brightness != maxBright)
        return "ends at brightness " + std::to_string(brightness) + ", not maxBright() (" + std::to_string(maxBright) +
               "): leave it there when the effect ends";
    return "";
}

// The events leading up to a smoke test's problem
static void printSmokeContext(const std::string &label, const std::vector<std::string> &events, long at)
{
    std::printf("\n---- %s: problem at event %ld of %zu ----\n", label.c_str(), at, events.size());
    for (long i = at - 6; i <= at; i++)
        if (i >= 0)
            std::printf("%s%5ld %s\n", i == at ? ">>" : "  ", i, events[i].c_str());
}

// Plays one smoke case at one group of settings. Returns "" if it's fine, or what went wrong.
static std::string smokeRun(const SmokeCase &c, const SmokeSettings &settings)
{
    const std::string label = c.id + " at maxBright " + std::to_string(settings.maxBright) + ", eyeGap " +
                              std::to_string(settings.eyeGap);
    const bool neverEnds = !c.asBlock && isSurpriseShow(c.run);
    RecordingEngine engine(settings.maxBright, settings.eyeGap, settings.name.c_str());
    engine.eventLimit = neverEnds ? kSurprisePrefixEvents : kSmokeEventLimit;
    try
    {
        if (c.asBlock)
            playBlock(engine, ShowBlock{ShowBlock::Kind::Effect, c.id});
        else
            c.run(engine);
    }
    catch (const EffectInterrupted &)
    {
        if (!engine.hitLimit)
            return label + ": threw EffectInterrupted, but nobody interrupted it";
    }
    catch (...)
    {
        return label + ": threw an exception";
    }
    if (neverEnds && !engine.hitLimit)
        return label + ": stopped by itself after " + std::to_string(engine.events.size()) +
               " events (it should go on until interrupted)";
    if (!neverEnds && engine.hitLimit)
        return label + ": never stopped: still going after " + std::to_string(engine.eventLimit) + " events";

    // A show block starts with the effect's phrase, as a button press does
    if (c.asBlock)
    {
        const EffectInfo *fx = findEffect(c.id.c_str());
        const std::string said = fx && fx->phrase ? std::string("S ") + fx->phrase : "";
        if (!said.empty() && (engine.events.empty() || engine.events[0] != said))
            return label + ": as a show block, it should say \"" + fx->phrase + "\" first";
    }

    long at = -1;
    std::string problem = checkRecording(engine.events, settings.maxBright, !neverEnds, at);
    if (problem.empty())
        return "";
    if (at >= 0)
    {
        printSmokeContext(label, engine.events, at);
        return label + ": event " + std::to_string(at) + " \"" + engine.events[at] + "\": " + problem;
    }
    return label + ": " + problem;
}

static std::string runSmokeCase(const SmokeCase &c)
{
    for (const SmokeSettings &settings : smokeSettings(c.hasGolden))
    {
        std::string problem = smokeRun(c, settings);
        if (!problem.empty())
            return problem;
    }
    return "";
}

static void testSmokeCase()
{
    static std::string failure;
    failure = runSmokeCase(gSmokeCases[gSmokeCurrent]);
    if (!failure.empty())
        TEST_FAIL_MESSAGE(failure.c_str());
}

// ---- Frame maths (display/frame.h), checked against main.py's place() and lower() ----

static const Frame kPic = {0x81, 0x42, 0x24, 0x18, 0xf0, 0x0f, 0xaa, 0x55};

static void assertFrame(const Frame &expected, const Frame &actual)
{
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), actual.data(), 8);
}

static void test_place()
{
    assertFrame(kPic, place(kPic, 0));
    // Python: [row >> x for row in picture]
    assertFrame(Frame{0x10, 0x08, 0x04, 0x03, 0x1e, 0x01, 0x15, 0x0a}, place(kPic, 3));
    // Python: [(row << -x) & 0xff for row in picture]
    assertFrame(Frame{0x08, 0x10, 0x20, 0xc0, 0x80, 0x78, 0x50, 0xa8}, place(kPic, -3));
    assertFrame(Frame{0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00}, place(kPic, 7));
    assertFrame(Frame{0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x80}, place(kPic, -7));
    assertFrame(kBlank, place(kPic, 8));
    assertFrame(kBlank, place(kPic, -8));
    assertFrame(kBlank, place(kPic, 24));
    assertFrame(kBlank, place(kPic, -24));
}

static void test_lower()
{
    assertFrame(kPic, lower(kPic, 0));
    // Python: ([0] * rows + picture + [0] * 8)[:8]
    assertFrame(Frame{0x00, 0x00, 0x00, 0x81, 0x42, 0x24, 0x18, 0xf0}, lower(kPic, 3));
    assertFrame(Frame{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81}, lower(kPic, 7));
    assertFrame(kBlank, lower(kPic, 8));
    assertFrame(kBlank, lower(kPic, 12));
}

static void test_overlay()
{
    assertFrame(Frame{0x81, 0x42, 0x24, 0x18, 0xff, 0xff, 0xaa, 0x55},
                overlay(kPic, Frame{0x00, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00}));
    assertFrame(kPic, overlay(kPic, kBlank));
    assertFrame(kPic, overlay(kBlank, kPic));
    assertFrame(kPic, overlay(kPic, kPic));
}

// ---- Fonts (display/font.h) ----

static const Glyph kTinyGlyphs[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0, 0}},
    {'A', {0x38, 0x6c, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0x00}},
    {'1', {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x00}},
};
static const Font kTinyFont = {kTinyGlyphs, sizeof(kTinyGlyphs) / sizeof(kTinyGlyphs[0])};

static void test_font_get()
{
    assertFrame(kTinyGlyphs[1].rows, kTinyFont.get('A'));
    assertFrame(kTinyGlyphs[2].rows, kTinyFont.get('1'));
    // Python: font.get(c, blank)
    assertFrame(kBlank, kTinyFont.get('$'));
    assertFrame(kBlank, kTinyFont.get('a'));
    assertFrame(kBlank, kTinyFont.get('\0'));
    const Font empty = {nullptr, 0};
    assertFrame(kBlank, empty.get('A'));
}

// Every character of every font matches matrix_fonts.py (test/golden/fonts.txt)
static void test_fonts_match_python()
{
    static std::string failure;
    failure.clear();
    std::vector<std::string> lines;
    if (gGoldenDir.empty() || !readLines(gGoldenDir + "fonts.txt", lines))
        TEST_FAIL_MESSAGE("can't read test/golden/fonts.txt");
    int bad = 0;
    for (const std::string &line : lines)
    {
        char name[32];
        int code = 0;
        char hex[17];
        if (std::sscanf(line.c_str(), "%31s %d %16s", name, &code, hex) != 3)
            continue;
        const Font *font = nullptr;
        if (!std::strcmp(name, "textFont"))
            font = &textFont;
        else if (!std::strcmp(name, "witchFont"))
            font = &witchFont;
        else if (!std::strcmp(name, "batFont"))
            font = &batFont;
        else if (!std::strcmp(name, "pacmanFont"))
            font = &pacmanFont;
        if (!font)
        {
            failure += std::string(" unknown font ") + name + ";";
            bad++;
            continue;
        }
        std::string actual = RecordingEngine::hex(font->get(static_cast<char>(code)));
        if (actual != hex)
        {
            if (++bad <= 8)
                failure += std::string(" ") + name + " char " + std::to_string(code) + " is " + actual + ", python " + hex + ";";
        }
    }
    if (bad)
    {
        failure = std::to_string(bad) + " font pictures differ:" + failure;
        TEST_FAIL_MESSAGE(failure.c_str());
    }
}

// ---- Building blocks (effects/primitives.h) ----

// main.py's text_window(), worked out in Python for these messages and columns
static void test_text_window()
{
    struct Case
    {
        const char *message;
        int x;
        Frame expected;
    };
    static const Case cases[] = {
        {"AB", -17, kBlank},
        {"AB", -9, kBlank},
        {"AB", -8, kBlank},
        {"AB", -5, {0x01, 0x03, 0x06, 0x07, 0x06, 0x06, 0x06, 0x00}},
        {"AB", -1, {0x1c, 0x36, 0x63, 0x7f, 0x63, 0x63, 0x63, 0x00}},
        {"AB", 0, {0x38, 0x6c, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0x00}},
        {"AB", 3, {0xc7, 0x63, 0x33, 0xf3, 0x33, 0x33, 0x37, 0x00}},
        {"AB", 7, {0x7e, 0x33, 0x33, 0x3e, 0x33, 0x33, 0x7e, 0x00}},
        {"AB", 8, {0xfc, 0x66, 0x66, 0x7c, 0x66, 0x66, 0xfc, 0x00}},
        {"AB", 13, {0x80, 0xc0, 0xc0, 0x80, 0xc0, 0xc0, 0x80, 0x00}},
        {"AB", 16, kBlank},
        {"AB", 17, kBlank},
        {"AB", 24, kBlank},
        // '$' isn't in the font, so it's a gap
        {"A$1", 3, {0xc0, 0x60, 0x30, 0xf0, 0x30, 0x30, 0x30, 0x00}},
        {"A$1", 7, kBlank},
        {"A$1", 8, kBlank},
        {"A$1", 13, {0x03, 0x07, 0x03, 0x03, 0x03, 0x03, 0x0f, 0x00}},
        {"A$1", 16, {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x00}},
        {"A$1", 17, {0x30, 0x70, 0x30, 0x30, 0x30, 0x30, 0xfc, 0x00}},
        {"A$1", 23, kBlank},
        {"", -3, kBlank},
        {"", 0, kBlank},
    };
    for (const Case &c : cases)
    {
        char label[48];
        std::snprintf(label, sizeof(label), "textWindow(\"%s\", x=%d)", c.message, c.x);
        Frame actual = textWindow(textFont, c.message, std::strlen(c.message), c.x);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(c.expected.data(), actual.data(), 8, label);
    }
}

// Python's // and % round down for negative numbers
static void test_floor_div_mod()
{
    TEST_ASSERT_EQUAL_INT(-3, floorDiv(-8, 3));
    TEST_ASSERT_EQUAL_INT(1, floorMod(-8, 3));
    TEST_ASSERT_EQUAL_INT(-1, floorDiv(-1, 8));
    TEST_ASSERT_EQUAL_INT(7, floorMod(-1, 8));
    TEST_ASSERT_EQUAL_INT(-2, floorDiv(-16, 8));
    TEST_ASSERT_EQUAL_INT(0, floorMod(-16, 8));
    TEST_ASSERT_EQUAL_INT(3, floorDiv(7, 2));
    TEST_ASSERT_EQUAL_INT(1, floorMod(7, 2));
    TEST_ASSERT_EQUAL_INT(0, floorDiv(0, 4));
    TEST_ASSERT_EQUAL_INT(2, floorMod(-4, 3));
}

// round(seconds * 1000) as Python does it (halves go to the even number)
static void test_seconds_to_ms()
{
    TEST_ASSERT_EQUAL_UINT32(62, secondsToMs(0.0625));
    TEST_ASSERT_EQUAL_UINT32(2, secondsToMs(0.0025));
    TEST_ASSERT_EQUAL_UINT32(4, secondsToMs(0.0035));
    TEST_ASSERT_EQUAL_UINT32(0, secondsToMs(0.0005));
    TEST_ASSERT_EQUAL_UINT32(2, secondsToMs(0.0015));
    TEST_ASSERT_EQUAL_UINT32(1000, secondsToMs(1.0005));
    TEST_ASSERT_EQUAL_UINT32(275, secondsToMs(0.3 - 1 * 0.025));
    TEST_ASSERT_EQUAL_UINT32(300, secondsToMs(0.3));
    TEST_ASSERT_EQUAL_UINT32(0, secondsToMs(0.0));
}

// randomDots() ANDs one random byte per point of sparseness, in order
static void test_random_dots()
{
    RecordingEngine a, b;
    uint8_t expected = 0xff;
    for (int i = 0; i < 3; i++)
        expected &= static_cast<uint8_t>(b.random(256));
    TEST_ASSERT_EQUAL_HEX8(expected, randomDots(a, 3));
    TEST_ASSERT_EQUAL_UINT32(3, a.randomCalls);
    RecordingEngine c;
    TEST_ASSERT_EQUAL_HEX8(0xff, randomDots(c, 0));
    TEST_ASSERT_EQUAL_UINT32(0, c.randomCalls);
}

// ---- The fruit machine (effects.cpp) ----

namespace fm = fruit_machine;

static const char *symbolName(fm::Symbol symbol)
{
    static const char *const names[] = {"cherries", "bell", "BAR", "melon", "double BAR", "JACK/POT"};
    const int i = static_cast<int>(symbol);
    return i >= 0 && i < fm::kSymbols ? names[i] : "(not a symbol)";
}

// Every roll and pick: the jackpot is 1 roll in 16 and any win 4 in 16. Wins show the same symbol on
// both reels (never JACK and POT), loses two different ones (never JACK and POT together), and every
// symbol that can win, and every pair that loses, comes up equally often.
static void test_fruit_machine_odds()
{
    static std::string failure; // static: Unity fails a test with longjmp
    failure.clear();
    int jackpots = 0, wins = 0, loses = 0;
    for (uint32_t roll = 0; roll < fm::kRolls; roll++)
    {
        const fm::Result result = fm::spinFor(roll, 0).result;
        jackpots += result == fm::Result::Jackpot;
        wins += result == fm::Result::Win;
        loses += result == fm::Result::Lose;
        int winning[fm::kSymbols] = {};
        int losing[fm::kSymbols][fm::kSymbols] = {};
        for (uint32_t pick = 0; pick < fm::kPicks; pick++)
        {
            const fm::Spin spin = fm::spinFor(roll, pick);
            const int left = static_cast<int>(spin.left);
            const int right = static_cast<int>(spin.right);
            const std::string at = " roll " + std::to_string(roll) + " pick " + std::to_string(pick) + " (" +
                                   symbolName(spin.left) + ", " + symbolName(spin.right) + "):";
            if (spin.result != result)
                failure += at + " the pick changed the result;";
            if (left < 0 || left >= fm::kSymbols || right < 0 || right >= fm::kSymbols)
            {
                failure += at + " not a symbol;";
                continue;
            }
            const bool jackAndPot = spin.left == fm::Symbol::JackOrPot && spin.right == fm::Symbol::JackOrPot;
            switch (spin.result)
            {
            case fm::Result::Jackpot:
                if (!jackAndPot)
                    failure += at + " a jackpot that isn't JACK and POT;";
                break;
            case fm::Result::Win:
                if (left != right || jackAndPot)
                    failure += at + " a win needs the same symbol twice (but not JACK and POT);";
                winning[left]++;
                break;
            case fm::Result::Lose:
                if (left == right)
                    failure += at + " a lose with the same symbol twice;";
                losing[left][right]++;
                break;
            }
        }
        for (int a = 0; a < fm::kSymbols; a++)
        {
            const bool canWin = a != static_cast<int>(fm::Symbol::JackOrPot);
            if (result == fm::Result::Win && winning[a] != (canWin ? 6 : 0))
                failure += " roll " + std::to_string(roll) + ": " + symbolName(static_cast<fm::Symbol>(a)) + " wins " +
                           std::to_string(winning[a]) + " picks in 30;";
            for (int b = 0; b < fm::kSymbols && result == fm::Result::Lose; b++)
                if (losing[a][b] != (a != b ? 1 : 0))
                    failure += " roll " + std::to_string(roll) + ": " + symbolName(static_cast<fm::Symbol>(a)) + " and " +
                               symbolName(static_cast<fm::Symbol>(b)) + " lose " + std::to_string(losing[a][b]) +
                               " times;";
        }
    }
    if (jackpots != 1 || wins != 3 || loses != 12)
        failure += " rolls: " + std::to_string(jackpots) + " jackpot, " + std::to_string(wins) + " win, " +
                   std::to_string(loses) + " lose (should be 1, 3 and 12 in 16);";
    if (!failure.empty())
    {
        failure = "Fruit machine odds:" + failure;
        TEST_FAIL_MESSAGE(failure.c_str());
    }
}

// The frames (as "F ..." event lines) and the total time of a recording
static std::vector<std::string> framesOf(const std::vector<std::string> &events, long &totalMs)
{
    std::vector<std::string> frames;
    totalMs = 0;
    for (const std::string &e : events)
    {
        long ms = 0;
        if (e.size() == 35 && e[0] == 'F')
            frames.push_back(e);
        else if (isWait(e, ms))
            totalMs += ms;
    }
    return frames;
}

static std::string frameLine(const Frame &left, const Frame &right)
{
    return "F " + RecordingEngine::hex(left) + " " + RecordingEngine::hex(right);
}

// Every pair of symbols: the reels stop on them, the left one first, after 3 to 5 seconds of spinning
static void test_fruit_machine_reels_land()
{
    static std::string failure;
    failure.clear();
    for (int a = 0; a < fm::kSymbols; a++)
    {
        for (int b = 0; b < fm::kSymbols; b++)
        {
            const fm::Spin spin = {fm::Result::Lose, static_cast<fm::Symbol>(a), static_cast<fm::Symbol>(b)};
            const std::string label = std::string(" ") + symbolName(spin.left) + " and " + symbolName(spin.right) + ":";
            RecordingEngine engine;
            engine.eventLimit = kSmokeEventLimit;
            fm::spinReels(engine, spin);
            long ms = 0;
            const std::vector<std::string> frames = framesOf(engine.events, ms);
            const std::string leftEye = RecordingEngine::hex(fm::picture(spin.left, false));
            const std::string rightEye = RecordingEngine::hex(fm::picture(spin.right, true));
            if (frames.empty() || frames.back() != frameLine(fm::picture(spin.left, false), fm::picture(spin.right, true)))
            {
                failure += label + " the reels stop on " + (frames.empty() ? "nothing" : frames.back()) + ";";
                continue;
            }
            // The last frame each reel is still moving (or wobbling)
            long leftMoving = -1, rightMoving = -1;
            for (size_t i = 0; i < frames.size(); i++)
            {
                if (frames[i].substr(2, 16) != leftEye)
                    leftMoving = static_cast<long>(i);
                if (frames[i].substr(19, 16) != rightEye)
                    rightMoving = static_cast<long>(i);
            }
            if (leftMoving >= rightMoving)
                failure += label + " the left reel should land first;";
            if (ms < 3000 || ms > 5000)
                failure += label + " spins for " + std::to_string(ms) + " ms;";
        }
    }
    if (!failure.empty())
    {
        failure = "Fruit machine reels:" + failure;
        TEST_FAIL_MESSAGE(failure.c_str());
    }
}

// Each kind of result at every smoke-test setting: it lands on the right symbols (the first picture held
// for a while), says the right thing, and passes the smoke checks
static void test_fruit_machine_results()
{
    static std::string failure;
    failure.clear();
    using fm::Result;
    using fm::Symbol;
    const struct
    {
        fm::Spin spin;
        const char *says;
    } cases[] = {
        {{Result::Lose, Symbol::Bell, Symbol::Melon}, "Oh no! Try again!"},
        {{Result::Lose, Symbol::JackOrPot, Symbol::Cherries}, "Oh no! Try again!"}, // so near!
        {{Result::Lose, Symbol::DoubleBar, Symbol::JackOrPot}, "Oh no! Try again!"},
        {{Result::Win, Symbol::Cherries, Symbol::Cherries}, "You win!"},
        {{Result::Win, Symbol::Bar, Symbol::Bar}, "You win!"},
        {{Result::Jackpot, Symbol::JackOrPot, Symbol::JackOrPot}, "Jackpot!"},
    };
    for (const auto &c : cases)
    {
        for (const SmokeSettings &settings : smokeSettings(false))
        {
            const std::string label = std::string(" ") + symbolName(c.spin.left) + " and " + symbolName(c.spin.right) +
                                      " at maxBright " + std::to_string(settings.maxBright) + ", eyeGap " +
                                      std::to_string(settings.eyeGap) + ":";
            RecordingEngine engine(settings.maxBright, settings.eyeGap, settings.name.c_str());
            engine.eventLimit = kSmokeEventLimit;
            try
            {
                fm::play(engine, c.spin);
            }
            catch (...)
            {
                failure += label + " threw (or never finished);";
                continue;
            }
            long at = -1;
            const std::string problem = checkRecording(engine.events, settings.maxBright, true, at);
            if (!problem.empty())
                failure += label + " " + problem + ";";
            // The landed reels: the first picture that stays up for 400 ms or more
            std::string landed;
            for (size_t i = 0; i < engine.events.size() && landed.empty(); i++)
            {
                long held = 0, ms = 0;
                for (size_t j = i + 1; j < engine.events.size() && isWait(engine.events[j], ms); j++)
                    held += ms;
                if (engine.events[i][0] == 'F' && held >= 400)
                    landed = engine.events[i];
            }
            const std::string expected = frameLine(fm::picture(c.spin.left, false), fm::picture(c.spin.right, true));
            if (landed != expected)
                failure += label + " landed on \"" + landed + "\";";
            bool said = false;
            for (const std::string &e : engine.events)
                said = said || e == std::string("S ") + c.says;
            if (!said)
                failure += label + " never says \"" + c.says + "\";";
        }
    }
    if (!failure.empty())
    {
        failure = "Fruit machine results:" + failure;
        TEST_FAIL_MESSAGE(failure.c_str());
    }
}

int main()
{
    gGoldenDir = findGoldenDir();

    UNITY_BEGIN();
    RUN_TEST(test_place);
    RUN_TEST(test_lower);
    RUN_TEST(test_overlay);
    RUN_TEST(test_font_get);
    RUN_TEST(test_fonts_match_python);
    RUN_TEST(test_text_window);
    RUN_TEST(test_floor_div_mod);
    RUN_TEST(test_seconds_to_ms);
    RUN_TEST(test_random_dots);
    RUN_TEST(test_registry_ids);
    RUN_TEST(test_registry_buttons);
    RUN_TEST(test_registry_phrases);
    RUN_TEST(test_fruit_machine_odds);
    RUN_TEST(test_fruit_machine_reels_land);
    RUN_TEST(test_fruit_machine_results);

    std::vector<std::string> ids;
    if (gGoldenDir.empty() || !readLines(gGoldenDir + "index.txt", ids) || ids.empty())
    {
        std::printf("Can't find test/golden/index.txt. Run: python3 tools/capture_python_frames.py\n");
    }
    for (const std::string &id : ids)
    {
        if (id[0] == '~')
        {
            gOldIds.insert(id.substr(1)); // an old effect changed on purpose: no recording to compare
            continue;
        }
        gCases.push_back({id, "golden_" + id});
        if (!looksExtra(id))
            gOldIds.insert(id);
    }

    RUN_TEST(test_golden_files_match_registry);
    RUN_TEST(test_surprise_mix_matches_python);
    RUN_TEST(test_phrases_match_python);
    for (gCurrent = 0; gCurrent < gCases.size(); gCurrent++)
        UnityDefaultTestRun(testGoldenCase, gCases[gCurrent].testName.c_str(), __LINE__);

    // Every effect in the registry, then the golden-only calls (animations, building blocks...)
    size_t count = 0;
    const EffectInfo *effects = allEffects(count);
    for (size_t i = 0; i < count; i++)
        gSmokeCases.push_back({effects[i].id, effects[i].run, gOldIds.count(effects[i].id) > 0,
                               std::string("smoke_") + effects[i].id});
    for (const ExtraCase &c : kExtraCases)
        gSmokeCases.push_back({c.id, c.run, true, std::string("smoke_") + c.id});
    // ...and every effect that may go in a show, played as a block of one
    for (size_t i = 0; i < count; i++)
        if (canBeInShow(effects[i]))
            gSmokeCases.push_back({effects[i].id, effects[i].run, false, std::string("smoke_block_") + effects[i].id, true});
    for (gSmokeCurrent = 0; gSmokeCurrent < gSmokeCases.size(); gSmokeCurrent++)
        UnityDefaultTestRun(testSmokeCase, gSmokeCases[gSmokeCurrent].testName.c_str(), __LINE__);

    return UNITY_END();
}
