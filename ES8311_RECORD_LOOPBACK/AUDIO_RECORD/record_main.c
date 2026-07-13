#include "stm32f10x.h"
#include <stdio.h>

#include "delay.h"
#include "usart.h"
#include "audio_record.h"
#include "record_board_audio.h"

static const char *check_text(int ok)
{
    return ok ? "OK" : "FAIL";
}

static int codec_i2c_seen(const board_audio_debug_info_t *dbg)
{
    return (dbg->codec_addr != 0U) ||
           (dbg->i2c_probe_low_result == 0) ||
           (dbg->i2c_probe_high_result == 0) ||
           (dbg->i2c_probe30_addr_ack == 0) ||
           (dbg->i2c_probe32_addr_ack == 0);
}

static void dump_playback_path_check(const char *tag, const board_audio_debug_info_t *dbg)
{
    int i2c_ok = codec_i2c_seen(dbg);
    int mcu_master_tx = ((dbg->spi3_i2scfgr & 0x0F00U) == 0x0E00U);
    int codec_clks = ((dbg->codec_reg01 & 0x3FU) == 0x3FU);
    int dac_serial = ((dbg->codec_reg09 & 0x3FU) == 0x0CU);
    int amp_enabled = (dbg->gpio_amp_state == 0U);
    int tx_seen = (dbg->tx_frames > 0U);

    printf("[%s-check] i2c=%s mcu_master_tx=%s codec_clks=%s dac_sdp=%s amp_enabled=%s tx_seen=%s sr=%lu i2spr=0x%04lX\r\n",
           tag,
           check_text(i2c_ok),
           check_text(mcu_master_tx),
           check_text(codec_clks),
           check_text(dac_serial),
           check_text(amp_enabled),
           check_text(tx_seen),
           (unsigned long)dbg->last_sample_rate,
           (unsigned long)dbg->spi3_i2spr);
}

static void dump_record_path_check(const char *tag, const board_audio_debug_info_t *dbg)
{
    int i2c_ok = codec_i2c_seen(dbg);
    int mcu_master_rx = ((dbg->spi3_i2scfgr & 0x0F00U) == 0x0F00U);
    int codec_clks = ((dbg->codec_reg01 & 0x3FU) == 0x3FU);
    int adc_power = ((dbg->codec_reg0d & 0xF0U) == 0U) && ((dbg->codec_reg0e & 0x70U) == 0U);
    int mic_path = ((dbg->codec_reg14 & 0x40U) == 0U) && ((dbg->codec_reg14 & 0x10U) != 0U);
    int adc_serial = ((dbg->codec_reg0a & 0x3FU) == 0x0CU);
    int asdout_pin = ((dbg->codec_reg07 & 0x10U) == 0U);
    int adcdat_sel = (((dbg->codec_reg44 & 0x70U) == 0U) || ((dbg->codec_reg44 & 0x70U) == 0x50U));
    int audio_seen = (dbg->rx_left_abs_peak > 32U || dbg->rx_right_abs_peak > 32U);

#if BOARD_AUDIO_RECORD_RX_BACKEND == BOARD_AUDIO_RECORD_RX_HARDWARE_SPI3
    printf("[%s-check] i2c=%s mcu_master_rx=%s codec_clks=%s adc_power=%s mic_path=%s adc_sdp=%s asdout_pin=%s adcdat_sel=%s spi_ovr=%s capture=PB5_SPI3_I2S audio_seen=%s\r\n",
           tag,
           check_text(i2c_ok),
           check_text(mcu_master_rx),
           check_text(codec_clks),
           check_text(adc_power),
           check_text(mic_path),
           check_text(adc_serial),
           check_text(asdout_pin),
           check_text(adcdat_sel),
           check_text(dbg->rx_overruns == 0U),
           audio_seen ? "YES" : "NO");
#else
    printf("[%s-check] i2c=%s mcu_master_rx=%s codec_clks=%s adc_power=%s mic_path=%s adc_sdp=%s asdout_pin=%s adcdat_sel=%s spi_ovr=%s capture=PB%lu_SOFT_FIXED audio_seen=%s\r\n",
           tag, check_text(i2c_ok), check_text(mcu_master_rx), check_text(codec_clks),
           check_text(adc_power), check_text(mic_path), check_text(adc_serial),
           check_text(asdout_pin), check_text(adcdat_sel), "IGNORED",
           (unsigned long)dbg->rx_soft_data_pin, audio_seen ? "YES" : "NO");
#endif
    printf("[%s-check-detail] r07_tri_adcdat=%lu r14_dmic=%lu r14_linsel=%lu r14_gain=0x%lX r16_scale=0x%lX r17_adcvol=0x%02lX r0A=0x%02lX r44_adc2dac=%lu r44_adcdat_sel=%lu rx_ovr=%lu soft_pin=PB%lu soft_frames=%lu bclk_to=%lu lrck_to=%lu pin_high=%lu pin_edges=%lu\r\n",
           tag,
           (unsigned long)((dbg->codec_reg07 >> 4) & 1U),
           (unsigned long)((dbg->codec_reg14 >> 6) & 1U),
           (unsigned long)((dbg->codec_reg14 >> 4) & 1U),
           (unsigned long)(dbg->codec_reg14 & 0x0FU),
           (unsigned long)(dbg->codec_reg16 & 0x0FU),
           (unsigned long)dbg->codec_reg17,
           (unsigned long)dbg->codec_reg0a,
           (unsigned long)((dbg->codec_reg44 >> 7) & 1U),
           (unsigned long)((dbg->codec_reg44 >> 4) & 7U),
           (unsigned long)dbg->rx_overruns,
           (unsigned long)dbg->rx_soft_data_pin,
           (unsigned long)dbg->rx_soft_frames,
           (unsigned long)dbg->rx_soft_bclk_timeouts,
           (unsigned long)dbg->rx_soft_lrck_timeouts,
           (unsigned long)dbg->rx_soft_pin_high,
           (unsigned long)dbg->rx_soft_pin_edges);
}

static void dump_board_audio_debug_ptr(const char *tag, const board_audio_debug_info_t *dbg)
{
    printf("[%s] mode=%d tx_frames=%lu rx_frames=%lu sr=%lu ch=%lu codec_addr=0x%02lX fail_reg=0x%02lX amp_pc13=%lu dma_under=%lu dma_used=%lu dma_w=%lu last_error=%d\r\n",
           tag,
           dbg->last_mode,
           (unsigned long)dbg->tx_frames,
           (unsigned long)dbg->rx_frames,
           (unsigned long)dbg->last_sample_rate,
           (unsigned long)dbg->last_channels,
           (unsigned long)dbg->codec_addr,
           (unsigned long)dbg->codec_fail_reg,
           (unsigned long)dbg->gpio_amp_state,
           (unsigned long)dbg->dma_underruns,
           (unsigned long)dbg->dma_used_halfwords,
           (unsigned long)dbg->dma_write_index,
           dbg->last_error);
    printf("[%s-i2c] scl_idle=%lu sda_idle=%lu recover_pulses=%lu attempts=%lu probe30=%d probe32=%d p30_ack=%d/%d/%d p32_ack=%d/%d/%d\r\n",
           tag,
           (unsigned long)dbg->i2c_scl_idle,
           (unsigned long)dbg->i2c_sda_idle,
           (unsigned long)dbg->i2c_recover_pulses,
           (unsigned long)dbg->i2c_probe_attempts,
           dbg->i2c_probe_low_result,
           dbg->i2c_probe_high_result,
           dbg->i2c_probe30_addr_ack,
           dbg->i2c_probe30_reg_ack,
           dbg->i2c_probe30_data_ack,
           dbg->i2c_probe32_addr_ack,
           dbg->i2c_probe32_reg_ack,
           dbg->i2c_probe32_data_ack);
    printf("[%s-codec-a] r00=%02lX r01=%02lX r02=%02lX r03=%02lX r04=%02lX r05=%02lX r06=%02lX r07=%02lX r08=%02lX r09=%02lX r0A=%02lX r0B=%02lX r0C=%02lX\r\n",
           tag,
           (unsigned long)dbg->codec_reg00,
           (unsigned long)dbg->codec_reg01,
           (unsigned long)dbg->codec_reg02,
           (unsigned long)dbg->codec_reg03,
           (unsigned long)dbg->codec_reg04,
           (unsigned long)dbg->codec_reg05,
           (unsigned long)dbg->codec_reg06,
           (unsigned long)dbg->codec_reg07,
           (unsigned long)dbg->codec_reg08,
           (unsigned long)dbg->codec_reg09,
           (unsigned long)dbg->codec_reg0a,
           (unsigned long)dbg->codec_reg0b,
           (unsigned long)dbg->codec_reg0c);
    printf("[%s-codec-b] r0D=%02lX r0E=%02lX r0F=%02lX r10=%02lX r11=%02lX r12=%02lX r13=%02lX r14=%02lX r16=%02lX r25=%02lX r31=%02lX r32=%02lX r33=%02lX r34=%02lX r37=%02lX r44=%02lX\r\n",
           tag,
           (unsigned long)dbg->codec_reg0d,
           (unsigned long)dbg->codec_reg0e,
           (unsigned long)dbg->codec_reg0f,
           (unsigned long)dbg->codec_reg10,
           (unsigned long)dbg->codec_reg11,
           (unsigned long)dbg->codec_reg12,
           (unsigned long)dbg->codec_reg13,
           (unsigned long)dbg->codec_reg14,
           (unsigned long)dbg->codec_reg16,
           (unsigned long)dbg->codec_reg25,
           (unsigned long)dbg->codec_reg31,
           (unsigned long)dbg->codec_reg32,
           (unsigned long)dbg->codec_reg33,
           (unsigned long)dbg->codec_reg34,
           (unsigned long)dbg->codec_reg37,
           (unsigned long)dbg->codec_reg44);
    printf("[%s-codec-c] r15=%02lX r17=%02lX r18=%02lX r19=%02lX r1A=%02lX r1B=%02lX r1C=%02lX\r\n",
           tag,
           (unsigned long)dbg->codec_reg15,
           (unsigned long)dbg->codec_reg17,
           (unsigned long)dbg->codec_reg18,
           (unsigned long)dbg->codec_reg19,
           (unsigned long)dbg->codec_reg1a,
           (unsigned long)dbg->codec_reg1b,
           (unsigned long)dbg->codec_reg1c);
    printf("[%s-mcu] spi_sr=0x%04lX i2scfgr=0x%04lX i2spr=0x%04lX spi_cr2=0x%04lX rcc_cfgr=0x%08lX gpio_idr A=0x%04lX B=0x%04lX C=0x%04lX gpio_odr A=0x%04lX B=0x%04lX C=0x%04lX\r\n",
           tag,
           (unsigned long)dbg->spi3_sr,
           (unsigned long)dbg->spi3_i2scfgr,
           (unsigned long)dbg->spi3_i2spr,
           (unsigned long)dbg->spi3_cr2,
           (unsigned long)dbg->rcc_cfgr,
           (unsigned long)dbg->gpioa_idr,
           (unsigned long)dbg->gpiob_idr,
           (unsigned long)dbg->gpioc_idr,
           (unsigned long)dbg->gpioa_odr,
           (unsigned long)dbg->gpiob_odr,
           (unsigned long)dbg->gpioc_odr);
    printf("[%s-rxraw] left_peak=%lu right_peak=%lu left_nz=%lu right_nz=%lu timeouts=%lu overruns=%lu soft_pin=PB%lu soft_frames=%lu bclk_to=%lu lrck_to=%lu pin_high=%lu pin_edges=%lu\r\n",
           tag,
           (unsigned long)dbg->rx_left_abs_peak,
           (unsigned long)dbg->rx_right_abs_peak,
           (unsigned long)dbg->rx_left_nonzero,
           (unsigned long)dbg->rx_right_nonzero,
           (unsigned long)dbg->rx_read_timeouts,
           (unsigned long)dbg->rx_overruns,
           (unsigned long)dbg->rx_soft_data_pin,
           (unsigned long)dbg->rx_soft_frames,
           (unsigned long)dbg->rx_soft_bclk_timeouts,
           (unsigned long)dbg->rx_soft_lrck_timeouts,
           (unsigned long)dbg->rx_soft_pin_high,
           (unsigned long)dbg->rx_soft_pin_edges);
    printf("[%s-pins] samples=%lu mclk_h=%lu mclk_e=%lu bclk_h=%lu bclk_e=%lu lrck_h=%lu lrck_e=%lu pb4_h=%lu pb4_e=%lu pb5_h=%lu pb5_e=%lu\r\n",
           tag,
           (unsigned long)dbg->pin_scan_samples,
           (unsigned long)dbg->pin_scan_mclk_high,
           (unsigned long)dbg->pin_scan_mclk_edges,
           (unsigned long)dbg->pin_scan_bclk_high,
           (unsigned long)dbg->pin_scan_bclk_edges,
           (unsigned long)dbg->pin_scan_lrck_high,
           (unsigned long)dbg->pin_scan_lrck_edges,
           (unsigned long)dbg->pin_scan_pb4_high,
           (unsigned long)dbg->pin_scan_pb4_edges,
           (unsigned long)dbg->pin_scan_pb5_high,
           (unsigned long)dbg->pin_scan_pb5_edges);
    printf("[%s-rxfirst-l] %d %d %d %d %d %d %d %d\r\n",
           tag,
           dbg->rx_first_left[0], dbg->rx_first_left[1], dbg->rx_first_left[2], dbg->rx_first_left[3],
           dbg->rx_first_left[4], dbg->rx_first_left[5], dbg->rx_first_left[6], dbg->rx_first_left[7]);
    printf("[%s-rxfirst-r] %d %d %d %d %d %d %d %d\r\n",
           tag,
           dbg->rx_first_right[0], dbg->rx_first_right[1], dbg->rx_first_right[2], dbg->rx_first_right[3],
           dbg->rx_first_right[4], dbg->rx_first_right[5], dbg->rx_first_right[6], dbg->rx_first_right[7]);
    printf("[%s-rxlast-l] %d %d %d %d %d %d %d %d\r\n",
           tag,
           dbg->rx_last_left[0], dbg->rx_last_left[1], dbg->rx_last_left[2], dbg->rx_last_left[3],
           dbg->rx_last_left[4], dbg->rx_last_left[5], dbg->rx_last_left[6], dbg->rx_last_left[7]);
    printf("[%s-rxlast-r] %d %d %d %d %d %d %d %d\r\n",
           tag,
           dbg->rx_last_right[0], dbg->rx_last_right[1], dbg->rx_last_right[2], dbg->rx_last_right[3],
           dbg->rx_last_right[4], dbg->rx_last_right[5], dbg->rx_last_right[6], dbg->rx_last_right[7]);
    if (dbg->last_mode == 2)
        dump_record_path_check(tag, dbg);
    else
        dump_playback_path_check(tag, dbg);
}

static void dump_record_debug(void)
{
    const audio_record_debug_info_t *dbg = audio_record_get_debug_info();
    uint32_t i;

    printf("[record] samples_per_channel=%lu ch=%lu sr=%lu duration_s=%lu adpcm_bytes=%lu chunk=%lu tone=%d monitor=%d variant=%lu result=%d\r\n",
           (unsigned long)dbg->samples_per_channel,
           (unsigned long)dbg->channels,
           (unsigned long)dbg->sample_rate,
           (unsigned long)dbg->duration_seconds,
           (unsigned long)dbg->adpcm_bytes,
           (unsigned long)dbg->chunk_samples,
           dbg->test_tone_result,
           dbg->monitor_result,
           (unsigned long)dbg->soft_variant_selected,
           dbg->last_result);
    printf("[record-config] speaker_vol=%lu monitor_dac=0x%02lX target_peak=%lu max_gain_pct=%lu adc_vol=0x%02lX adc_scale=0x%02lX mic_path=0x%02lX\r\n",
           (unsigned long)dbg->playback_volume_percent,
           (unsigned long)dbg->monitor_dac_volume_reg,
           (unsigned long)dbg->playback_target_peak,
           (unsigned long)dbg->playback_max_gain_percent,
           (unsigned long)dbg->record_adc_volume_reg,
           (unsigned long)dbg->record_adc_scale_reg,
           (unsigned long)dbg->record_mic_path_reg);
    printf("[record-capture] min=%d max=%d peak=%lu mean_abs=%lu nonzero=%lu zero=%lu clipped=%lu dc_est=%d\r\n",
           dbg->capture_min,
           dbg->capture_max,
           (unsigned long)dbg->capture_abs_peak,
           (unsigned long)dbg->capture_mean_abs,
           (unsigned long)dbg->capture_nonzero,
           (unsigned long)dbg->capture_zero,
           (unsigned long)dbg->capture_clipped,
           dbg->capture_dc_estimate);
    printf("[record-playback] peak=%lu mean_abs=%lu nonzero=%lu gain_pct=%lu\r\n",
           (unsigned long)dbg->playback_abs_peak,
           (unsigned long)dbg->playback_mean_abs,
           (unsigned long)dbg->playback_nonzero,
           (unsigned long)dbg->playback_gain_percent);
    printf("[record-samples-first] %d %d %d %d %d %d %d %d\r\n",
           dbg->first_samples[0], dbg->first_samples[1], dbg->first_samples[2], dbg->first_samples[3],
           dbg->first_samples[4], dbg->first_samples[5], dbg->first_samples[6], dbg->first_samples[7]);
    printf("[record-samples-last] %d %d %d %d %d %d %d %d\r\n",
           dbg->last_samples[0], dbg->last_samples[1], dbg->last_samples[2], dbg->last_samples[3],
           dbg->last_samples[4], dbg->last_samples[5], dbg->last_samples[6], dbg->last_samples[7]);
    printf("[record-adpcm-first] %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           (unsigned int)dbg->first_adpcm[0], (unsigned int)dbg->first_adpcm[1],
           (unsigned int)dbg->first_adpcm[2], (unsigned int)dbg->first_adpcm[3],
           (unsigned int)dbg->first_adpcm[4], (unsigned int)dbg->first_adpcm[5],
           (unsigned int)dbg->first_adpcm[6], (unsigned int)dbg->first_adpcm[7]);
    printf("[record-adpcm-last] %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           (unsigned int)dbg->last_adpcm[0], (unsigned int)dbg->last_adpcm[1],
           (unsigned int)dbg->last_adpcm[2], (unsigned int)dbg->last_adpcm[3],
           (unsigned int)dbg->last_adpcm[4], (unsigned int)dbg->last_adpcm[5],
           (unsigned int)dbg->last_adpcm[6], (unsigned int)dbg->last_adpcm[7]);

    for (i = 0U; i < BOARD_AUDIO_SOFT_I2S_VARIANT_COUNT; ++i)
    {
        const board_audio_soft_i2s_variant_info_t *v = &dbg->soft_variants[i];
        printf("[record-soft-v%lu] pin=PB%lu skip=%lu edge=%s score=%lu lpk=%lu rpk=%lu lnz=%lu rnz=%lu lchg=%lu rchg=%lu high=%lu edges=%lu first=%d/%d last=%d/%d\r\n",
               (unsigned long)v->variant,
               (unsigned long)v->data_pin,
               (unsigned long)v->skip_edges,
               v->sample_falling ? "fall" : "rise",
               (unsigned long)v->score,
               (unsigned long)v->left_abs_peak,
               (unsigned long)v->right_abs_peak,
               (unsigned long)v->left_nonzero,
               (unsigned long)v->right_nonzero,
               (unsigned long)v->left_changes,
               (unsigned long)v->right_changes,
               (unsigned long)v->pin_high,
               (unsigned long)v->pin_edges,
               v->first_left,
               v->first_right,
               v->last_left,
               v->last_right);
    }
}
int main(void)
{
    int record_result;
    const audio_record_debug_info_t *record_dbg;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    delay_Init();
    MX_USART1_Init(115200, 1, 2);

    printf("\r\n[audio-record] baremetal boot\r\n");
    printf("[audio-record] build %s %s\r\n", __DATE__, __TIME__);
    printf("[audio-record] USART1 PA9/PA10 115200 8N1\r\n");
#if BOARD_AUDIO_RECORD_RX_BACKEND == BOARD_AUDIO_RECORD_RX_HARDWARE_SPI3
    printf("[audio-record] fw=record-weact-16k-hw-i2s-pb5-v43\r\n");
    printf("[audio-record] record_rx=SPI3 I2S MasterRx PB5; record/replay 16k; monitor off\r\n");
#else
    printf("[audio-record] fw=record-weact-16k-soft-i2s-pb4-v43\r\n");
    printf("[audio-record] record_rx=software I2S PB4 v2; record/replay 16k; monitor off\r\n");
#endif

    record_result = audio_record_demo_once();
    printf("[audio-record] record_result=%d\r\n", record_result);
    record_dbg = audio_record_get_debug_info();

    if (record_result == -5)
    {
        printf("[audio-record] init failed before record; check ES8311 I2C/power/connection\r\n");
        dump_board_audio_debug_ptr("init-fail", &record_dbg->playback_init_board);
    }
    else if (record_result == -1)
    {
        printf("[audio-record] record init failed; check ES8311 ADC/mic/I2S setup\r\n");
        dump_board_audio_debug_ptr("record-init", &record_dbg->record_init_board);
    }
    else
    {
        dump_record_debug();
        dump_board_audio_debug_ptr("record-init", &record_dbg->record_init_board);
        dump_board_audio_debug_ptr("record-done", &record_dbg->record_done_board);
        dump_board_audio_debug_ptr("monitor-done", &record_dbg->monitor_done_board);

        if (record_result == -3)
        {
            dump_board_audio_debug_ptr("playback-init", &record_dbg->playback_init_board);
        }
        else if (record_result != -2)
        {
            dump_board_audio_debug_ptr("playback-init", &record_dbg->playback_init_board);
            dump_board_audio_debug_ptr("playback-ready", &record_dbg->playback_ready_board);
            dump_board_audio_debug_ptr("record-loopback", &record_dbg->playback_done_board);
        }
    }

    printf("[audio-record] done record=%d\r\n", record_result);

    while (1)
        delay_ms(1000);
}
