#include <string.h>

#include "../../third_party/minimp3/minimp3.h"
#include "../test_stubs.h"

test_minimp3_state_t g_test_minimp3_state;

void mp3dec_init(mp3dec_t *dec)
{
    memset(dec, 0, sizeof(*dec));
}

int mp3dec_decode_frame(mp3dec_t *dec, const uint8_t *mp3, int mp3_bytes, mp3d_sample_t *pcm, mp3dec_frame_info_t *info)
{
    int i;
    int index;
    int decode_result;
    int frame_bytes;
    int channels;
    int hz;

    (void)dec;
    (void)mp3;
    g_test_minimp3_state.decode_calls++;
    index = g_test_minimp3_state.decode_calls - 1;

    decode_result = g_test_minimp3_state.decode_result;
    frame_bytes = g_test_minimp3_state.frame_bytes > 0 ? g_test_minimp3_state.frame_bytes : mp3_bytes;
    channels = g_test_minimp3_state.channels;
    hz = g_test_minimp3_state.hz;

    if (index >= 0 && index < g_test_minimp3_state.scripted_count)
    {
        decode_result = g_test_minimp3_state.scripted_results[index];
        if (g_test_minimp3_state.scripted_frame_bytes[index] > 0)
            frame_bytes = g_test_minimp3_state.scripted_frame_bytes[index];
        if (g_test_minimp3_state.scripted_channels[index] > 0)
            channels = g_test_minimp3_state.scripted_channels[index];
        if (g_test_minimp3_state.scripted_hz[index] > 0)
            hz = g_test_minimp3_state.scripted_hz[index];
    }

    if (info)
    {
        info->frame_bytes = frame_bytes;
        info->frame_offset = 0;
        info->channels = channels;
        info->hz = hz;
        info->layer = 3;
        info->bitrate_kbps = 128;
    }

    if (pcm && decode_result > 0)
    {
        for (i = 0; i < decode_result * channels; ++i)
        {
            pcm[i] = (mp3d_sample_t)(i * 16);
        }
    }

    return decode_result;
}