#include "stm32f10x.h"
#include "rtthread.h"
#include "audio_player.h"
#include "audio_record.h"
#include "board_audio.h"
#include "debug_uart.h"

#define LED_PERIPH                 RCC_APB2Periph_GPIOC
#define LED_PORT                   GPIOC
#define LED_PIN                    GPIO_Pin_13

#define DEMO_FILE_PATH_BUFFER_SIZE 13U
#define ENABLE_RECORD_DEMO         1

typedef enum {
    APP_STAGE_BOOT = 0,
    APP_STAGE_AUDIO_OK,
    APP_STAGE_SD_MOUNT_OK,
    APP_STAGE_FILE_OPEN_OK,
    APP_STAGE_RECORD_OK,
    APP_STAGE_FATAL_ERROR
} app_stage_t;

static void led_init(void)
{
    GPIO_InitTypeDef gpioDef;

    RCC_APB2PeriphClockCmd(LED_PERIPH, ENABLE);

    gpioDef.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpioDef.GPIO_Pin   = LED_PIN;
    gpioDef.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(LED_PORT, &gpioDef);
    GPIO_SetBits(LED_PORT, LED_PIN);
}

static void led_toggle(void)
{
    GPIO_WriteBit(LED_PORT, LED_PIN,
                  (BitAction)!GPIO_ReadInputDataBit(LED_PORT, LED_PIN));
}

static void blink_pattern(unsigned int count, unsigned int on_ms, unsigned int off_ms)
{
    unsigned int i;

    for (i = 0; i < count; ++i) {
        GPIO_ResetBits(LED_PORT, LED_PIN);
        rt_thread_mdelay(on_ms);
        GPIO_SetBits(LED_PORT, LED_PIN);
        rt_thread_mdelay(off_ms);
    }
}

static void indicate_stage(app_stage_t stage)
{
    switch (stage) {
        case APP_STAGE_BOOT:
            blink_pattern(1, 80, 120);
            break;
        case APP_STAGE_AUDIO_OK:
            blink_pattern(2, 80, 120);
            break;
        case APP_STAGE_SD_MOUNT_OK:
            blink_pattern(3, 80, 120);
            break;
        case APP_STAGE_FILE_OPEN_OK:
            blink_pattern(4, 80, 120);
            break;
        case APP_STAGE_RECORD_OK:
            blink_pattern(5, 80, 120);
            break;
        case APP_STAGE_FATAL_ERROR:
        default:
            blink_pattern(6, 60, 60);
            break;
    }
}

static void dump_player_stats(void)
{
    const audio_player_stats_t *stats = audio_player_get_stats();

    debug_uart_printf("[mp3] bytes_read=%lu decode_calls=%lu frames_decoded=%lu frames_skipped=%lu pcm_blocks=%lu sr=%lu ch=%lu\n",
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

    debug_uart_printf("[%s] mode=%d tx_frames=%lu rx_frames=%lu sr=%lu ch=%lu last_error=%d\n",
                      tag,
                      dbg->last_mode,
                      (unsigned long)dbg->tx_frames,
                      (unsigned long)dbg->rx_frames,
                      (unsigned long)dbg->last_sample_rate,
                      (unsigned long)dbg->last_channels,
                      dbg->last_error);
}

static void dump_record_debug(void)
{
    const audio_record_debug_info_t *dbg = audio_record_get_debug_info();

    debug_uart_printf("[record] samples_per_channel=%lu ch=%lu sr=%lu result=%d\n",
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

    debug_uart_init();
    debug_uart_puts("\n[audio-demo] boot\n");

    led_init();
    indicate_stage(APP_STAGE_BOOT);

    init_result = audio_player_init();
    debug_uart_printf("[audio-demo] audio_player_init=%d err=%s\n", init_result, audio_player_last_error());
    dump_board_audio_debug("audio-init");
    if (init_result != 0) {
        while (1) {
            indicate_stage(APP_STAGE_FATAL_ERROR);
            rt_thread_mdelay(600);
        }
    }

    indicate_stage(APP_STAGE_AUDIO_OK);
    indicate_stage(APP_STAGE_SD_MOUNT_OK);

    find_result = audio_player_find_first_mp3(demo_file_path, sizeof(demo_file_path));
    debug_uart_printf("[audio-demo] find_mp3=%d err=%s\n", find_result, audio_player_last_error());
    if (find_result != 0) {
        play_result = find_result;
    } else {
        debug_uart_printf("[audio-demo] play file=%s\n", demo_file_path);
        play_result = audio_player_play_from_sd(demo_file_path);
    }
    debug_uart_printf("[audio-demo] play_result=%d err=%s\n", play_result, audio_player_last_error());
    dump_player_stats();
    dump_board_audio_debug("playback");
    if (play_result == 0)
        indicate_stage(APP_STAGE_FILE_OPEN_OK);

#if ENABLE_RECORD_DEMO
    debug_uart_puts("[audio-demo] record demo start, please speak within about 1 second\n");
    record_result = audio_record_demo_once();
    debug_uart_printf("[audio-demo] record_result=%d\n", record_result);
    dump_record_debug();
    dump_board_audio_debug("record-playback");
    if (record_result == 0)
        indicate_stage(APP_STAGE_RECORD_OK);
#endif

    while (1) {
        if (play_result != 0 || record_result != 0) {
            led_toggle();
            rt_thread_mdelay(150);
            led_toggle();
            rt_thread_mdelay(850);
        } else {
            led_toggle();
            rt_thread_mdelay(1000);
        }
    }
}
