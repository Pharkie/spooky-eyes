// Unit tests for shows (src/effects/show.cpp): the default show is usable, checkBlock() and checkShow()
// accept, tidy and turn away the right things, and playBlock() plays a block the way a button press
// would. test_effects checks the default show frame by frame against the old pumpkin (default_show), and
// smoke-tests every effect as a show block.
//
//   pio test -e native -f test_show

#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "../recording_engine.h"
#include "core/constants.h"
#include "effects/registry.h"
#include "effects/show.h"

void setUp()
{
}

void tearDown()
{
}

// ---- Helpers ----

static ShowBlock effectBlock(const char *id)
{
    return ShowBlock{ShowBlock::Kind::Effect, id};
}

static ShowBlock wordsBlock(const std::string &words)
{
    return ShowBlock{ShowBlock::Kind::Words, words};
}

// checkBlock() on a copy of `block`. Returns "" if it gave the expected answer (and, if accepted, the
// expected tidied value), else what went wrong.
static std::string blockProblem(const char *what, const ShowBlock &block, bool expectOk, const std::string &expectValue,
                                const char *expectInError = nullptr)
{
    ShowBlock copy = block;
    std::string error;
    const bool ok = checkBlock(copy, error);
    if (ok != expectOk)
        return std::string(what) + (ok ? ": accepted, but shouldn't be" : ": turned away (" + error + "), but shouldn't be");
    if (ok && copy.value != expectValue)
        return std::string(what) + ": tidied to \"" + copy.value + "\", expected \"" + expectValue + "\"";
    if (!ok && error.empty())
        return std::string(what) + ": turned away with no reason";
    if (!ok && expectInError && error.find(expectInError) == std::string::npos)
        return std::string(what) + ": the reason \"" + error + "\" should mention \"" + expectInError + "\"";
    return "";
}

static void failIfAny(const std::string &problems)
{
    static std::string message; // static: Unity fails a test with longjmp
    message = problems;
    if (!message.empty())
        TEST_FAIL_MESSAGE(message.c_str());
}

// ---- The default show ----

// Every block of the default show exists, may go in a show and passes checkShow() untouched
static void test_default_show_is_valid()
{
    const Show &original = defaultShow();
    TEST_ASSERT_TRUE_MESSAGE(!original.empty(), "the default show is empty");
    TEST_ASSERT_TRUE_MESSAGE(original.size() <= kMaxShowBlocks, "the default show has more than kMaxShowBlocks blocks");

    std::string problems;
    for (size_t i = 0; i < original.size(); i++)
    {
        const ShowBlock &block = original[i];
        const std::string where = " block " + std::to_string(i + 1) + " (" + block.value + ")";
        if (block.kind != ShowBlock::Kind::Effect)
            continue; // words are checked by checkShow below
        const EffectInfo *effect = findEffect(block.value.c_str());
        if (!effect)
            problems += where + ": no such effect in registry.cpp;";
        else if (!canBeInShow(*effect))
            problems += where + ": can't go in a show;";
    }

    Show copy = original;
    std::string error;
    if (!checkShow(copy, error))
        problems += " checkShow turned it away: " + error + ";";
    for (size_t i = 0; i < copy.size() && i < original.size(); i++)
        if (copy[i].value != original[i].value || copy[i].kind != original[i].kind)
            problems += " checkShow changed block " + std::to_string(i + 1) + ";";
    failIfAny(problems.empty() ? "" : "Default show:" + problems);
}

// Only effects that never end (the surprise show) are kept out of shows
static void test_can_be_in_show()
{
    size_t count = 0;
    const EffectInfo *effects = allEffects(count);
    std::string problems;
    for (size_t i = 0; i < count; i++)
    {
        const bool expected = std::strcmp(effects[i].id, "surprise") != 0;
        if (canBeInShow(effects[i]) != expected)
            problems += std::string(" ") + effects[i].id + (expected ? " (should be allowed)" : " (should not be allowed)");
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(findEffect("surprise"), "no surprise show in the registry");
    failIfAny(problems.empty() ? "" : "canBeInShow:" + problems);
}

// ---- checkBlock ----

static void test_check_block_effects()
{
    std::string problems;
    problems += blockProblem("creeper", effectBlock("creeper"), true, "creeper");
    problems += blockProblem("spell (long, but it ends)", effectBlock("spell"), true, "spell");
    problems += blockProblem("name", effectBlock("name"), true, "name");
    problems += blockProblem("unknown id", effectBlock("dragon"), false, "", "dragon");
    problems += blockProblem("surprise (never ends)", effectBlock("surprise"), false, "", "Surprise show");
    problems += blockProblem("empty id", effectBlock(""), false, "");
    problems += blockProblem("wrong case", effectBlock("Creeper"), false, "");
    problems += blockProblem("spaces round the id", effectBlock(" creeper "), false, "");
    problems += blockProblem("words as an id", effectBlock("Happy Halloween!"), false, "");
    ShowBlock odd{static_cast<ShowBlock::Kind>(7), "creeper"};
    problems += blockProblem("unknown kind", odd, false, "");
    failIfAny(problems);
}

static void test_check_block_words()
{
    std::string problems;
    problems += blockProblem("plain words", wordsBlock("Happy Halloween!"), true, "Happy Halloween!");
    problems += blockProblem("spaces round them", wordsBlock("   Boo!  "), true, "Boo!");
    // Curly quotes and apostrophes, as phones type them
    problems += blockProblem("curly quotes", wordsBlock("It’s “spooky”…"), true, "It's \"spooky\"...");
    problems += blockProblem("accents", wordsBlock("Zoë’s pumpkin"), true, "Zoe's pumpkin");
    problems += blockProblem("emoji among words", wordsBlock("Boo \xF0\x9F\x8E\x83!"), true, "Boo !");
    problems += blockProblem("empty", wordsBlock(""), false, "");
    problems += blockProblem("only spaces", wordsBlock("    "), false, "");
    problems += blockProblem("only emoji", wordsBlock("\xF0\x9F\x8E\x83\xF0\x9F\x91\xBB"), false, "", "emoji");

    // Too long: trimmed to kMaxWordsLength
    const std::string longest(kMaxWordsLength, 'a');
    problems += blockProblem("kMaxWordsLength characters", wordsBlock(longest), true, longest);
    problems += blockProblem("too long", wordsBlock(longest + "bcdef"), true, longest);
    problems += blockProblem("too long, ending in spaces", wordsBlock(std::string(kMaxWordsLength - 1, 'a') + "   b"), true,
                             std::string(kMaxWordsLength - 1, 'a'));
    failIfAny(problems);
}

// ---- checkShow ----

static void test_check_show()
{
    std::string problems;
    std::string error;

    Show empty;
    if (checkShow(empty, error))
        problems += " an empty show was accepted;";

    Show one = {effectBlock("blink")};
    if (!checkShow(one, error))
        problems += " one block was turned away (" + error + ");";

    Show full(kMaxShowBlocks, effectBlock("hearts"));
    if (!checkShow(full, error))
        problems += " kMaxShowBlocks blocks were turned away (" + error + ");";

    Show tooMany(kMaxShowBlocks + 1, effectBlock("hearts"));
    error.clear();
    if (checkShow(tooMany, error))
        problems += " kMaxShowBlocks + 1 blocks were accepted;";
    else if (error.find(std::to_string(kMaxShowBlocks)) == std::string::npos)
        problems += " the too-many-blocks reason \"" + error + "\" should give the limit;";

    // The bad block is named (counting from 1)
    Show badThird = {effectBlock("pumpkin"), wordsBlock("Hello"), effectBlock("surprise"), effectBlock("bat")};
    error.clear();
    if (checkShow(badThird, error))
        problems += " a show with the surprise show in it was accepted;";
    else if (error.rfind("Block 3: ", 0) != 0)
        problems += " the reason \"" + error + "\" should start with \"Block 3: \";";

    Show emptyWords = {wordsBlock("\xF0\x9F\x8E\x83")};
    error.clear();
    if (checkShow(emptyWords, error))
        problems += " words with nothing the eyes can show were accepted;";

    // Words are tidied in place: two words blocks with different messages
    Show tidy = {wordsBlock("  Trick ‘or’ treat  "), effectBlock("witch"), wordsBlock("Happy Halloween!")};
    if (!checkShow(tidy, error))
        problems += " a show of words and an effect was turned away (" + error + ");";
    else if (tidy[0].value != "Trick 'or' treat" || tidy[2].value != "Happy Halloween!" || tidy[1].value != "witch")
        problems += " words weren't tidied in place (got \"" + tidy[0].value + "\");";

    failIfAny(problems.empty() ? "" : "checkShow:" + problems);
}

// ---- playBlock ----

static std::vector<std::string> recorded(void (*play)(Engine &))
{
    RecordingEngine engine;
    engine.eventLimit = 50000;
    play(engine);
    return engine.events;
}

static std::vector<std::string> playedAsBlock(const ShowBlock &block)
{
    RecordingEngine engine;
    engine.eventLimit = 50000;
    playBlock(engine, block);
    return engine.events;
}

// An effect block says the effect's phrase, then plays it exactly as the effect does
static void test_play_effect_block()
{
    const EffectInfo *hearts = findEffect("hearts");
    TEST_ASSERT_NOT_NULL(hearts);
    TEST_ASSERT_NOT_NULL(hearts->phrase);
    std::vector<std::string> expected = recorded(hearts->run);
    expected.insert(expected.begin(), std::string("S ") + hearts->phrase);
    const std::vector<std::string> actual = playedAsBlock(effectBlock("hearts"));
    TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
    for (size_t i = 0; i < expected.size(); i++)
        TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), actual[i].c_str());

    // An effect with no phrase just plays (the creeper speaks for itself, in the middle)
    const EffectInfo *creeper = findEffect("creeper");
    TEST_ASSERT_NOT_NULL(creeper);
    TEST_ASSERT_NULL(creeper->phrase);
    const std::vector<std::string> plain = recorded(creeper->run);
    const std::vector<std::string> block = playedAsBlock(effectBlock("creeper"));
    TEST_ASSERT_TRUE(plain == block);
}

static void scrollHello(Engine &engine)
{
    scrollWords(engine, "Hello, it's me!");
}

// A words block says the words, then scrolls them as typed words do
static void test_play_words_block()
{
    std::vector<std::string> expected = recorded(scrollHello);
    expected.insert(expected.begin(), "S Hello, it's me!");
    const std::vector<std::string> actual = playedAsBlock(wordsBlock("Hello, it's me!"));
    TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
    for (size_t i = 0; i < expected.size(); i++)
        TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), actual[i].c_str());
}

// Blocks checkShow() would turn away play nothing (and never get stuck in the surprise show)
static void test_play_bad_blocks()
{
    TEST_ASSERT_EQUAL_size_t(0, playedAsBlock(effectBlock("surprise")).size());
    TEST_ASSERT_EQUAL_size_t(0, playedAsBlock(effectBlock("dragon")).size());
    TEST_ASSERT_EQUAL_size_t(0, playedAsBlock(effectBlock("")).size());
    TEST_ASSERT_EQUAL_size_t(0, playedAsBlock(wordsBlock("")).size());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_default_show_is_valid);
    RUN_TEST(test_can_be_in_show);
    RUN_TEST(test_check_block_effects);
    RUN_TEST(test_check_block_words);
    RUN_TEST(test_check_show);
    RUN_TEST(test_play_effect_block);
    RUN_TEST(test_play_words_block);
    RUN_TEST(test_play_bad_blocks);
    return UNITY_END();
}
