#include "audio_record.h"

#include <string.h>

#include "board_audio.h"

#define AUDIO_RECORD_DEMO_SAMPLES 8000U
#define AUDIO_RECORD_DEMO_CHANNELS 1U
#define AUDIO_RECORD_DEMO_RATE 8000U

static int16_t g_record_buffer[AUDIO_RECORD_DEMO_SAMPLES * AUDIO_RECORD_DEMO_CHANNELS];
static audio_record_debug_info_t g_record_debug;

int audio_record_demo_once(void)
{
    memset(&g_record_debug, 0, sizeof(g_record_debug));
    g_record_debug.channels = AUDIO_RECORD_DEMO_CHANNELS;
    g_record_debug.sample_rate = AUDIO_RECORD_DEMO_RATE;

    if (board_audio_init_record() != 0)
    {
        g_record_debug.last_result = -1;
        return -1;
    }

    if (board_audio_capture_pcm(g_record_buffer, AUDIO_RECORD_DEMO_SAMPLES, AUDIO_RECORD_DEMO_CHANNELS) != 0)
    {
        g_record_debug.last_result = -2;
        return -2;
    }

    g_record_debug.samples_per_channel = AUDIO_RECORD_DEMO_SAMPLES;

    if (board_audio_init_playback() != 0)
    {
        g_record_debug.last_result = -3;
        return -3;
    }

    if (board_audio_play_pcm(g_record_buffer, AUDIO_RECORD_DEMO_SAMPLES, AUDIO_RECORD_DEMO_CHANNELS) != 0)
    {
        g_record_debug.last_result = -4;
        return -4;
    }

    g_record_debug.last_result = 0;
    return 0;
}

const audio_record_debug_info_t *audio_record_get_debug_info(void)
{
    return &g_record_debug;
}
