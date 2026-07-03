#include <string.h>

#include "../../src/board_audio.h"
#include "../test_stubs.h"

test_board_audio_state_t g_test_board_audio_state;

int board_audio_init(void)
{
    g_test_board_audio_state.board_audio_init_calls++;
    return g_test_board_audio_state.board_audio_init_result;
}

int board_audio_init_playback(void)
{
    g_test_board_audio_state.board_audio_init_calls++;
    return g_test_board_audio_state.board_audio_init_result;
}

int board_audio_init_record(void)
{
    g_test_board_audio_state.board_audio_init_calls++;
    return g_test_board_audio_state.board_audio_init_result;
}

int board_audio_play_test_tone(uint32_t duration_ms)
{
    (void)duration_ms;
    return 0;
}

int board_audio_play_pcm(const int16_t *pcm, uint32_t samples, uint32_t channels)
{
    (void)pcm;
    g_test_board_audio_state.board_audio_play_pcm_calls++;
    g_test_board_audio_state.last_pcm_samples = samples;
    g_test_board_audio_state.last_pcm_channels = channels;
    return g_test_board_audio_state.board_audio_play_pcm_result;
}

int board_audio_capture_pcm(int16_t *pcm, uint32_t samples, uint32_t channels)
{
    uint32_t i;

    g_test_board_audio_state.last_pcm_samples = samples;
    g_test_board_audio_state.last_pcm_channels = channels;
    if (pcm != 0)
    {
        for (i = 0; i < samples * channels; ++i)
            pcm[i] = (int16_t)i;
    }
    return 0;
}

const board_audio_debug_info_t *board_audio_get_debug_info(void)
{
    static board_audio_debug_info_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    return &dummy;
}