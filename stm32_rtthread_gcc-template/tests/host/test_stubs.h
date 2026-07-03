#ifndef TEST_STUBS_H
#define TEST_STUBS_H

#include <stdint.h>

#include "../../src/pff/pff.h"
#include "../../third_party/minimp3/minimp3.h"

typedef struct
{
    int board_audio_init_result;
    int board_audio_play_pcm_result;
    int board_audio_init_calls;
    int board_audio_play_pcm_calls;
    uint32_t last_pcm_samples;
    uint32_t last_pcm_channels;
} test_board_audio_state_t;

typedef struct
{
    int init_result;
    int init_calls;
    const char *last_error;
    int is_ready_result;
    int read_sector_result;
    int read_sector_calls;
    uint32_t last_sector;
    unsigned char sector_data[512];
} test_sdcard_state_t;

typedef struct
{
    FRESULT mount_result;
    FRESULT open_result;
    FRESULT read_result;
    unsigned int read_calls;
    FRESULT opendir_result;
    FRESULT readdir_result;
    const char *dir_names[8];
    unsigned char dir_attrs[8];
    unsigned int dir_count;
    unsigned int dir_index;
    const unsigned char *read_data;
    unsigned int read_data_size;
    unsigned int read_offset;
} test_pff_state_t;

typedef struct
{
    int decode_result;
    int decode_calls;
    int frame_bytes;
    int channels;
    int hz;
    int scripted_results[8];
    int scripted_frame_bytes[8];
    int scripted_channels[8];
    int scripted_hz[8];
    int scripted_count;
} test_minimp3_state_t;

extern test_board_audio_state_t g_test_board_audio_state;
extern test_sdcard_state_t g_test_sdcard_state;
extern test_pff_state_t g_test_pff_state;
extern test_minimp3_state_t g_test_minimp3_state;

void test_stubs_reset(void);

#endif
