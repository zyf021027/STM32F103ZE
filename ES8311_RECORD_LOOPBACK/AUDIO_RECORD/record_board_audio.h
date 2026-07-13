#ifndef RECORD_BOARD_AUDIO_H
#define RECORD_BOARD_AUDIO_H

#include <stdint.h>

#define BOARD_AUDIO_RECORD_RX_SOFTWARE_PB4 0U
#define BOARD_AUDIO_RECORD_RX_HARDWARE_SPI3 1U

#ifndef BOARD_AUDIO_RECORD_RX_BACKEND
#define BOARD_AUDIO_RECORD_RX_BACKEND BOARD_AUDIO_RECORD_RX_HARDWARE_SPI3
#endif

#if BOARD_AUDIO_RECORD_RX_BACKEND != BOARD_AUDIO_RECORD_RX_SOFTWARE_PB4 && \
    BOARD_AUDIO_RECORD_RX_BACKEND != BOARD_AUDIO_RECORD_RX_HARDWARE_SPI3
#error "Unsupported BOARD_AUDIO_RECORD_RX_BACKEND"
#endif

#define BOARD_AUDIO_SOFT_I2S_VARIANT_COUNT 12U

typedef struct
{
    uint32_t variant;
    uint32_t data_pin;
    uint32_t skip_edges;
    uint32_t sample_falling;
    uint32_t score;
    uint32_t left_abs_peak;
    uint32_t right_abs_peak;
    uint32_t left_nonzero;
    uint32_t right_nonzero;
    uint32_t left_changes;
    uint32_t right_changes;
    uint32_t pin_high;
    uint32_t pin_edges;
    int16_t first_left;
    int16_t first_right;
    int16_t last_left;
    int16_t last_right;
} board_audio_soft_i2s_variant_info_t;

typedef struct
{
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t last_sample_rate;
    uint32_t last_channels;
    uint32_t codec_addr;
    uint32_t codec_fail_reg;
    uint32_t i2c_scl_idle;
    uint32_t i2c_sda_idle;
    uint32_t i2c_recover_pulses;
    uint32_t i2c_probe_attempts;
    int i2c_probe_low_result;
    int i2c_probe_high_result;
    int i2c_probe30_addr_ack;
    int i2c_probe30_reg_ack;
    int i2c_probe30_data_ack;
    int i2c_probe32_addr_ack;
    int i2c_probe32_reg_ack;
    int i2c_probe32_data_ack;
    uint32_t codec_reg00;
    uint32_t codec_reg01;
    uint32_t codec_reg02;
    uint32_t codec_reg03;
    uint32_t codec_reg04;
    uint32_t codec_reg05;
    uint32_t codec_reg06;
    uint32_t codec_reg07;
    uint32_t codec_reg08;
    uint32_t codec_reg09;
    uint32_t codec_reg0a;
    uint32_t codec_reg0b;
    uint32_t codec_reg0c;
    uint32_t codec_reg32;
    uint32_t codec_reg31;
    uint32_t codec_reg33;
    uint32_t codec_reg34;
    uint32_t codec_reg0d;
    uint32_t codec_reg0e;
    uint32_t codec_reg0f;
    uint32_t codec_reg10;
    uint32_t codec_reg11;
    uint32_t codec_reg12;
    uint32_t codec_reg13;
    uint32_t codec_reg14;
    uint32_t codec_reg15;
    uint32_t codec_reg16;
    uint32_t codec_reg17;
    uint32_t codec_reg18;
    uint32_t codec_reg19;
    uint32_t codec_reg1a;
    uint32_t codec_reg1b;
    uint32_t codec_reg1c;
    uint32_t codec_reg25;
    uint32_t codec_reg37;
    uint32_t codec_reg44;
    uint32_t spi3_sr;
    uint32_t spi3_i2scfgr;
    uint32_t spi3_i2spr;
    uint32_t spi3_cr2;
    uint32_t gpioa_idr;
    uint32_t gpiob_idr;
    uint32_t gpioc_idr;
    uint32_t gpioa_odr;
    uint32_t gpiob_odr;
    uint32_t gpioc_odr;
    uint32_t rcc_cfgr;
    uint32_t gpio_amp_state;
    uint32_t dma_underruns;
    uint32_t dma_used_halfwords;
    uint32_t dma_write_index;
    uint32_t rx_left_abs_peak;
    uint32_t rx_right_abs_peak;
    uint32_t rx_left_nonzero;
    uint32_t rx_right_nonzero;
    uint32_t rx_read_timeouts;
    uint32_t rx_overruns;
    uint32_t rx_soft_data_pin;
    uint32_t rx_soft_frames;
    uint32_t rx_soft_bclk_timeouts;
    uint32_t rx_soft_lrck_timeouts;
    uint32_t rx_soft_pin_high;
    uint32_t rx_soft_pin_edges;
    uint32_t pin_scan_samples;
    uint32_t pin_scan_mclk_high;
    uint32_t pin_scan_mclk_edges;
    uint32_t pin_scan_bclk_high;
    uint32_t pin_scan_bclk_edges;
    uint32_t pin_scan_lrck_high;
    uint32_t pin_scan_lrck_edges;
    uint32_t pin_scan_pb4_high;
    uint32_t pin_scan_pb4_edges;
    uint32_t pin_scan_pb5_high;
    uint32_t pin_scan_pb5_edges;
    int16_t rx_first_left[8];
    int16_t rx_first_right[8];
    int16_t rx_last_left[8];
    int16_t rx_last_right[8];
    int last_error;
    int last_mode;
} board_audio_debug_info_t;

int board_audio_init(void);
int board_audio_init_playback(void);
int board_audio_init_record(void);
int board_audio_init_record_rate(uint32_t sample_rate);
void board_audio_set_sample_rate(uint32_t sample_rate);
void board_audio_amp_set(int enabled);
void board_audio_amp_set_pin_high(int high);
int board_audio_play_test_tone(uint32_t duration_ms);
int board_audio_adc_dac_monitor(uint32_t duration_ms);
int board_audio_disable_adc_dac_monitor(void);
int board_audio_probe_soft_i2s_variants(board_audio_soft_i2s_variant_info_t *info, uint32_t count);
uint32_t board_audio_get_soft_i2s_variant(void);
void board_audio_scan_i2s_pins(uint32_t samples);
int board_audio_play_pcm(const int16_t *pcm, uint32_t samples, uint32_t channels);
void board_audio_drain(void);
void board_audio_stop_playback(void);
int board_audio_capture_mono_sample(int16_t *sample);
int board_audio_capture_pcm(int16_t *pcm, uint32_t samples, uint32_t channels);
uint32_t board_audio_get_playback_volume_percent(void);
uint32_t board_audio_get_monitor_dac_volume_reg(void);
uint32_t board_audio_get_record_adc_volume_reg(void);
uint32_t board_audio_get_record_adc_scale_reg(void);
uint32_t board_audio_get_record_mic_path_reg(void);
const board_audio_debug_info_t *board_audio_get_debug_info(void);

#endif
