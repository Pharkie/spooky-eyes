// Unit tests for when the WiFi switches off to save batteries (src/core/wifi_saver.h): only with the
// setting on, nothing under way and nobody using the pumpkin for kWifiIdleOffMs, including when millis()
// wraps round or someone was noted on another task just after "now" was read.
//
//   pio test -e native -f test_wifi_saver

#include <unity.h>

#include <cstdint>

#include "core/constants.h"
#include "core/wifi_saver.h"

void setUp()
{
}

void tearDown()
{
}

static bool offAt(uint32_t nowMs, uint32_t lastUsedMs, bool saverOn = true, bool busy = false)
{
    return wifi_saver::timeToSwitchOff(saverOn, busy, nowMs, lastUsedMs, kWifiIdleOffMs);
}

static void test_switches_off_after_the_idle_time()
{
    const uint32_t used = 5000;
    TEST_ASSERT_FALSE_MESSAGE(offAt(used, used), "just used");
    TEST_ASSERT_FALSE_MESSAGE(offAt(used + kWifiIdleOffMs - 1, used), "a millisecond early");
    TEST_ASSERT_TRUE_MESSAGE(offAt(used + kWifiIdleOffMs, used), "exactly on time");
    TEST_ASSERT_TRUE_MESSAGE(offAt(used + kWifiIdleOffMs * 3, used), "long after");
}

static void test_setting_off_keeps_wifi_on()
{
    TEST_ASSERT_FALSE(offAt(kWifiIdleOffMs * 2, 0, false));
}

static void test_busy_keeps_wifi_on()
{
    TEST_ASSERT_FALSE(offAt(kWifiIdleOffMs * 2, 0, true, true));
}

static void test_millis_wrapping_round()
{
    const uint32_t used = UINT32_MAX - 1000; // a second before millis() wraps round after 49 days
    TEST_ASSERT_FALSE_MESSAGE(offAt(used + 2000, used), "a second after it wrapped");
    TEST_ASSERT_FALSE_MESSAGE(offAt(used + kWifiIdleOffMs - 1, used), "a millisecond early, across the wrap");
    TEST_ASSERT_TRUE_MESSAGE(offAt(used + kWifiIdleOffMs, used), "on time, across the wrap");
}

static void test_use_noted_after_now_counts_as_just_now()
{
    // The web server noted a request between the network task reading millis() and reading the last use
    TEST_ASSERT_FALSE_MESSAGE(offAt(100000, 100005), "noted 5 ms after now");
    TEST_ASSERT_FALSE_MESSAGE(offAt(UINT32_MAX - 5, 4), "now just before millis() wraps round, noted just after");
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_switches_off_after_the_idle_time);
    RUN_TEST(test_setting_off_keeps_wifi_on);
    RUN_TEST(test_busy_keeps_wifi_on);
    RUN_TEST(test_millis_wrapping_round);
    RUN_TEST(test_use_noted_after_now_counts_as_just_now);
    return UNITY_END();
}
