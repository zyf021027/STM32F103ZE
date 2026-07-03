#include "test_common.h"
#include "test_stubs.h"

#include "../../src/audio_player.h"

static void test_init_success(void)
{
    int result;

    test_stubs_reset();

    result = audio_player_init();
    TEST_ASSERT_INT_EQ(0, result);
    TEST_ASSERT_STR_EQ("ok", audio_player_last_error());
    TEST_ASSERT_INT_EQ(1, g_test_board_audio_state.board_audio_init_calls);
    TEST_ASSERT_INT_EQ(1, g_test_sdcard_state.init_calls);
}

static void test_init_board_audio_fail(void)
{
    int result;

    test_stubs_reset();
    g_test_board_audio_state.board_audio_init_result = -11;

    result = audio_player_init();
    TEST_ASSERT_INT_EQ(-1, result);
    TEST_ASSERT_STR_EQ("board audio init failed", audio_player_last_error());
}

static void test_init_sdcard_fail(void)
{
    int result;

    test_stubs_reset();
    g_test_sdcard_state.init_result = -2;
    g_test_sdcard_state.last_error  = "sd init timeout";

    result = audio_player_init();
    TEST_ASSERT_INT_EQ(-2, result);
    TEST_ASSERT_STR_EQ("sd init timeout", audio_player_last_error());
}

static void test_play_open_fail(void)
{
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.open_result = FR_NO_FILE;

    result = audio_player_play_mp3_from_sd("test.mp3");
    TEST_ASSERT_INT_EQ(-1, result);
    TEST_ASSERT_STR_EQ("pf_open failed", audio_player_last_error());
}

static void test_play_read_fail(void)
{
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.read_result = FR_DISK_ERR;

    result = audio_player_play_mp3_from_sd("test.mp3");
    TEST_ASSERT_INT_EQ(-2, result);
    TEST_ASSERT_STR_EQ("pf_read failed", audio_player_last_error());
}

static void test_play_empty_file(void)
{
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.read_data      = 0;
    g_test_pff_state.read_data_size = 0;

    result = audio_player_play_mp3_from_sd("empty.mp3");
    TEST_ASSERT_INT_EQ(-6, result);
    TEST_ASSERT_STR_EQ("mp3 file is empty", audio_player_last_error());
}

static void test_play_no_valid_frame(void)
{
    static const unsigned char garbage[] = {0x00, 0x01, 0x02, 0x03};
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.read_data                   = garbage;
    g_test_pff_state.read_data_size              = sizeof(garbage);
    g_test_minimp3_state.scripted_count          = 1;
    g_test_minimp3_state.scripted_results[0]     = 0;
    g_test_minimp3_state.scripted_frame_bytes[0] = 0;

    result = audio_player_play_mp3_from_sd("garbage.mp3");
    TEST_ASSERT_INT_EQ(-7, result);
    TEST_ASSERT_STR_EQ("no valid mp3 frame decoded", audio_player_last_error());
}

static void test_play_decode_and_output_success(void)
{
    static const unsigned char fake_mp3[] = {
        0xFF, 0xFB, 0x90, 0x64, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77};
    const audio_player_stats_t *stats;
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.read_data         = fake_mp3;
    g_test_pff_state.read_data_size    = sizeof(fake_mp3);
    g_test_minimp3_state.decode_result = 4;
    g_test_minimp3_state.frame_bytes   = (int)sizeof(fake_mp3);
    g_test_minimp3_state.channels      = 2;
    g_test_minimp3_state.hz            = 48000;

    result = audio_player_play_mp3_from_sd("test.mp3");
    TEST_ASSERT_INT_EQ(0, result);
    TEST_ASSERT_STR_EQ("mp3 playback done", audio_player_last_error());
    TEST_ASSERT_INT_EQ(1, g_test_board_audio_state.board_audio_play_pcm_calls);
    TEST_ASSERT_INT_EQ(4, (int)g_test_board_audio_state.last_pcm_samples);
    TEST_ASSERT_INT_EQ(2, (int)g_test_board_audio_state.last_pcm_channels);
    TEST_ASSERT_TRUE(g_test_minimp3_state.decode_calls >= 1);

    stats = audio_player_get_stats();
    TEST_ASSERT_INT_EQ((int)sizeof(fake_mp3), (int)stats->bytes_read);
    TEST_ASSERT_INT_EQ(1, (int)stats->frames_decoded);
    TEST_ASSERT_INT_EQ(1, (int)stats->pcm_blocks_sent);
    TEST_ASSERT_INT_EQ(48000, (int)stats->last_sample_rate);
    TEST_ASSERT_INT_EQ(2, (int)stats->last_channels);
}

static void test_play_pcm_output_fail(void)
{
    static const unsigned char fake_mp3[] = {
        0xFF, 0xFB, 0x90, 0x64, 0xAA, 0xBB, 0xCC, 0xDD};
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.read_data                           = fake_mp3;
    g_test_pff_state.read_data_size                      = sizeof(fake_mp3);
    g_test_minimp3_state.decode_result                   = 8;
    g_test_minimp3_state.frame_bytes                     = (int)sizeof(fake_mp3);
    g_test_minimp3_state.channels                        = 2;
    g_test_board_audio_state.board_audio_play_pcm_result = -1;

    result = audio_player_play_mp3_from_sd("test.mp3");
    TEST_ASSERT_INT_EQ(-4, result);
    TEST_ASSERT_STR_EQ("pcm output failed", audio_player_last_error());
}

static void test_play_skips_invalid_prefix_then_decodes(void)
{
    static const unsigned char fake_mp3[] = {
        0x00, 0x00, 0x00, 0xFF, 0xFB, 0x90, 0x64, 0x12,
        0x34, 0x56, 0x78, 0x9A};
    const audio_player_stats_t *stats;
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.read_data                   = fake_mp3;
    g_test_pff_state.read_data_size              = sizeof(fake_mp3);
    g_test_minimp3_state.scripted_count          = 2;
    g_test_minimp3_state.scripted_results[0]     = 0;
    g_test_minimp3_state.scripted_frame_bytes[0] = 3;
    g_test_minimp3_state.scripted_results[1]     = 4;
    g_test_minimp3_state.scripted_frame_bytes[1] = (int)(sizeof(fake_mp3) - 3U);
    g_test_minimp3_state.scripted_channels[1]    = 2;
    g_test_minimp3_state.scripted_hz[1]          = 48000;

    result = audio_player_play_mp3_from_sd("test.mp3");
    TEST_ASSERT_INT_EQ(0, result);
    TEST_ASSERT_INT_EQ(2, g_test_minimp3_state.decode_calls);
    TEST_ASSERT_INT_EQ(1, g_test_board_audio_state.board_audio_play_pcm_calls);

    stats = audio_player_get_stats();
    TEST_ASSERT_INT_EQ(1, (int)stats->frames_skipped);
    TEST_ASSERT_INT_EQ(1, (int)stats->frames_decoded);
}

static void test_play_rejects_invalid_channel_count(void)
{
    static const unsigned char fake_mp3[] = {
        0xFF, 0xFB, 0x90, 0x64, 0x10, 0x20, 0x30, 0x40};
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.read_data         = fake_mp3;
    g_test_pff_state.read_data_size    = sizeof(fake_mp3);
    g_test_minimp3_state.decode_result = 4;
    g_test_minimp3_state.frame_bytes   = (int)sizeof(fake_mp3);
    g_test_minimp3_state.channels      = 3;

    result = audio_player_play_mp3_from_sd("test.mp3");
    TEST_ASSERT_INT_EQ(-3, result);
    TEST_ASSERT_STR_EQ("unsupported channel count", audio_player_last_error());
}

static void test_play_rejects_invalid_frame_size(void)
{
    static const unsigned char fake_mp3[] = {
        0xFF, 0xFB, 0x90, 0x64, 0x01, 0x02, 0x03, 0x04};
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.read_data         = fake_mp3;
    g_test_pff_state.read_data_size    = sizeof(fake_mp3);
    g_test_minimp3_state.decode_result = 4;
    g_test_minimp3_state.frame_bytes   = (int)sizeof(fake_mp3) + 10;
    g_test_minimp3_state.channels      = 2;

    result = audio_player_play_mp3_from_sd("test.mp3");
    TEST_ASSERT_INT_EQ(-5, result);
    TEST_ASSERT_STR_EQ("mp3 frame size invalid", audio_player_last_error());
}

static void test_play_handles_multiple_chunk_reads(void)
{
    static const unsigned char fake_mp3[] = {
        0xFF, 0xFB, 0x90, 0x64, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};
    const audio_player_stats_t *stats;
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.read_data                   = fake_mp3;
    g_test_pff_state.read_data_size              = sizeof(fake_mp3);
    g_test_minimp3_state.scripted_count          = 2;
    g_test_minimp3_state.scripted_results[0]     = 4;
    g_test_minimp3_state.scripted_frame_bytes[0] = 8;
    g_test_minimp3_state.scripted_channels[0]    = 2;
    g_test_minimp3_state.scripted_hz[0]          = 48000;
    g_test_minimp3_state.scripted_results[1]     = 4;
    g_test_minimp3_state.scripted_frame_bytes[1] = 8;
    g_test_minimp3_state.scripted_channels[1]    = 2;
    g_test_minimp3_state.scripted_hz[1]          = 48000;

    result = audio_player_play_mp3_from_sd("test.mp3");
    TEST_ASSERT_INT_EQ(0, result);
    TEST_ASSERT_INT_EQ(2, g_test_board_audio_state.board_audio_play_pcm_calls);
    TEST_ASSERT_TRUE(g_test_pff_state.read_calls >= 1);

    stats = audio_player_get_stats();
    TEST_ASSERT_INT_EQ(2, (int)stats->frames_decoded);
    TEST_ASSERT_INT_EQ(2, (int)stats->pcm_blocks_sent);
}

static void test_play_dispatches_mp3_by_extension(void)
{
    static const unsigned char fake_mp3[] = {
        0xFF, 0xFB, 0x90, 0x64, 0x00, 0x11, 0x22, 0x33};
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.read_data         = fake_mp3;
    g_test_pff_state.read_data_size    = sizeof(fake_mp3);
    g_test_minimp3_state.decode_result = 4;
    g_test_minimp3_state.frame_bytes   = (int)sizeof(fake_mp3);
    g_test_minimp3_state.channels      = 2;

    result = audio_player_play_from_sd("demo.MP3");
    TEST_ASSERT_INT_EQ(0, result);
    TEST_ASSERT_STR_EQ("mp3 playback done", audio_player_last_error());
}

static void test_find_first_mp3_skips_non_mp3(void)
{
    char path[13];
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.dir_names[0] = "README.TXT";
    g_test_pff_state.dir_names[1] = "SONG.MP3";
    g_test_pff_state.dir_count    = 2;

    result = audio_player_find_first_mp3(path, sizeof(path));
    TEST_ASSERT_INT_EQ(0, result);
    TEST_ASSERT_STR_EQ("SONG.MP3", path);
}

static void test_find_first_mp3_reports_missing(void)
{
    char path[13];
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());
    g_test_pff_state.dir_names[0] = "README.TXT";
    g_test_pff_state.dir_count    = 1;

    result = audio_player_find_first_mp3(path, sizeof(path));
    TEST_ASSERT_INT_EQ(-5, result);
    TEST_ASSERT_STR_EQ("no mp3 file found", audio_player_last_error());
}

static void test_play_reports_mp4_disabled(void)
{
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());

    result = audio_player_play_from_sd("demo.mp4");
    TEST_ASSERT_INT_EQ(-21, result);
    TEST_ASSERT_STR_EQ("mp4/m4a support disabled", audio_player_last_error());
}

static void test_play_reports_unknown_extension(void)
{
    int result;

    test_stubs_reset();
    TEST_ASSERT_INT_EQ(0, audio_player_init());

    result = audio_player_play_from_sd("demo.wav");
    TEST_ASSERT_INT_EQ(-22, result);
    TEST_ASSERT_STR_EQ("unsupported file extension", audio_player_last_error());
}

int main(void)
{
    test_init_success();
    test_init_board_audio_fail();
    test_init_sdcard_fail();
    test_play_open_fail();
    test_play_read_fail();
    test_play_empty_file();
    test_play_no_valid_frame();
    test_play_decode_and_output_success();
    test_play_pcm_output_fail();
    test_play_skips_invalid_prefix_then_decodes();
    test_play_rejects_invalid_channel_count();
    test_play_rejects_invalid_frame_size();
    test_play_handles_multiple_chunk_reads();
    test_play_dispatches_mp3_by_extension();
    test_find_first_mp3_skips_non_mp3();
    test_find_first_mp3_reports_missing();
    test_play_reports_mp4_disabled();
    test_play_reports_unknown_extension();

    printf("host_audio_player_tests: all tests passed\n");
    return 0;
}
