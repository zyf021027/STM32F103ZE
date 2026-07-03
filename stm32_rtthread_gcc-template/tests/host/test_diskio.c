#include "test_common.h"
#include "test_stubs.h"

#include "../../src/pff/diskio.h"

static void fill_sector_pattern(void)
{
    unsigned int i;

    for (i = 0; i < 512U; ++i)
        g_test_sdcard_state.sector_data[i] = (unsigned char)(i & 0xFFU);
}

static void test_disk_initialize_success(void)
{
    test_stubs_reset();
    g_test_sdcard_state.init_result = 0;

    TEST_ASSERT_INT_EQ(0, disk_initialize());
    TEST_ASSERT_INT_EQ(1, g_test_sdcard_state.init_calls);
}

static void test_disk_initialize_fail(void)
{
    test_stubs_reset();
    g_test_sdcard_state.init_result = -2;

    TEST_ASSERT_INT_EQ(STA_NOINIT, disk_initialize());
}

static void test_disk_readp_not_ready(void)
{
    unsigned char out[16];

    test_stubs_reset();
    g_test_sdcard_state.is_ready_result = 0;

    TEST_ASSERT_INT_EQ(RES_NOTRDY, disk_readp(out, 0, 0, sizeof(out)));
    TEST_ASSERT_INT_EQ(0, g_test_sdcard_state.read_sector_calls);
}

static void test_disk_readp_sector_read_fail(void)
{
    unsigned char out[16];

    test_stubs_reset();
    g_test_sdcard_state.read_sector_result = -5;

    TEST_ASSERT_INT_EQ(RES_ERROR, disk_readp(out, 3, 0, sizeof(out)));
    TEST_ASSERT_INT_EQ(1, g_test_sdcard_state.read_sector_calls);
    TEST_ASSERT_INT_EQ(3, (int)g_test_sdcard_state.last_sector);
}

static void test_disk_readp_parameter_error(void)
{
    unsigned char out[16];

    test_stubs_reset();

    TEST_ASSERT_INT_EQ(RES_PARERR, disk_readp(out, 1, 500, 20));
    TEST_ASSERT_INT_EQ(1, g_test_sdcard_state.read_sector_calls);
}

static void test_disk_readp_success_copy(void)
{
    unsigned char out[8];
    unsigned int i;

    test_stubs_reset();
    fill_sector_pattern();

    TEST_ASSERT_INT_EQ(RES_OK, disk_readp(out, 5, 10, sizeof(out)));
    TEST_ASSERT_INT_EQ(1, g_test_sdcard_state.read_sector_calls);
    TEST_ASSERT_INT_EQ(5, (int)g_test_sdcard_state.last_sector);

    for (i = 0; i < sizeof(out); ++i)
        TEST_ASSERT_INT_EQ(10 + (int)i, out[i]);
}

static void test_disk_readp_null_buffer_still_ok(void)
{
    test_stubs_reset();
    fill_sector_pattern();

    TEST_ASSERT_INT_EQ(RES_OK, disk_readp(0, 2, 4, 8));
    TEST_ASSERT_INT_EQ(1, g_test_sdcard_state.read_sector_calls);
}

static void test_disk_writep_is_not_supported(void)
{
    TEST_ASSERT_INT_EQ(RES_PARERR, disk_writep(0, 0));
}

int main(void)
{
    test_disk_initialize_success();
    test_disk_initialize_fail();
    test_disk_readp_not_ready();
    test_disk_readp_sector_read_fail();
    test_disk_readp_parameter_error();
    test_disk_readp_success_copy();
    test_disk_readp_null_buffer_still_ok();
    test_disk_writep_is_not_supported();

    printf("host_diskio_tests: all tests passed\n");
    return 0;
}