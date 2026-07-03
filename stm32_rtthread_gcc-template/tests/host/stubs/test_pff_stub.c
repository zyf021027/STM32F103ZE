#include <string.h>

#include "../../src/pff/pff.h"
#include "../test_stubs.h"

test_pff_state_t g_test_pff_state;

void test_stubs_reset(void)
{
    memset(&g_test_board_audio_state, 0, sizeof(g_test_board_audio_state));
    memset(&g_test_sdcard_state, 0, sizeof(g_test_sdcard_state));
    memset(&g_test_pff_state, 0, sizeof(g_test_pff_state));
    memset(&g_test_minimp3_state, 0, sizeof(g_test_minimp3_state));

    g_test_sdcard_state.last_error         = "sdcard stub error";
    g_test_sdcard_state.is_ready_result    = 1;
    g_test_sdcard_state.read_sector_result = 0;
    g_test_pff_state.mount_result          = FR_OK;
    g_test_pff_state.open_result           = FR_OK;
    g_test_pff_state.read_result           = FR_OK;
    g_test_pff_state.opendir_result        = FR_OK;
    g_test_pff_state.readdir_result        = FR_OK;
    g_test_minimp3_state.channels          = 2;
    g_test_minimp3_state.hz                = 48000;
}

FRESULT pf_mount(FATFS *fs)
{
    (void)fs;
    return g_test_pff_state.mount_result;
}

FRESULT pf_open(const char *path)
{
    (void)path;
    g_test_pff_state.read_offset = 0;
    return g_test_pff_state.open_result;
}

FRESULT pf_read(void *buff, UINT btr, UINT *br)
{
    unsigned int remaining;
    unsigned int to_copy;

    g_test_pff_state.read_calls++;
    if (g_test_pff_state.read_result != FR_OK) {
        if (br)
            *br = 0;
        return g_test_pff_state.read_result;
    }

    remaining = g_test_pff_state.read_data_size - g_test_pff_state.read_offset;
    to_copy   = btr;
    if (to_copy > remaining)
        to_copy = remaining;

    if (to_copy > 0 && buff != NULL && g_test_pff_state.read_data != NULL) {
        memcpy(buff, g_test_pff_state.read_data + g_test_pff_state.read_offset, to_copy);
    }

    g_test_pff_state.read_offset += to_copy;
    if (br)
        *br = to_copy;
    return FR_OK;
}

FRESULT pf_write(const void *buff, UINT btw, UINT *bw)
{
    (void)buff;
    (void)btw;
    (void)bw;
    return FR_DISK_ERR;
}

FRESULT pf_lseek(DWORD ofs)
{
    (void)ofs;
    return FR_DISK_ERR;
}

FRESULT pf_opendir(DIR *dj, const char *path)
{
    (void)dj;
    (void)path;
    g_test_pff_state.dir_index = 0;
    return g_test_pff_state.opendir_result;
}

FRESULT pf_readdir(DIR *dj, FILINFO *fno)
{
    const char *name;
    unsigned int i;

    (void)dj;

    if (g_test_pff_state.readdir_result != FR_OK)
        return g_test_pff_state.readdir_result;

    memset(fno, 0, sizeof(*fno));
    if (g_test_pff_state.dir_index >= g_test_pff_state.dir_count)
        return FR_OK;

    name = g_test_pff_state.dir_names[g_test_pff_state.dir_index];
    if (name != 0) {
        for (i = 0; name[i] != '\0' && i < (sizeof(fno->fname) - 1U); ++i)
            fno->fname[i] = name[i];
        fno->fname[i] = '\0';
    }
    fno->fattrib = g_test_pff_state.dir_attrs[g_test_pff_state.dir_index];
    g_test_pff_state.dir_index++;
    return FR_OK;
}
