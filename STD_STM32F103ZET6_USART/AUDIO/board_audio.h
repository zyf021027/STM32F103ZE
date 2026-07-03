#ifndef BOARD_AUDIO_H
#define BOARD_AUDIO_H

#include <stdint.h>

typedef struct
{
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t last_sample_rate;
    uint32_t last_channels;
    uint32_t codec_addr;
    uint32_t codec_fail_reg;
    uint32_t codec_reg32;
    uint32_t codec_reg31;
    uint32_t codec_reg33;
    uint32_t codec_reg34;
    uint32_t codec_reg0d;
    uint32_t codec_reg25;
    uint32_t codec_reg44;
    uint32_t spi3_sr;
    uint32_t spi3_i2scfgr;
    uint32_t gpio_amp_state;
    int last_error;
    int last_mode;
} board_audio_debug_info_t;

int board_audio_init(void);
int board_audio_init_playback(void);
int board_audio_init_record(void);
void board_audio_set_sample_rate(uint32_t sample_rate);
void board_audio_amp_set(int enabled);
void board_audio_amp_set_pin_high(int high);
int board_audio_play_test_tone(uint32_t duration_ms);
int board_audio_play_pcm(const int16_t *pcm, uint32_t samples, uint32_t channels);
int board_audio_capture_pcm(int16_t *pcm, uint32_t samples, uint32_t channels);
const board_audio_debug_info_t *board_audio_get_debug_info(void);

#endif
