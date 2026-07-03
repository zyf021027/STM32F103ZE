#include "test_common.h"

#include <stdint.h>
#include <string.h>

#include "../../src/board_audio.h"
#include "../../src/audio_record.h"

typedef struct
{
    int init_record_result;
    int capture_result;
    int init_playback_result;
    int play_result;
    int init_record_calls;
    int capture_calls;
    int init_playback_calls;
    int play_calls;
    uint32_t last_capture_samples;
    uint32_t last_capture_channels;
    uint32_t last_play_samples;
    uint32_t last_play_channels;
} record_stub_state_t;

static record_stub_state_t g_record_stub;

int board_audio_init(void)
{
    return 0;
}

int board_audio_init_playback(void)
{
    g_record_stub.init_playback_calls++;
    return g_record_stub.init_playback_result;
}

int board_audio_init_record(void)
{
    g_record_stub.init_record_calls++;
    return g_record_stub.init_record_result;
}

int board_audio_play_test_tone(uint32_t duration_ms)
{
    (void)duration_ms;
    return 0;
}

int board_audio_play_pcm(const int16_t *pcm, uint32_t samples, uint32_t channels)
{
    (void)pcm;
    g_record_stub.play_calls++;
    g_record_stub.last_play_samples = samples;
    g_record_stub.last_play_channels = channels;
    return g_record_stub.play_result;
}

int board_audio_capture_pcm(int16_t *pcm, uint32_t samples, uint32_t channels)
{
    uint32_t i;

    g_record_stub.capture_calls++;
    g_record_stub.last_capture_samples = samples;
    g_record_stub.last_capture_channels = channels;
    if (pcm != 0)
    {
        for (i = 0; i < samples * channels; ++i)
            pcm[i] = (int16_t)i;
    }
    return g_record_stub.capture_result;
}

const board_audio_debug_info_t *board_audio_get_debug_info(void)
{
    static board_audio_debug_info_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    return &dummy;
}

static void test_record_demo_success(void)
{
    const audio_record_debug_info_t *dbg;

    memset(&g_record_stub, 0, sizeof(g_record_stub));
    TEST_ASSERT_INT_EQ(0, audio_record_demo_once());
    dbg = audio_record_get_debug_info();
    TEST_ASSERT_INT_EQ(8000, (int)dbg->samples_per_channel);
    TEST_ASSERT_INT_EQ(1, (int)dbg->channels);
    TEST_ASSERT_INT_EQ(8000, (int)dbg->sample_rate);
    TEST_ASSERT_INT_EQ(1, g_record_stub.init_record_calls);
    TEST_ASSERT_INT_EQ(1, g_record_stub.capture_calls);
    TEST_ASSERT_INT_EQ(1, g_record_stub.init_playback_calls);
    TEST_ASSERT_INT_EQ(1, g_record_stub.play_calls);
}

static void test_record_demo_init_record_fail(void)
{
    memset(&g_record_stub, 0, sizeof(g_record_stub));
    g_record_stub.init_record_result = -1;
    TEST_ASSERT_INT_EQ(-1, audio_record_demo_once());
}

static void test_record_demo_capture_fail(void)
{
    memset(&g_record_stub, 0, sizeof(g_record_stub));
    g_record_stub.capture_result = -1;
    TEST_ASSERT_INT_EQ(-2, audio_record_demo_once());
}

static void test_record_demo_init_playback_fail(void)
{
    memset(&g_record_stub, 0, sizeof(g_record_stub));
    g_record_stub.init_playback_result = -1;
    TEST_ASSERT_INT_EQ(-3, audio_record_demo_once());
}

static void test_record_demo_play_fail(void)
{
    memset(&g_record_stub, 0, sizeof(g_record_stub));
    g_record_stub.play_result = -1;
    TEST_ASSERT_INT_EQ(-4, audio_record_demo_once());
}

int main(void)
{
    test_record_demo_success();
    test_record_demo_init_record_fail();
    test_record_demo_capture_fail();
    test_record_demo_init_playback_fail();
    test_record_demo_play_fail();
    printf("host_record_tests: all tests passed\n");
    return 0;
}