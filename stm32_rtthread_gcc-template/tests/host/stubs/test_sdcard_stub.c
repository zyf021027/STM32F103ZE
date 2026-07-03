#include "../../src/sdcard_stub.h"
#include "../test_stubs.h"

test_sdcard_state_t g_test_sdcard_state;

int sdcard_stub_init(void)
{
    g_test_sdcard_state.init_calls++;
    return g_test_sdcard_state.init_result;
}

int sdcard_stub_is_ready(void)
{
    return g_test_sdcard_state.is_ready_result;
}

int sdcard_stub_read_sector(uint32_t sector, uint8_t *buffer)
{
    unsigned int i;

    g_test_sdcard_state.read_sector_calls++;
    g_test_sdcard_state.last_sector = sector;
    if (buffer != 0)
    {
        for (i = 0; i < 512U; ++i)
            buffer[i] = g_test_sdcard_state.sector_data[i];
    }
    return g_test_sdcard_state.read_sector_result;
}

const char *sdcard_stub_last_error_string(void)
{
    return g_test_sdcard_state.last_error ? g_test_sdcard_state.last_error : "sdcard stub error";
}