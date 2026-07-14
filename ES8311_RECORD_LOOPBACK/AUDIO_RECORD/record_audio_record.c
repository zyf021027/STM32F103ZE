#include "audio_record.h"

#include <string.h>
#include <stdio.h>

#include "record_board_audio.h"

#define AUDIO_RECORD_DEMO_SECONDS 5U
#define AUDIO_RECORD_DEMO_CHANNELS 1U
#define AUDIO_RECORD_DEMO_RATE 16000U
#define AUDIO_RECORD_PLAYBACK_RATE AUDIO_RECORD_DEMO_RATE
#define AUDIO_RECORD_DEMO_SAMPLES (AUDIO_RECORD_DEMO_RATE * AUDIO_RECORD_DEMO_SECONDS)
#define AUDIO_RECORD_CHUNK_SAMPLES 128U
#define AUDIO_RECORD_CAPTURE_STEP_SAMPLES 1U
#define AUDIO_RECORD_ADPCM_BYTES ((AUDIO_RECORD_DEMO_SAMPLES + 1U) / 2U)
#define AUDIO_RECORD_TEST_TONE_MS 1000U
#define AUDIO_RECORD_MONITOR_MS 5000U
#define AUDIO_RECORD_ENABLE_ADC_DAC_MONITOR 0U
#define AUDIO_RECORD_PLAYBACK_TARGET_PEAK 12000U
#define AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE 256U
#define AUDIO_RECORD_PLAYBACK_MAX_GAIN_Q8 (2U * AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE)
#define AUDIO_RECORD_DC_BLOCK_SHIFT 8U
#define AUDIO_RECORD_CAPTURE_LPF_SHIFT 2U
#define AUDIO_RECORD_CAPTURE_LPF_ENABLE 0U

static const int g_adpcm_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8};

static const int g_adpcm_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

typedef struct
{
    int predictor;
    int step_index;
} audio_adpcm_state_t;

static uint8_t g_record_adpcm[AUDIO_RECORD_ADPCM_BYTES];
static int16_t g_record_pcm_chunk[AUDIO_RECORD_CHUNK_SAMPLES];
static audio_record_debug_info_t g_record_debug;
static uint32_t g_capture_abs_sum;
static uint32_t g_playback_abs_sum;
static uint32_t g_record_playback_gain_q8 = AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE;
static int32_t g_record_dc_estimate_q8;
#if AUDIO_RECORD_CAPTURE_LPF_ENABLE
static int32_t g_record_lpf_state_q0;
#endif

static void audio_record_stage_log(const char *stage)
{
    const board_audio_debug_info_t *b = board_audio_get_debug_info();

    printf("[record-stage] %s mode=%d sr=%lu ch=%lu tx=%lu rx=%lu lpk=%lu rpk=%lu lnz=%lu rnz=%lu soft=%lu to=%lu ov=%lu r16=0x%02lX r17=0x%02lX r44=0x%02lX err=%d\r\n",
           stage,
           b->last_mode,
           (unsigned long)b->last_sample_rate,
           (unsigned long)b->last_channels,
           (unsigned long)b->tx_frames,
           (unsigned long)b->rx_frames,
           (unsigned long)b->rx_left_abs_peak,
           (unsigned long)b->rx_right_abs_peak,
           (unsigned long)b->rx_left_nonzero,
           (unsigned long)b->rx_right_nonzero,
           (unsigned long)b->rx_soft_frames,
           (unsigned long)b->rx_soft_bclk_timeouts,
           (unsigned long)b->rx_overruns,
           (unsigned long)b->codec_reg16,
           (unsigned long)b->codec_reg17,
           (unsigned long)b->codec_reg44,
           b->last_error);
}

static int audio_record_clamp_int16(int value)
{
    if (value > 32767)
        return 32767;
    if (value < -32768)
        return -32768;
    return value;
}

static uint32_t audio_record_abs16(int sample)
{
    if (sample < 0)
        return (sample <= -32768) ? 32768U : (uint32_t)(-sample);
    return (uint32_t)sample;
}

static void audio_record_reset_stats(void)
{
    g_capture_abs_sum = 0U;
    g_playback_abs_sum = 0U;
    g_record_playback_gain_q8 = AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE;
    g_record_dc_estimate_q8 = 0;
#if AUDIO_RECORD_CAPTURE_LPF_ENABLE
    g_record_lpf_state_q0 = 0;
#endif
    g_record_debug.capture_min = 32767;
    g_record_debug.playback_gain_percent = 100U;
    g_record_debug.capture_max = -32768;
    memset(g_record_debug.first_samples, 0, sizeof(g_record_debug.first_samples));
    memset(g_record_debug.last_samples, 0, sizeof(g_record_debug.last_samples));
    memset(g_record_debug.first_adpcm, 0, sizeof(g_record_debug.first_adpcm));
    memset(g_record_debug.last_adpcm, 0, sizeof(g_record_debug.last_adpcm));
}

static void audio_record_update_capture_stats(uint32_t sample_index, int16_t sample)
{
    uint32_t abs_value = audio_record_abs16(sample);

    if ((int)sample < g_record_debug.capture_min)
        g_record_debug.capture_min = sample;
    if ((int)sample > g_record_debug.capture_max)
        g_record_debug.capture_max = sample;
    if (abs_value > g_record_debug.capture_abs_peak)
        g_record_debug.capture_abs_peak = abs_value;
    if (sample == 0)
        g_record_debug.capture_zero++;
    else
        g_record_debug.capture_nonzero++;
    if (sample >= 32760 || sample <= -32760)
        g_record_debug.capture_clipped++;
    if (sample_index < 8U)
        g_record_debug.first_samples[sample_index] = sample;
    if (sample_index >= (AUDIO_RECORD_DEMO_SAMPLES - 8U))
        g_record_debug.last_samples[sample_index - (AUDIO_RECORD_DEMO_SAMPLES - 8U)] = sample;
    g_capture_abs_sum += abs_value;
}

static void audio_record_update_playback_stats(int16_t sample)
{
    uint32_t abs_value = audio_record_abs16(sample);

    if (abs_value > g_record_debug.playback_abs_peak)
        g_record_debug.playback_abs_peak = abs_value;
    if (sample != 0)
        g_record_debug.playback_nonzero++;
    g_playback_abs_sum += abs_value;
}

static void audio_record_finish_stats(void)
{
    uint32_t i;

    if (AUDIO_RECORD_DEMO_SAMPLES > 0U)
    {
        g_record_debug.capture_mean_abs = g_capture_abs_sum / AUDIO_RECORD_DEMO_SAMPLES;
        g_record_debug.playback_mean_abs = g_playback_abs_sum / AUDIO_RECORD_DEMO_SAMPLES;
    }

    for (i = 0U; i < 8U; ++i)
    {
        g_record_debug.first_adpcm[i] = g_record_adpcm[i];
        g_record_debug.last_adpcm[i] = g_record_adpcm[AUDIO_RECORD_ADPCM_BYTES - 8U + i];
    }
}

static uint8_t audio_record_adpcm_encode_sample(audio_adpcm_state_t *state, int16_t sample)
{
    int step = g_adpcm_step_table[state->step_index];
    int diff = (int)sample - state->predictor;
    int code = 0;
    int delta = step >> 3;

    if (diff < 0)
    {
        code = 8;
        diff = -diff;
    }
    if (diff >= step)
    {
        code |= 4;
        diff -= step;
        delta += step;
    }
    if (diff >= (step >> 1))
    {
        code |= 2;
        diff -= step >> 1;
        delta += step >> 1;
    }
    if (diff >= (step >> 2))
    {
        code |= 1;
        delta += step >> 2;
    }

    if (code & 8)
        state->predictor -= delta;
    else
        state->predictor += delta;

    state->predictor = audio_record_clamp_int16(state->predictor);
    state->step_index += g_adpcm_index_table[code & 0x0F];
    if (state->step_index < 0)
        state->step_index = 0;
    if (state->step_index > 88)
        state->step_index = 88;

    return (uint8_t)(code & 0x0F);
}

static int16_t audio_record_adpcm_decode_sample(audio_adpcm_state_t *state, uint8_t code)
{
    int step = g_adpcm_step_table[state->step_index];
    int delta = step >> 3;

    if (code & 4)
        delta += step;
    if (code & 2)
        delta += step >> 1;
    if (code & 1)
        delta += step >> 2;

    if (code & 8)
        state->predictor -= delta;
    else
        state->predictor += delta;

    state->predictor = audio_record_clamp_int16(state->predictor);
    state->step_index += g_adpcm_index_table[code & 0x0F];
    if (state->step_index < 0)
        state->step_index = 0;
    if (state->step_index > 88)
        state->step_index = 88;

    return (int16_t)state->predictor;
}

static void audio_record_store_nibble(uint32_t sample_index, uint8_t code)
{
    uint32_t byte_index = sample_index >> 1;

    if ((sample_index & 1U) == 0U)
        g_record_adpcm[byte_index] = code;
    else
        g_record_adpcm[byte_index] |= (uint8_t)(code << 4);
}

static uint8_t audio_record_load_nibble(uint32_t sample_index)
{
    uint8_t value = g_record_adpcm[sample_index >> 1];

    if ((sample_index & 1U) == 0U)
        return (uint8_t)(value & 0x0FU);
    return (uint8_t)(value >> 4);
}

static int16_t audio_record_remove_dc(int16_t sample, uint32_t sample_index)
{
    int32_t sample_q8 = (int32_t)sample << AUDIO_RECORD_DC_BLOCK_SHIFT;
    int32_t filtered;

    if (sample_index == 0U)
        g_record_dc_estimate_q8 = sample_q8;
    else
        g_record_dc_estimate_q8 += (sample_q8 - g_record_dc_estimate_q8) >> AUDIO_RECORD_DC_BLOCK_SHIFT;

    filtered = (int32_t)sample - (g_record_dc_estimate_q8 >> AUDIO_RECORD_DC_BLOCK_SHIFT);
    g_record_debug.capture_dc_estimate = (int)(g_record_dc_estimate_q8 >> AUDIO_RECORD_DC_BLOCK_SHIFT);
    return (int16_t)audio_record_clamp_int16(filtered);
}

#if AUDIO_RECORD_CAPTURE_LPF_ENABLE
static int16_t audio_record_low_pass(int16_t sample, uint32_t sample_index)
{
    if (sample_index == 0U)
        g_record_lpf_state_q0 = sample;
    else
        g_record_lpf_state_q0 += ((int32_t)sample - g_record_lpf_state_q0) >> AUDIO_RECORD_CAPTURE_LPF_SHIFT;

    return (int16_t)audio_record_clamp_int16((int)g_record_lpf_state_q0);
}
#endif
static int audio_record_capture_adpcm(void)
{
    audio_adpcm_state_t state = {0, 0};
    uint32_t done;

    memset(g_record_adpcm, 0, sizeof(g_record_adpcm));

    for (done = 0U; done < AUDIO_RECORD_DEMO_SAMPLES; ++done)
    {
        if (board_audio_capture_mono_sample(&g_record_pcm_chunk[0]) != 0)
            return -1;

        g_record_pcm_chunk[0] = audio_record_remove_dc(g_record_pcm_chunk[0], done);
#if AUDIO_RECORD_CAPTURE_LPF_ENABLE
        g_record_pcm_chunk[0] = audio_record_low_pass(g_record_pcm_chunk[0], done);
#endif
        audio_record_update_capture_stats(done, g_record_pcm_chunk[0]);
        audio_record_store_nibble(done, audio_record_adpcm_encode_sample(&state, g_record_pcm_chunk[0]));
    }

    g_record_debug.samples_per_channel = AUDIO_RECORD_DEMO_SAMPLES;
    g_record_debug.capture_mean_abs = g_capture_abs_sum / AUDIO_RECORD_DEMO_SAMPLES;
    return 0;
}

static void audio_record_prepare_playback_gain(void)
{
    uint32_t peak = g_record_debug.capture_abs_peak;
    uint32_t gain_q8 = AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE;

    if (peak > 0U && peak < AUDIO_RECORD_PLAYBACK_TARGET_PEAK)
    {
        gain_q8 = (uint32_t)(((uint64_t)AUDIO_RECORD_PLAYBACK_TARGET_PEAK * AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE + (peak / 2U)) / peak);
        if (gain_q8 < AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE)
            gain_q8 = AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE;
        if (gain_q8 > AUDIO_RECORD_PLAYBACK_MAX_GAIN_Q8)
            gain_q8 = AUDIO_RECORD_PLAYBACK_MAX_GAIN_Q8;
    }

    g_record_playback_gain_q8 = gain_q8;
    g_record_debug.playback_gain_percent = (gain_q8 * 100U + (AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE / 2U)) / AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE;
}

static int16_t audio_record_apply_playback_gain(int16_t sample)
{
    int32_t value = (int32_t)sample * (int32_t)g_record_playback_gain_q8;

    if (value >= 0)
        value = (value + (int32_t)(AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE / 2U)) / (int32_t)AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE;
    else
        value = -((-value + (int32_t)(AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE / 2U)) / (int32_t)AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE);

    if (value > 32767)
        return 32767;
    if (value < -32768)
        return -32768;
    return (int16_t)value;
}

static int audio_record_play_adpcm(void)
{
    audio_adpcm_state_t state = {0, 0};
    uint32_t done = 0U;

    while (done < AUDIO_RECORD_DEMO_SAMPLES)
    {
        uint32_t chunk = AUDIO_RECORD_DEMO_SAMPLES - done;
        uint32_t i;

        if (chunk > AUDIO_RECORD_CHUNK_SAMPLES)
            chunk = AUDIO_RECORD_CHUNK_SAMPLES;

        for (i = 0U; i < chunk; ++i)
        {
            int16_t decoded = audio_record_adpcm_decode_sample(&state, audio_record_load_nibble(done + i));
            g_record_pcm_chunk[i] = audio_record_apply_playback_gain(decoded);
            audio_record_update_playback_stats(g_record_pcm_chunk[i]);
        }

        if (board_audio_play_pcm(g_record_pcm_chunk, chunk, AUDIO_RECORD_DEMO_CHANNELS) != 0)
            return -1;

        done += chunk;
    }

    g_record_debug.playback_mean_abs = g_playback_abs_sum / AUDIO_RECORD_DEMO_SAMPLES;
    return 0;
}

int audio_record_demo_once(void)
{
    memset(&g_record_debug, 0, sizeof(g_record_debug));
    g_record_debug.channels = AUDIO_RECORD_DEMO_CHANNELS;
    g_record_debug.sample_rate = AUDIO_RECORD_DEMO_RATE;
    g_record_debug.duration_seconds = AUDIO_RECORD_DEMO_SECONDS;
    g_record_debug.adpcm_bytes = AUDIO_RECORD_ADPCM_BYTES;
    g_record_debug.chunk_samples = AUDIO_RECORD_CAPTURE_STEP_SAMPLES;
    g_record_debug.playback_target_peak = AUDIO_RECORD_PLAYBACK_TARGET_PEAK;
    g_record_debug.playback_max_gain_percent = (AUDIO_RECORD_PLAYBACK_MAX_GAIN_Q8 * 100U) / AUDIO_RECORD_PLAYBACK_GAIN_Q8_ONE;
    g_record_debug.playback_volume_percent = board_audio_get_playback_volume_percent();
    g_record_debug.monitor_dac_volume_reg = board_audio_get_monitor_dac_volume_reg();
    g_record_debug.record_adc_volume_reg = board_audio_get_record_adc_volume_reg();
    g_record_debug.record_adc_scale_reg = board_audio_get_record_adc_scale_reg();
    g_record_debug.record_mic_path_reg = board_audio_get_record_mic_path_reg();
    audio_record_reset_stats();

    printf("[record-stage] init-playback-for-tone start\r\n");
    if (board_audio_init_playback() != 0)
    {
        g_record_debug.last_result = -5;
        g_record_debug.playback_init_board = *board_audio_get_debug_info();
        audio_record_stage_log("init-playback-for-tone fail");
        return -5;
    }
    board_audio_set_sample_rate(AUDIO_RECORD_DEMO_RATE);
    printf("[record-stage] pre-tone start ms=%lu\r\n", (unsigned long)AUDIO_RECORD_TEST_TONE_MS);
    g_record_debug.test_tone_result = board_audio_play_test_tone(AUDIO_RECORD_TEST_TONE_MS);
    board_audio_drain();
    audio_record_stage_log("pre-tone done");

    printf("[record-stage] init-record start\r\n");
    if (board_audio_init_record_rate(AUDIO_RECORD_DEMO_RATE) != 0)
    {
        g_record_debug.last_result = -1;
        g_record_debug.record_init_board = *board_audio_get_debug_info();
        audio_record_stage_log("init-record fail");
        return -1;
    }

    board_audio_scan_i2s_pins(4096U);
    g_record_debug.record_init_board = *board_audio_get_debug_info();
    audio_record_stage_log("init-record ok");

#if AUDIO_RECORD_ENABLE_ADC_DAC_MONITOR
    printf("[record-stage] adc-dac-monitor start ms=%lu speak-now\r\n", (unsigned long)AUDIO_RECORD_MONITOR_MS);
    g_record_debug.monitor_result = board_audio_adc_dac_monitor(AUDIO_RECORD_MONITOR_MS);
    g_record_debug.monitor_result |= board_audio_disable_adc_dac_monitor();
    board_audio_scan_i2s_pins(4096U);
    g_record_debug.monitor_done_board = *board_audio_get_debug_info();
    audio_record_stage_log("adc-dac-monitor done");
#else
    printf("[record-stage] adc-dac-monitor skipped anti-feedback\r\n");
    g_record_debug.monitor_result = 0;
    g_record_debug.monitor_done_board = *board_audio_get_debug_info();
#endif

#if BOARD_AUDIO_RECORD_RX_BACKEND == BOARD_AUDIO_RECORD_RX_SOFTWARE_PB4
    board_audio_scan_i2s_pins(4096U);
    printf("[record-stage] soft-i2s probe start\r\n");
    (void)board_audio_probe_soft_i2s_variants(g_record_debug.soft_variants, BOARD_AUDIO_SOFT_I2S_VARIANT_COUNT);
    g_record_debug.soft_variant_selected = board_audio_get_soft_i2s_variant();
    printf("[record-stage] soft-i2s probe done selected=%lu\r\n", (unsigned long)g_record_debug.soft_variant_selected);
    if (g_record_debug.soft_variant_selected < BOARD_AUDIO_SOFT_I2S_VARIANT_COUNT)
    {
        const board_audio_soft_i2s_variant_info_t *v = &g_record_debug.soft_variants[g_record_debug.soft_variant_selected];
        printf("[record-stage] soft-i2s selected pin=PB%lu skip=%lu edge=%s score=%lu lpk=%lu rpk=%lu\r\n",
               (unsigned long)v->data_pin,
               (unsigned long)v->skip_edges,
               v->sample_falling ? "fall" : "rise",
               (unsigned long)v->score,
               (unsigned long)v->left_abs_peak,
               (unsigned long)v->right_abs_peak);
    }
#else
    printf("[record-stage] capture backend=SPI3_I2S_MASTER_RX pin=PB5\r\n");
#endif

    printf("[record-stage] capture start samples=%lu speak-now\r\n", (unsigned long)AUDIO_RECORD_DEMO_SAMPLES);
    if (audio_record_capture_adpcm() != 0)
    {
        g_record_debug.record_done_board = *board_audio_get_debug_info();
        g_record_debug.last_result = -2;
        audio_record_stage_log("capture fail");
        return -2;
    }

    g_record_debug.record_done_board = *board_audio_get_debug_info();
    audio_record_finish_stats();
    audio_record_prepare_playback_gain();
    printf("[record-stage] capture done peak=%lu mean=%lu min=%d max=%d gain_pct=%lu\r\n",
           (unsigned long)g_record_debug.capture_abs_peak,
           (unsigned long)g_record_debug.capture_mean_abs,
           g_record_debug.capture_min,
           g_record_debug.capture_max,
           (unsigned long)g_record_debug.playback_gain_percent);
    audio_record_stage_log("capture board");
    if (g_record_debug.capture_abs_peak < 256U)
        printf("[record-stage] mic_signal=TOO_LOW check-bias-polarity-routing\r\n");
    else if (g_record_debug.capture_abs_peak < 2000U)
        printf("[record-stage] mic_signal=LOW check-mic-distance-and-bias\r\n");
    else if (g_record_debug.capture_abs_peak > 30000U)
        printf("[record-stage] mic_signal=CLIPPING reduce-analog-gain\r\n");
    else
        printf("[record-stage] mic_signal=OK\r\n");

    printf("[record-stage] init-playback start\r\n");
    if (board_audio_init_playback() != 0)
    {
        g_record_debug.last_result = -3;
        g_record_debug.playback_init_board = *board_audio_get_debug_info();
        audio_record_stage_log("init-playback fail");
        return -3;
    }

    g_record_debug.playback_init_board = *board_audio_get_debug_info();
    audio_record_stage_log("init-playback ok");
    board_audio_set_sample_rate(AUDIO_RECORD_PLAYBACK_RATE);
    g_record_debug.playback_ready_board = *board_audio_get_debug_info();

    printf("[record-stage] playback expected record_ms=%lu playback_rate=%lu replay_ms=%lu total_ms=%lu frames=%lu\r\n",
           (unsigned long)(AUDIO_RECORD_DEMO_SECONDS * 1000U),
           (unsigned long)AUDIO_RECORD_PLAYBACK_RATE,
           (unsigned long)((AUDIO_RECORD_DEMO_SAMPLES * 1000U) / AUDIO_RECORD_PLAYBACK_RATE),
           (unsigned long)(((AUDIO_RECORD_DEMO_RATE / 4U) * 1000U) / AUDIO_RECORD_PLAYBACK_RATE + ((AUDIO_RECORD_DEMO_SAMPLES * 1000U) / AUDIO_RECORD_PLAYBACK_RATE)),
           (unsigned long)(AUDIO_RECORD_DEMO_SAMPLES + (AUDIO_RECORD_DEMO_RATE / 4U)));
    printf("[record-stage] playback-marker start\r\n");
    (void)board_audio_play_test_tone(250U);
    board_audio_drain();
    printf("[record-stage] playback-recorded start\r\n");
    if (audio_record_play_adpcm() != 0)
    {
        board_audio_drain();
        g_record_debug.playback_done_board = *board_audio_get_debug_info();
        board_audio_stop_playback();
        g_record_debug.last_result = -4;
        audio_record_stage_log("playback-recorded fail");
        return -4;
    }

    board_audio_drain();
    g_record_debug.playback_done_board = *board_audio_get_debug_info();
    board_audio_stop_playback();
    printf("[record-stage] playback-recorded stats peak=%lu mean=%lu nonzero=%lu gain_pct=%lu\r\n",
           (unsigned long)g_record_debug.playback_abs_peak,
           (unsigned long)g_record_debug.playback_mean_abs,
           (unsigned long)g_record_debug.playback_nonzero,
           (unsigned long)g_record_debug.playback_gain_percent);
    audio_record_stage_log("playback-recorded done");
    g_record_debug.last_result = 0;
    return 0;
}
const audio_record_debug_info_t *audio_record_get_debug_info(void)
{
    return &g_record_debug;
}
