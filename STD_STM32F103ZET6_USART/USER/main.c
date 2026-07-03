#include "stm32f10x.h"
#include <stdio.h>

#include "delay.h"
#include "usart.h"
#include "audio_player.h"
#include "audio_record.h"
#include "board_audio.h"
#include "sdcard_stub.h"

#define DEMO_FILE_PATH_BUFFER_SIZE 13U
#define ENABLE_RECORD_DEMO 1

static void dump_player_stats(void)
{
    const audio_player_stats_t *stats = audio_player_get_stats();

    printf("[mp3] bytes_read=%lu decode_calls=%lu frames_decoded=%lu frames_skipped=%lu pcm_blocks=%lu sr=%lu ch=%lu\r\n",
           (unsigned long)stats->bytes_read,
           (unsigned long)stats->decode_calls,
           (unsigned long)stats->frames_decoded,
           (unsigned long)stats->frames_skipped,
           (unsigned long)stats->pcm_blocks_sent,
           (unsigned long)stats->last_sample_rate,
           (unsigned long)stats->last_channels);
}

static void dump_board_audio_debug(const char *tag)
{
    const board_audio_debug_info_t *dbg = board_audio_get_debug_info();

    printf("[%s] mode=%d tx_frames=%lu rx_frames=%lu sr=%lu ch=%lu codec_addr=0x%02lX fail_reg=0x%02lX r31=0x%02lX r32=0x%02lX r33=0x%02lX r34=0x%02lX r0d=0x%02lX r25=0x%02lX r44=0x%02lX spi_sr=0x%04lX i2scfgr=0x%04lX amp=%lu last_error=%d\r\n",
           tag,
           dbg->last_mode,
           (unsigned long)dbg->tx_frames,
           (unsigned long)dbg->rx_frames,
           (unsigned long)dbg->last_sample_rate,
           (unsigned long)dbg->last_channels,
           (unsigned long)dbg->codec_addr,
           (unsigned long)dbg->codec_fail_reg,
           (unsigned long)dbg->codec_reg31,
           (unsigned long)dbg->codec_reg32,
           (unsigned long)dbg->codec_reg33,
           (unsigned long)dbg->codec_reg34,
           (unsigned long)dbg->codec_reg0d,
           (unsigned long)dbg->codec_reg25,
           (unsigned long)dbg->codec_reg44,
           (unsigned long)dbg->spi3_sr,
           (unsigned long)dbg->spi3_i2scfgr,
           (unsigned long)dbg->gpio_amp_state,
           dbg->last_error);
}

static void debug_print_text(const char *text)
{
    printf("%s", text);
}

static void dump_record_debug(void)
{
    const audio_record_debug_info_t *dbg = audio_record_get_debug_info();

    printf("[record] samples_per_channel=%lu ch=%lu sr=%lu result=%d\r\n",
           (unsigned long)dbg->samples_per_channel,
           (unsigned long)dbg->channels,
           (unsigned long)dbg->sample_rate,
           dbg->last_result);
}

int main(void)
{
    int init_result;
    int find_result;
    int play_result;
    int record_result = 0;
    char demo_file_path[DEMO_FILE_PATH_BUFFER_SIZE];

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    delay_Init();
    MX_USART1_Init(115200, 1, 2);

    printf("\r\n[audio-demo] baremetal boot\r\n");
    printf("[audio-demo] build %s %s\r\n", __DATE__, __TIME__);
    printf("[audio-demo] USART1 PA9/PA10 115200 8N1\r\n");
    sdcard_stub_set_debug(1, debug_print_text);
    audio_player_set_debug(1, debug_print_text);

    init_result = audio_player_init();
    printf("[audio-demo] audio_player_init=%d err=%s\r\n", init_result, audio_player_last_error());
    dump_board_audio_debug("audio-init");
    if (init_result != 0)
    {
        while (1)
            delay_ms(1000);
    }

    printf("[audio-demo] amp PC13 low/enabled, play 1s test tone\r\n");
    board_audio_amp_set(1);
    board_audio_play_test_tone(1000);
    dump_board_audio_debug("test-tone-enabled");

    board_audio_amp_set(1);

    find_result = audio_player_find_first_mp3(demo_file_path, sizeof(demo_file_path));
    printf("[audio-demo] find_mp3=%d err=%s\r\n", find_result, audio_player_last_error());
    if (find_result == 0)
    {
        printf("[audio-demo] play file=%s\r\n", demo_file_path);
        play_result = audio_player_play_from_sd(demo_file_path);
    }
    else
    {
        play_result = find_result;
    }

    printf("[audio-demo] play_result=%d err=%s\r\n", play_result, audio_player_last_error());
    dump_player_stats();
    dump_board_audio_debug("playback");

#if ENABLE_RECORD_DEMO
    printf("[audio-demo] record demo start, please speak within about 1 second\r\n");
    record_result = audio_record_demo_once();
    printf("[audio-demo] record_result=%d\r\n", record_result);
    dump_record_debug();
    dump_board_audio_debug("record-playback");
#endif

    printf("[audio-demo] done play=%d record=%d\r\n", play_result, record_result);
    while (1)
        delay_ms(1000);
}
