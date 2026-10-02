#include <string.h>

#include "unity.h"

#include "code_review_graph/version.h"

void setUp(void) {}
void tearDown(void) {}

static void test_version_is_non_null_and_nul_terminated(void)
{
    const char *version = crg_c_core_version();

    TEST_ASSERT_NOT_NULL(version);
    TEST_ASSERT_EQUAL_UINT32(5U, (uint32_t)strlen(version));
}

static void test_version_reports_expected_prefix(void)
{
    const char *version = crg_c_core_version();

    TEST_ASSERT_NOT_NULL(version);
    TEST_ASSERT_EQUAL_CHAR('0', version[0]);
    TEST_ASSERT_EQUAL_CHAR('.', version[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_version_is_non_null_and_nul_terminated);
    RUN_TEST(test_version_reports_expected_prefix);
    return UNITY_END();
}
