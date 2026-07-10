#ifndef AUDIO_RECORD_H
#define AUDIO_RECORD_H

#include <stdint.h>
#include "record_board_audio.h"

typedef struct
{
    uint32_t samples_per_channel;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t duration_seconds;
    uint32_t adpcm_bytes;
    uint32_t chunk_samples;
    uint32_t capture_abs_peak;
    uint32_t capture_mean_abs;
    uint32_t capture_nonzero;
    uint32_t capture_zero;
    uint32_t capture_clipped;
    uint32_t playback_abs_peak;
    uint32_t playback_mean_abs;
    uint32_t playback_nonzero;
    uint32_t playback_gain_percent;
    uint32_t playback_target_peak;
    uint32_t playback_max_gain_percent;
    uint32_t playback_volume_percent;
    uint32_t monitor_dac_volume_reg;
    uint32_t record_adc_volume_reg;
    uint32_t record_adc_scale_reg;
    uint32_t record_mic_path_reg;
    int capture_dc_estimate;
    int capture_min;
    int capture_max;
    uint32_t soft_variant_selected;
    int test_tone_result;
    int monitor_result;
    int last_result;
    int16_t first_samples[8];
    int16_t last_samples[8];
    uint8_t first_adpcm[8];
    uint8_t last_adpcm[8];
    board_audio_soft_i2s_variant_info_t soft_variants[BOARD_AUDIO_SOFT_I2S_VARIANT_COUNT];
    board_audio_debug_info_t record_init_board;
    board_audio_debug_info_t record_done_board;
    board_audio_debug_info_t playback_init_board;
    board_audio_debug_info_t playback_ready_board;
    board_audio_debug_info_t monitor_done_board;
    board_audio_debug_info_t playback_done_board;
} audio_record_debug_info_t;

int audio_record_demo_once(void);
const audio_record_debug_info_t *audio_record_get_debug_info(void);

#endif
