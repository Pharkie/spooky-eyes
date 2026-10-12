// Unit tests for the text rules in src/core/text.cpp: plain text for the eyes, web address names from
// pumpkins' names, and WiFi passwords. Each test lists every case that went wrong, with what came out
// and what should have.
//
//   pio test -e native -f test_text

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "core/constants.h"
#include "core/text.h"

void setUp()
{
}

void tearDown()
{
}

// ---- Helpers ----

// Text with anything unprintable shown as \xNN, for failure messages
static std::string shown(const std::string &s)
{
    std::string out;
    for (unsigned char c : s)
    {
        if (c >= 0x20 && c < 0x7f && c != '\\')
        {
            out += static_cast<char>(c);
        }
        else
        {
            char hex[5];
            std::snprintf(hex, sizeof(hex), "\\x%02x", c);
            out += hex;
        }
    }
    return out;
}

using TextFn = size_t (*)(const char *in, char *out, size_t outSize);

struct Case
{
    const char *in;
    const char *expected;
};

// Runs fn on `in` with a buffer of outSize bytes (inside a bigger one filled with 'Z'). Returns "" if it
// gave `expected`, NUL-terminated, returned its length and wrote nothing past outSize; else what's wrong.
static std::string check(const char *name, TextFn fn, const char *in, size_t outSize, const char *expected)
{
    char buffer[128];
    std::memset(buffer, 'Z', sizeof(buffer));
    const size_t n = fn(in, buffer, outSize);
    const std::string call = std::string(name) + "(\"" + shown(in) + "\", size " + std::to_string(outSize) + ")";
    for (size_t i = outSize; i < sizeof(buffer); i++)
        if (buffer[i] != 'Z')
            return " " + call + " wrote past the end of its buffer;";
    if (outSize == 0)
        return n == 0 ? "" : " " + call + " returned " + std::to_string(n) + ", expected 0;";
    const void *end = std::memchr(buffer, '\0', outSize);
    if (!end)
        return " " + call + " didn't NUL-terminate;";
    const std::string actual(buffer, static_cast<const char *>(end) - buffer);
    if (actual != expected)
        return " " + call + " gave \"" + shown(actual) + "\", expected \"" + shown(expected) + "\";";
    if (n != actual.size())
        return " " + call + " returned " + std::to_string(n) + " for \"" + shown(actual) + "\";";
    return "";
}

// Every case with a roomy buffer
static std::string checkAll(const char *name, TextFn fn, std::initializer_list<Case> cases)
{
    std::string failures;
    for (const Case &c : cases)
        failures += check(name, fn, c.in, 100, c.expected);
    return failures;
}

// Unity leaves a failing test with longjmp, so the message lives in a static and no std::string is in
// scope when it fails
#define FAIL_IF_ANY(problems)                          \
    do                                                 \
    {                                                  \
        static std::string failures_;                  \
        failures_ = (problems);                        \
        if (!failures_.empty())                        \
            TEST_FAIL_MESSAGE(failures_.c_str());      \
    } while (0)

// ---- text::plain() ----

static void test_plain_keeps_printable_ascii()
{
    FAIL_IF_ANY(checkAll("plain", text::plain,
                       {
                           {"Ghost's Pumpkin", "Ghost's Pumpkin"},
                           {"Hello Ghost! $5 ok?", "Hello Ghost! $5 ok?"},
                           {"!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~",
                            "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"},
                           {"", ""},
                       }));
}

// Phones type curly quotes and dashes; the font only has the straight ones
static void test_plain_quotes_and_dashes()
{
    FAIL_IF_ANY(checkAll("plain", text::plain,
                       {
                           {"Ghost\xe2\x80\x99s Pumpkin", "Ghost's Pumpkin"},  // U+2019 right single quote
                           {"\xe2\x80\x98" "Boo\xe2\x80\x99", "'Boo'"},       // U+2018
                           {"\xe2\x80\x9c" "Boo!\xe2\x80\x9d", "\"Boo!\""},    // U+201C, U+201D
                           {"Trick\xe2\x80\x93or\xe2\x80\x94treat", "Trick-or-treat"}, // en and em dash
                       }));
}

// Emoji and broken UTF-8 are dropped; accented letters lose their accents
static void test_plain_drops_other_characters()
{
    FAIL_IF_ANY(checkAll("plain", text::plain,
                       {
                           {"Boo!\xf0\x9f\x8e\x83", "Boo!"},
                           {"Ghost's \xf0\x9f\x8e\x83Pumpkin", "Ghost's Pumpkin"},
                           {"\xf0\x9f\x8e\x83 Boo \xf0\x9f\x8e\x83", "Boo"}, // then trimmed
                           {"\xf0\x9f\x8e\x83\xf0\x9f\x91\xbb", ""},
                           {"Zo\xc3\xab", "Zoe"},                 // ë
                           {"Ren\xc3\xa9" "e Stra\xc3\x9f" "e", "Renee Strasse"}, // é, ß
                           {"\xc3\x86sop \xc5\x81\xc3\xb3" "d\xc5\xba", "AEsop Lodz"},   // Æ, Ł, ó, ź
                           {"6\xc3\x97" "7", "67"},               // × isn't a letter: dropped
                           {"A\xff" "B", "AB"},      // a byte that can't start a character
                           {"Boo\xe2", "Boo"},       // a character cut short at the end...
                           {"Boo\xe2\x80", "Boo"},   // ...mustn't read past the NUL
                       }));
}

// Non-breaking spaces and control characters become spaces; spaces at either end go
static std::string plainSpacesProblems()
{
    std::string failures = checkAll("plain", text::plain,
                                    {
                                        {"Ghost's\xc2\xa0Pumpkin", "Ghost's Pumpkin"},
                                        {"a\tb\nc\rd", "a b c d"},
                                        {"a\x01" "b", "a b"},
                                        {"  Ghost  ", "Ghost"},
                                        {"\t Ghost \n", "Ghost"},
                                        {"\xc2\xa0" "Ghost\xc2\xa0", "Ghost"},
                                        {"   ", ""},
                                    });
    // DEL isn't printable either: a space or nothing, as long as it's gone
    char out[16];
    text::plain("A\x7f" "B", out, sizeof(out));
    if (std::strcmp(out, "A B") != 0 && std::strcmp(out, "AB") != 0)
        failures += " plain(\"A\\x7fB\") gave \"" + shown(out) + "\", expected \"A B\" or \"AB\";";
    return failures;
}

static void test_plain_spaces()
{
    FAIL_IF_ANY(plainSpacesProblems());
}

// Small buffers: cut short, always NUL-terminated, never written past
static std::string plainBufferProblems()
{
    std::string failures;
    failures += check("plain", text::plain, "Ghost's Pumpkin", 6, "Ghost");
    failures += check("plain", text::plain, "Ghost's Pumpkin", 16, "Ghost's Pumpkin");
    failures += check("plain", text::plain, "Ghost's Pumpkin", 15, "Ghost's Pumpki");
    failures += check("plain", text::plain, "Ghost's Pumpkin", 1, "");
    failures += check("plain", text::plain, "Ghost's Pumpkin", 0, "");
    // Room for two output characters, though the input has nine bytes
    failures += check("plain", text::plain, "\xe2\x80\x99\xe2\x80\x99\xe2\x80\x99", 3, "''");
    return failures;
}

static void test_plain_buffer_size()
{
    FAIL_IF_ANY(plainBufferProblems());
}

// ---- text::hostname() ----

static void test_hostname_names()
{
    FAIL_IF_ANY(checkAll("hostname", text::hostname,
                       {
                           {"Ghost's Pumpkin", "ghosts-pumpkin"},
                           {"Sam & Alex's Pumpkin", "sam-alexs-pumpkin"},
                           {"Jo Bloggs's Pumpkin", "jo-bloggss-pumpkin"},
                           {"Ruby's Pumpkin", "rubys-pumpkin"},
                           {"Ghost\xe2\x80\x99s Pumpkin", "ghosts-pumpkin"}, // curly apostrophe
                           {"Spooky Pumpkin", "spooky-pumpkin"},
                           {"PUMPKIN No. 2", "pumpkin-no-2"},
                           {"--Ghost--", "ghost"},
                           {"Ghost  &  Ghoul", "ghost-ghoul"},
                           {"Trick\xe2\x80\x94or\xe2\x80\x93Treat", "trick-or-treat"},
                           {"\xf0\x9f\x8e\x83 Ghost's Pumpkin \xf0\x9f\x8e\x83", "ghosts-pumpkin"},
                           {"Zo\xc3\xab's Pumpkin", "zoes-pumpkin"},      // accent folded, not a gap
                           {"Gro\xc3\x9f Pumpkin", "gross-pumpkin"},      // ß -> ss
                       }));
}

// Nothing usable left: "pumpkin"
static void test_hostname_fallback()
{
    FAIL_IF_ANY(checkAll("hostname", text::hostname,
                       {
                           {"!!!", "pumpkin"},
                           {"", "pumpkin"},
                           {"   ", "pumpkin"},
                           {"''", "pumpkin"},
                           {"\xf0\x9f\x8e\x83\xf0\x9f\x91\xbb", "pumpkin"},
                       }));
}

// At most kMaxHostnameLength (32) characters, and no hyphen left at the end after cutting
static void test_hostname_long_names()
{
    FAIL_IF_ANY(checkAll("hostname", text::hostname,
                       {
                           // Cut at 32 it would end "pumpkin-"
                           {"The Most Amazing Spooky Pumpkin In Town", "the-most-amazing-spooky-pumpkin"},
                           {"abcdefghijklmnopqrstuvwxyz0123456789", "abcdefghijklmnopqrstuvwxyz012345"},
                           {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
                       }));
}

// Small buffers: cut short (still no hyphen at the end), always NUL-terminated, never written past
static std::string hostnameBufferProblems()
{
    std::string failures;
    failures += check("hostname", text::hostname, "Ghost's Pumpkin", 8, "ghosts"); // not "ghosts-"
    failures += check("hostname", text::hostname, "Ghost's Pumpkin", 6, "ghost");
    failures += check("hostname", text::hostname, "Ghost's Pumpkin", 15, "ghosts-pumpkin");
    failures += check("hostname", text::hostname, "Ghost's Pumpkin", 1, "");
    failures += check("hostname", text::hostname, "Ghost's Pumpkin", 0, "");

    // The fallback in a small buffer: some of "pumpkin", safely
    char buffer[16];
    std::memset(buffer, 'Z', sizeof(buffer));
    text::hostname("!!!", buffer, 4);
    if (!std::memchr(buffer, '\0', 4) || buffer[4] != 'Z' || std::strncmp(buffer, "pumpkin", std::strlen(buffer)) != 0)
        failures += " hostname(\"!!!\", size 4) gave \"" + shown(std::string(buffer, 4)) + "\";";
    return failures;
}

static void test_hostname_buffer_size()
{
    FAIL_IF_ANY(hostnameBufferProblems());
}

// Whatever goes in, what comes out is a usable web address name
static std::string hostnameUsableProblems()
{
    std::string failures;
    for (const char *name : {"Ghost's Pumpkin", "-", "a-", "-a", "a--b", "\xe2\x80\x94\xe2\x80\x94", "\xc3\xa9t\xc3\xa9",
                             "\t\n", "x\xff", "A B C D E F G H I J K L M N O P Q R S T U V W X Y Z", "9 Lives"})
    {
        char out[64];
        text::hostname(name, out, sizeof(out));
        const size_t length = std::strlen(out);
        bool ok = length > 0 && length <= kMaxHostnameLength && out[0] != '-' && out[length - 1] != '-' &&
                  !std::strstr(out, "--");
        for (const char *c = out; *c; c++)
            ok = ok && ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-');
        if (!ok)
            failures += " hostname(\"" + shown(name) + "\") gave \"" + shown(out) + "\";";
    }
    return failures;
}

static void test_hostname_always_usable()
{
    FAIL_IF_ANY(hostnameUsableProblems());
}

// ---- text::validWifiPassword() ----

static std::string wifiPasswordProblems()
{
    struct PasswordCase
    {
        std::string password;
        bool valid;
        const char *what;
    };
    const std::string hex64 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const PasswordCase cases[] = {
        {"", true, "empty (an open network)"},
        {std::string(7, 'a'), false, "7 characters"},
        {std::string(8, 'a'), true, "8 characters"},
        {"pass word", true, "9 characters with a space"},
        {std::string(63, 'a'), true, "63 characters"},
        {hex64, true, "64 hex digits"},
        {"0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF", true, "64 capital hex digits"},
        {std::string(64, 'g'), false, "64 characters, not hex"},
        {hex64.substr(0, 63) + "x", false, "63 hex digits and an x"},
        {std::string(65, 'a'), false, "65 characters"},
        {hex64 + "0", false, "65 hex digits"},
    };
    std::string failures;
    for (const PasswordCase &c : cases)
        if (text::validWifiPassword(c.password.c_str()) != c.valid)
            failures += std::string(" ") + c.what + ": got " + (c.valid ? "invalid" : "valid") + ", expected " +
                        (c.valid ? "valid" : "invalid") + ";";
    return failures;
}

static void test_wifi_password()
{
    FAIL_IF_ANY(wifiPasswordProblems());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_plain_keeps_printable_ascii);
    RUN_TEST(test_plain_quotes_and_dashes);
    RUN_TEST(test_plain_drops_other_characters);
    RUN_TEST(test_plain_spaces);
    RUN_TEST(test_plain_buffer_size);
    RUN_TEST(test_hostname_names);
    RUN_TEST(test_hostname_fallback);
    RUN_TEST(test_hostname_long_names);
    RUN_TEST(test_hostname_buffer_size);
    RUN_TEST(test_hostname_always_usable);
    RUN_TEST(test_wifi_password);
    return UNITY_END();
}
