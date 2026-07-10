#include "stm32f10x.h"
#include <stdio.h>

#include "delay.h"
#include "usart.h"
#include "audio_record.h"
#include "record_board_audio.h"

static void dump_board_audio_debug(const char *tag)
{
    const board_audio_debug_info_t *dbg = board_audio_get_debug_info();

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
    int record_result;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    delay_Init();
    MX_USART1_Init(115200, 1, 2);

    printf("\r\n[audio-record] baremetal boot\r\n");
    printf("[audio-record] build %s %s\r\n", __DATE__, __TIME__);
    printf("[audio-record] USART1 PA9/PA10 115200 8N1\r\n");
    printf("[audio-record] record loopback start, speak for about 1 second\r\n");

    record_result = audio_record_demo_once();
    printf("[audio-record] record_result=%d\r\n", record_result);
    dump_record_debug();
    dump_board_audio_debug("record-loopback");
    printf("[audio-record] done record=%d\r\n", record_result);

    while (1)
        delay_ms(1000);
}
