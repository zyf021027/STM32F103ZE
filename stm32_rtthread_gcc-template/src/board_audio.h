#ifndef BOARD_AUDIO_H
#define BOARD_AUDIO_H

#include <stdint.h>

typedef struct
{
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t last_sample_rate;
    uint32_t last_channels;
    int last_error;
    int last_mode;
} board_audio_debug_info_t;

int board_audio_init(void);
int board_audio_init_playback(void);
int board_audio_init_record(void);
int board_audio_play_test_tone(uint32_t duration_ms);
int board_audio_play_pcm(const int16_t *pcm, uint32_t samples, uint32_t channels);
int board_audio_capture_pcm(int16_t *pcm, uint32_t samples, uint32_t channels);
const board_audio_debug_info_t *board_audio_get_debug_info(void);

#endif