#include "board_audio.h"

#include <string.h>

#include "stm32f10x.h"
#include "audio_baremetal_compat.h"

#define AUDIO_I2C_PORT GPIOB
#define AUDIO_I2C_SCL GPIO_Pin_6
#define AUDIO_I2C_SDA GPIO_Pin_7
#define AUDIO_I2S_MCLK_PORT GPIOC
#define AUDIO_I2S_MCLK_PIN GPIO_Pin_7
#define AUDIO_I2S_WS_PORT GPIOA
#define AUDIO_I2S_WS_PIN GPIO_Pin_15
#define AUDIO_I2S_CK_PORT GPIOB
#define AUDIO_I2S_CK_PIN GPIO_Pin_3
#define AUDIO_I2S_SD_PORT GPIOB
#define AUDIO_I2S_SD_PIN GPIO_Pin_5
#define AUDIO_I2S_RX_PORT GPIOB
#define AUDIO_I2S_RX_PIN GPIO_Pin_4
#define AUDIO_AMP_PORT GPIOC
#define AUDIO_AMP_PIN GPIO_Pin_13
#define AUDIO_AMP_ENABLE() GPIO_ResetBits(AUDIO_AMP_PORT, AUDIO_AMP_PIN)
#define AUDIO_AMP_DISABLE() GPIO_SetBits(AUDIO_AMP_PORT, AUDIO_AMP_PIN)
#define ES8311_I2C_ADDR_WRITE_LOW 0x30
#define ES8311_I2C_ADDR_WRITE_HIGH 0x32
#define ES8311_I2C_READ_OFFSET 0x01
#define AUDIO_SAMPLE_RATE 48000U
#define AUDIO_TEST_TABLE_SIZE 48U
#define AUDIO_PLAYBACK_VOLUME_PERCENT 10U
#define AUDIO_MODE_NONE 0
#define AUDIO_MODE_PLAYBACK 1
#define AUDIO_MODE_RECORD 2
#define AUDIO_DMA_BUFFER_HALFWORDS 4096U
#define AUDIO_DMA_PREROLL_HALFWORDS 1024U
#define AUDIO_DMA_GUARD_HALFWORDS 32U
#define AUDIO_DMA_CHANNEL DMA2_Channel2
static int16_t g_audio_dma_buffer[AUDIO_DMA_BUFFER_HALFWORDS];
static uint32_t g_audio_dma_write_index;
static uint32_t g_audio_dma_last_read_index;
static uint32_t g_audio_dma_read_wraps;
static uint32_t g_audio_dma_read_total;
static uint32_t g_audio_dma_write_total;
static int g_audio_dma_started;
static uint32_t g_audio_dma_underruns;

static const int16_t audio_test_wave[AUDIO_TEST_TABLE_SIZE] = {
    0, 1566, 3105, 4592, 6000, 7308, 8485, 9510,
    10392, 11012, 11590, 11876, 12000, 11876, 11590, 11012,
    10392, 9510, 8485, 7308, 6000, 4592, 3105, 1566,
    0, -1566, -3105, -4592, -6000, -7308, -8485, -9510,
    -10392, -11012, -11590, -11876, -12000, -11876, -11590, -11012,
    -10392, -9510, -8485, -7308, -6000, -4592, -3105, -1566};

static int16_t board_audio_apply_volume(int16_t sample)
{
    int32_t value = ((int32_t)sample * (int32_t)AUDIO_PLAYBACK_VOLUME_PERCENT) / 100;

    if (value > 32767)
        return 32767;
    if (value < -32768)
        return -32768;
    return (int16_t)value;
}
static board_audio_debug_info_t g_audio_debug;
static uint32_t board_audio_dma_used_halfwords(void);
static void board_audio_update_dma_debug(void)
{
    g_audio_debug.dma_underruns = g_audio_dma_underruns;
    g_audio_debug.dma_used_halfwords = board_audio_dma_used_halfwords();
    g_audio_debug.dma_write_index = g_audio_dma_write_index;
}
static uint8_t g_es8311_addr_write = ES8311_I2C_ADDR_WRITE_LOW;

static void audio_delay_short(void)
{
    volatile int i;
    for (i = 0; i < 40; ++i)
    {
        __NOP();
    }
}

static void board_audio_set_error(int err)
{
    g_audio_debug.last_error = err;
}

static void board_audio_reset_debug(int mode)
{
    memset(&g_audio_debug, 0, sizeof(g_audio_debug));
    g_audio_debug.last_mode = mode;
    g_audio_debug.last_sample_rate = AUDIO_SAMPLE_RATE;
}

static void i2c_scl_high(void)
{
    GPIO_SetBits(AUDIO_I2C_PORT, AUDIO_I2C_SCL);
    audio_delay_short();
}
static void i2c_scl_low(void)
{
    GPIO_ResetBits(AUDIO_I2C_PORT, AUDIO_I2C_SCL);
    audio_delay_short();
}
static void i2c_sda_high(void)
{
    GPIO_SetBits(AUDIO_I2C_PORT, AUDIO_I2C_SDA);
    audio_delay_short();
}
static void i2c_sda_low(void)
{
    GPIO_ResetBits(AUDIO_I2C_PORT, AUDIO_I2C_SDA);
    audio_delay_short();
}
static int i2c_sda_read(void) { return GPIO_ReadInputDataBit(AUDIO_I2C_PORT, AUDIO_I2C_SDA); }

static void i2c_start(void)
{
    i2c_sda_high();
    i2c_scl_high();
    i2c_sda_low();
    i2c_scl_low();
}

static void i2c_stop(void)
{
    i2c_sda_low();
    i2c_scl_high();
    i2c_sda_high();
}

static int i2c_write_byte(uint8_t value)
{
    int bit;
    int ack;

    for (bit = 0; bit < 8; ++bit)
    {
        if (value & 0x80U)
            i2c_sda_high();
        else
            i2c_sda_low();
        i2c_scl_high();
        i2c_scl_low();
        value <<= 1;
    }

    i2c_sda_high();
    i2c_scl_high();
    ack = i2c_sda_read();
    i2c_scl_low();
    return ack == 0 ? 0 : -1;
}

static uint8_t i2c_read_byte(int ack)
{
    int bit;
    uint8_t value = 0;

    i2c_sda_high();
    for (bit = 0; bit < 8; ++bit)
    {
        value <<= 1;
        i2c_scl_high();
        if (i2c_sda_read())
            value |= 1U;
        i2c_scl_low();
    }

    if (ack)
        i2c_sda_low();
    else
        i2c_sda_high();
    i2c_scl_high();
    i2c_scl_low();
    i2c_sda_high();
    return value;
}

static int es8311_write_reg_to(uint8_t addr, uint8_t reg, uint8_t value)
{
    int result;

    i2c_start();
    result = i2c_write_byte(addr);
    result |= i2c_write_byte(reg);
    result |= i2c_write_byte(value);
    i2c_stop();
    return result;
}

static int es8311_write_reg(uint8_t reg, uint8_t value)
{
    int result = es8311_write_reg_to(g_es8311_addr_write, reg, value);

    if (result != 0)
        g_audio_debug.codec_fail_reg = reg;
    return result;
}

static int es8311_read_reg(uint8_t reg, uint8_t *value)
{
    int result;

    i2c_start();
    result = i2c_write_byte(g_es8311_addr_write);
    result |= i2c_write_byte(reg);
    i2c_start();
    result |= i2c_write_byte((uint8_t)(g_es8311_addr_write | ES8311_I2C_READ_OFFSET));
    if (result == 0)
        *value = i2c_read_byte(0);
    i2c_stop();
    if (result != 0)
        g_audio_debug.codec_fail_reg = reg;
    return result;
}

static void board_audio_capture_debug_regs(void)
{
    uint8_t value;

    if (es8311_read_reg(0x00, &value) == 0)
        g_audio_debug.codec_reg00 = value;
    if (es8311_read_reg(0x01, &value) == 0)
        g_audio_debug.codec_reg01 = value;
    if (es8311_read_reg(0x02, &value) == 0)
        g_audio_debug.codec_reg02 = value;
    if (es8311_read_reg(0x03, &value) == 0)
        g_audio_debug.codec_reg03 = value;
    if (es8311_read_reg(0x04, &value) == 0)
        g_audio_debug.codec_reg04 = value;
    if (es8311_read_reg(0x05, &value) == 0)
        g_audio_debug.codec_reg05 = value;
    if (es8311_read_reg(0x06, &value) == 0)
        g_audio_debug.codec_reg06 = value;
    if (es8311_read_reg(0x07, &value) == 0)
        g_audio_debug.codec_reg07 = value;
    if (es8311_read_reg(0x08, &value) == 0)
        g_audio_debug.codec_reg08 = value;
    if (es8311_read_reg(0x09, &value) == 0)
        g_audio_debug.codec_reg09 = value;
    if (es8311_read_reg(0x0A, &value) == 0)
        g_audio_debug.codec_reg0a = value;
    if (es8311_read_reg(0x0B, &value) == 0)
        g_audio_debug.codec_reg0b = value;
    if (es8311_read_reg(0x0C, &value) == 0)
        g_audio_debug.codec_reg0c = value;
    if (es8311_read_reg(0x0D, &value) == 0)
        g_audio_debug.codec_reg0d = value;
    if (es8311_read_reg(0x0E, &value) == 0)
        g_audio_debug.codec_reg0e = value;
    if (es8311_read_reg(0x0F, &value) == 0)
        g_audio_debug.codec_reg0f = value;
    if (es8311_read_reg(0x10, &value) == 0)
        g_audio_debug.codec_reg10 = value;
    if (es8311_read_reg(0x11, &value) == 0)
        g_audio_debug.codec_reg11 = value;
    if (es8311_read_reg(0x12, &value) == 0)
        g_audio_debug.codec_reg12 = value;
    if (es8311_read_reg(0x13, &value) == 0)
        g_audio_debug.codec_reg13 = value;
    if (es8311_read_reg(0x14, &value) == 0)
        g_audio_debug.codec_reg14 = value;
    if (es8311_read_reg(0x16, &value) == 0)
        g_audio_debug.codec_reg16 = value;
    if (es8311_read_reg(0x25, &value) == 0)
        g_audio_debug.codec_reg25 = value;
    if (es8311_read_reg(0x31, &value) == 0)
        g_audio_debug.codec_reg31 = value;
    if (es8311_read_reg(0x32, &value) == 0)
        g_audio_debug.codec_reg32 = value;
    if (es8311_read_reg(0x33, &value) == 0)
        g_audio_debug.codec_reg33 = value;
    if (es8311_read_reg(0x34, &value) == 0)
        g_audio_debug.codec_reg34 = value;
    if (es8311_read_reg(0x37, &value) == 0)
        g_audio_debug.codec_reg37 = value;
    if (es8311_read_reg(0x44, &value) == 0)
        g_audio_debug.codec_reg44 = value;

    g_audio_debug.spi3_sr = SPI3->SR;
    g_audio_debug.spi3_i2scfgr = SPI3->I2SCFGR;
    g_audio_debug.spi3_i2spr = SPI3->I2SPR;
    g_audio_debug.spi3_cr2 = SPI3->CR2;
    g_audio_debug.gpioa_idr = GPIOA->IDR;
    g_audio_debug.gpiob_idr = GPIOB->IDR;
    g_audio_debug.gpioc_idr = GPIOC->IDR;
    g_audio_debug.gpioa_odr = GPIOA->ODR;
    g_audio_debug.gpiob_odr = GPIOB->ODR;
    g_audio_debug.gpioc_odr = GPIOC->ODR;
    g_audio_debug.rcc_cfgr = RCC->CFGR;
    g_audio_debug.gpio_amp_state = GPIO_ReadOutputDataBit(AUDIO_AMP_PORT, AUDIO_AMP_PIN);
    board_audio_update_dma_debug();
}

static int es8311_probe_addr(uint8_t addr)
{
    i2c_stop();
    return es8311_write_reg_to(addr, 0x00, 0x80);
}

static int es8311_probe(void)
{
    if (es8311_probe_addr(ES8311_I2C_ADDR_WRITE_LOW) == 0)
    {
        g_es8311_addr_write = ES8311_I2C_ADDR_WRITE_LOW;
        g_audio_debug.codec_addr = g_es8311_addr_write;
        return 0;
    }

    if (es8311_probe_addr(ES8311_I2C_ADDR_WRITE_HIGH) == 0)
    {
        g_es8311_addr_write = ES8311_I2C_ADDR_WRITE_HIGH;
        g_audio_debug.codec_addr = g_es8311_addr_write;
        return 0;
    }

    g_audio_debug.codec_addr = 0;
    g_audio_debug.codec_fail_reg = 0xFFU;
    return -1;
}

static void board_audio_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI3, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = AUDIO_I2C_SCL | AUDIO_I2C_SDA;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(AUDIO_I2C_PORT, &gpio);
    i2c_sda_high();
    i2c_scl_high();
    audio_delay_ms(2);

    gpio.GPIO_Pin = AUDIO_AMP_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(AUDIO_AMP_PORT, &gpio);
    AUDIO_AMP_ENABLE();

    gpio.GPIO_Pin = AUDIO_I2S_MCLK_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(AUDIO_I2S_MCLK_PORT, &gpio);

    gpio.GPIO_Pin = AUDIO_I2S_WS_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(AUDIO_I2S_WS_PORT, &gpio);

    gpio.GPIO_Pin = AUDIO_I2S_CK_PIN | AUDIO_I2S_SD_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(AUDIO_I2S_CK_PORT, &gpio);

    gpio.GPIO_Pin = AUDIO_I2S_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(AUDIO_I2S_RX_PORT, &gpio);
}

static void board_audio_write_frame(int16_t left, int16_t right);
static void board_audio_dma_stop(void);
static void board_audio_dma_start(void);

static uint32_t board_audio_dma_read_index(void)
{
    uint32_t remaining;

    if (!g_audio_dma_started)
        return 0U;

    remaining = DMA_GetCurrDataCounter(AUDIO_DMA_CHANNEL);
    if (remaining > AUDIO_DMA_BUFFER_HALFWORDS)
        remaining = AUDIO_DMA_BUFFER_HALFWORDS;
    return (AUDIO_DMA_BUFFER_HALFWORDS - remaining) % AUDIO_DMA_BUFFER_HALFWORDS;
}

static uint32_t board_audio_dma_update_read_total(void)
{
    uint32_t read_index = board_audio_dma_read_index();

    if (read_index < g_audio_dma_last_read_index)
        g_audio_dma_read_wraps++;
    g_audio_dma_last_read_index = read_index;
    g_audio_dma_read_total = (g_audio_dma_read_wraps * AUDIO_DMA_BUFFER_HALFWORDS) + read_index;
    return g_audio_dma_read_total;
}

static uint32_t board_audio_dma_used_halfwords(void)
{
    uint32_t read_total = board_audio_dma_update_read_total();

    if (g_audio_dma_write_total <= read_total)
        return 0U;
    return g_audio_dma_write_total - read_total;
}

static uint32_t board_audio_dma_free_halfwords(void)
{
    uint32_t used = board_audio_dma_used_halfwords();

    if (used >= (AUDIO_DMA_BUFFER_HALFWORDS - AUDIO_DMA_GUARD_HALFWORDS))
        return 0U;
    return AUDIO_DMA_BUFFER_HALFWORDS - used - AUDIO_DMA_GUARD_HALFWORDS;
}

static void board_audio_dma_recover_if_underrun(void)
{
    uint32_t read_total = board_audio_dma_update_read_total();

    if (g_audio_dma_write_total <= read_total)
    {
        g_audio_dma_underruns++;
        memset(g_audio_dma_buffer, 0, sizeof(g_audio_dma_buffer));
        g_audio_dma_write_total = read_total + AUDIO_DMA_PREROLL_HALFWORDS;
        g_audio_dma_write_index = g_audio_dma_write_total % AUDIO_DMA_BUFFER_HALFWORDS;
    }
}

static void board_audio_dma_wait_free(uint32_t halfwords)
{
    while (board_audio_dma_free_halfwords() < halfwords)
    {
        board_audio_dma_recover_if_underrun();
        __NOP();
    }
}

static void board_audio_dma_write_frame(int16_t left, int16_t right)
{
    if (!g_audio_dma_started)
    {
        board_audio_write_frame(left, right);
        return;
    }

    board_audio_dma_wait_free(2U);
    board_audio_dma_recover_if_underrun();
    g_audio_dma_buffer[g_audio_dma_write_index] = left;
    g_audio_dma_write_index = (g_audio_dma_write_index + 1U) % AUDIO_DMA_BUFFER_HALFWORDS;
    g_audio_dma_write_total++;
    g_audio_dma_buffer[g_audio_dma_write_index] = right;
    g_audio_dma_write_index = (g_audio_dma_write_index + 1U) % AUDIO_DMA_BUFFER_HALFWORDS;
    g_audio_dma_write_total++;
    g_audio_debug.tx_frames++;
}

static void board_audio_dma_stop(void)
{
    SPI_I2S_DMACmd(SPI3, SPI_I2S_DMAReq_Tx, DISABLE);
    DMA_Cmd(AUDIO_DMA_CHANNEL, DISABLE);
    g_audio_dma_started = 0;
    g_audio_dma_write_index = 0U;
    g_audio_dma_last_read_index = 0U;
    g_audio_dma_read_wraps = 0U;
    g_audio_dma_read_total = 0U;
    g_audio_dma_write_total = 0U;
}

static void board_audio_dma_start(void)
{
    DMA_InitTypeDef dma;
    uint32_t read_index;

    memset(g_audio_dma_buffer, 0, sizeof(g_audio_dma_buffer));
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
    DMA_Cmd(AUDIO_DMA_CHANNEL, DISABLE);
    DMA_DeInit(AUDIO_DMA_CHANNEL);

    dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI3->DR;
    dma.DMA_MemoryBaseAddr = (uint32_t)g_audio_dma_buffer;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = AUDIO_DMA_BUFFER_HALFWORDS;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(AUDIO_DMA_CHANNEL, &dma);

    SPI_I2S_DMACmd(SPI3, SPI_I2S_DMAReq_Tx, ENABLE);
    DMA_Cmd(AUDIO_DMA_CHANNEL, ENABLE);
    g_audio_dma_started = 1;
    read_index = board_audio_dma_read_index();
    g_audio_dma_last_read_index = read_index;
    g_audio_dma_read_wraps = 0U;
    g_audio_dma_read_total = read_index;
    g_audio_dma_write_total = g_audio_dma_read_total + AUDIO_DMA_PREROLL_HALFWORDS;
    g_audio_dma_write_index = g_audio_dma_write_total % AUDIO_DMA_BUFFER_HALFWORDS;
}

static void board_audio_dma_drain(void)
{
    uint32_t timeout = AUDIO_DMA_BUFFER_HALFWORDS * 4U;

    while (board_audio_dma_used_halfwords() > AUDIO_DMA_PREROLL_HALFWORDS && timeout-- > 0U)
    {
        __NOP();
    }
}

static uint32_t board_audio_i2s_freq(uint32_t sample_rate)
{
    if (sample_rate >= 43000U && sample_rate <= 45000U)
        return I2S_AudioFreq_44k;
    return I2S_AudioFreq_48k;
}

static void board_audio_i2s_init_tx_rate(uint32_t sample_rate)
{
    I2S_InitTypeDef i2s;

    board_audio_dma_stop();
    SPI_I2S_DeInit(SPI3);
    i2s.I2S_Mode = I2S_Mode_MasterTx;
    i2s.I2S_Standard = I2S_Standard_Phillips;
    i2s.I2S_DataFormat = I2S_DataFormat_16b;
    i2s.I2S_MCLKOutput = I2S_MCLKOutput_Enable;
    i2s.I2S_AudioFreq = board_audio_i2s_freq(sample_rate);
    i2s.I2S_CPOL = I2S_CPOL_Low;
    I2S_Init(SPI3, &i2s);
    I2S_Cmd(SPI3, ENABLE);
    board_audio_dma_start();
    g_audio_debug.last_sample_rate = sample_rate;
    g_audio_debug.spi3_sr = SPI3->SR;
    g_audio_debug.spi3_i2scfgr = SPI3->I2SCFGR;
}

static void board_audio_i2s_init_tx(void)
{
    board_audio_i2s_init_tx_rate(AUDIO_SAMPLE_RATE);
}

static void board_audio_i2s_init_rx(void)
{
    I2S_InitTypeDef i2s;

    SPI_I2S_DeInit(SPI3);
    i2s.I2S_Mode = I2S_Mode_MasterRx;
    i2s.I2S_Standard = I2S_Standard_Phillips;
    i2s.I2S_DataFormat = I2S_DataFormat_16b;
    i2s.I2S_MCLKOutput = I2S_MCLKOutput_Enable;
    i2s.I2S_AudioFreq = I2S_AudioFreq_48k;
    i2s.I2S_CPOL = I2S_CPOL_Low;
    I2S_Init(SPI3, &i2s);
    I2S_Cmd(SPI3, ENABLE);
}

static int board_audio_codec_init_playback(void)
{
    int result = 0;

    if (es8311_probe() != 0)
        return -1;

    result |= es8311_write_reg(0x00, 0x3F);
    audio_delay_ms(10);
    result |= es8311_write_reg(0x00, 0x80);
    audio_delay_ms(2);
    result |= es8311_write_reg(0x01, 0x30);
    result |= es8311_write_reg(0x02, 0x00);
    result |= es8311_write_reg(0x03, 0x10);
    result |= es8311_write_reg(0x16, 0x24);
    result |= es8311_write_reg(0x04, 0x10);
    result |= es8311_write_reg(0x05, 0x00);
    result |= es8311_write_reg(0x0B, 0x00);
    result |= es8311_write_reg(0x0C, 0x00);
    result |= es8311_write_reg(0x10, 0x1F);
    result |= es8311_write_reg(0x11, 0x7F);
    result |= es8311_write_reg(0x06, 0x0F);
    result |= es8311_write_reg(0x07, 0xFF);
    result |= es8311_write_reg(0x08, 0x04);
    result |= es8311_write_reg(0x09, 0x0C);
    result |= es8311_write_reg(0x0A, 0x4C);
    result |= es8311_write_reg(0x44, 0x08);
    result |= es8311_write_reg(0x31, 0x00);
    result |= es8311_write_reg(0x32, 0xBF);
    result |= es8311_write_reg(0x33, 0x00);
    result |= es8311_write_reg(0x34, 0x00);
    result |= es8311_write_reg(0x0D, 0x01);
    audio_delay_ms(120);
    result |= es8311_write_reg(0x0E, 0x02);
    result |= es8311_write_reg(0x12, 0x01);
    result |= es8311_write_reg(0x13, 0x10);
    result |= es8311_write_reg(0x14, 0x1A);
    result |= es8311_write_reg(0x0D, 0x06);
    audio_delay_ms(30);
    result |= es8311_write_reg(0x25, 0x00);
    result |= es8311_write_reg(0x01, 0x3F);
    board_audio_capture_debug_regs();
    return result;
}

static int board_audio_codec_init_record(void)
{
    int result = 0;

    if (es8311_probe() != 0)
        return -1;

    result |= es8311_write_reg(0x01, 0x30);
    result |= es8311_write_reg(0x02, 0x00);
    result |= es8311_write_reg(0x03, 0x10);
    result |= es8311_write_reg(0x04, 0x10);
    result |= es8311_write_reg(0x05, 0x00);
    result |= es8311_write_reg(0x06, 0x00);
    result |= es8311_write_reg(0x07, 0x10);
    result |= es8311_write_reg(0x08, 0x20);
    result |= es8311_write_reg(0x09, 0x30);
    result |= es8311_write_reg(0x0A, 0x00);
    result |= es8311_write_reg(0x0B, 0x00);
    result |= es8311_write_reg(0x0C, 0x00);
    result |= es8311_write_reg(0x14, 0x18);
    result |= es8311_write_reg(0x15, 0x00);
    result |= es8311_write_reg(0x16, 0x24);
    result |= es8311_write_reg(0x17, 0x88);
    result |= es8311_write_reg(0x18, 0x02);
    result |= es8311_write_reg(0x19, 0x20);
    result |= es8311_write_reg(0x1A, 0x20);
    result |= es8311_write_reg(0x37, 0x08);
    result |= es8311_write_reg(0x44, 0x00);
    result |= es8311_write_reg(0x00, 0x80);
    result |= es8311_write_reg(0x01, 0x3F);
    board_audio_capture_debug_regs();
    return result;
}

static void board_audio_write_word(int16_t value)
{
    while (SPI_I2S_GetFlagStatus(SPI3, SPI_I2S_FLAG_TXE) == RESET)
    {
    }
    SPI_I2S_SendData(SPI3, (uint16_t)value);
}

static int16_t board_audio_read_word(void)
{
    while (SPI_I2S_GetFlagStatus(SPI3, SPI_I2S_FLAG_RXNE) == RESET)
    {
    }
    return (int16_t)SPI_I2S_ReceiveData(SPI3);
}

static void board_audio_write_frame(int16_t left, int16_t right)
{
    board_audio_write_word(left);
    board_audio_write_word(right);
    g_audio_debug.tx_frames++;
}

int board_audio_init_playback(void)
{
    board_audio_gpio_init();
    board_audio_reset_debug(AUDIO_MODE_PLAYBACK);
    audio_delay_ms(10);
    board_audio_i2s_init_tx();
    audio_delay_ms(10);
    if (board_audio_codec_init_playback() != 0)
    {
        board_audio_set_error(-1);
        return -1;
    }
    audio_delay_ms(20);
    AUDIO_AMP_ENABLE();
    return 0;
}

int board_audio_init_record(void)
{
    board_audio_gpio_init();
    board_audio_reset_debug(AUDIO_MODE_RECORD);
    audio_delay_ms(10);
    board_audio_i2s_init_rx();
    audio_delay_ms(10);
    if (board_audio_codec_init_record() != 0)
    {
        board_audio_set_error(-2);
        return -1;
    }
    audio_delay_ms(20);
    AUDIO_AMP_ENABLE();
    return 0;
}

int board_audio_init(void)
{
    return board_audio_init_playback();
}

void board_audio_set_sample_rate(uint32_t sample_rate)
{
    if (sample_rate == 0U || sample_rate == g_audio_debug.last_sample_rate)
        return;

    board_audio_i2s_init_tx_rate(sample_rate);
}

void board_audio_amp_set(int enabled)
{
    if (enabled)
        AUDIO_AMP_ENABLE();
    else
        AUDIO_AMP_DISABLE();
    board_audio_capture_debug_regs();
}

void board_audio_amp_set_pin_high(int high)
{
    if (high)
        GPIO_SetBits(AUDIO_AMP_PORT, AUDIO_AMP_PIN);
    else
        GPIO_ResetBits(AUDIO_AMP_PORT, AUDIO_AMP_PIN);
    board_audio_capture_debug_regs();
}

int board_audio_play_pcm(const int16_t *pcm, uint32_t samples, uint32_t channels)
{
    uint32_t i;

    if (pcm == RT_NULL || samples == 0U)
    {
        board_audio_set_error(-3);
        return -1;
    }

    g_audio_debug.last_channels = channels;
    g_audio_debug.spi3_sr = SPI3->SR;
    g_audio_debug.spi3_i2scfgr = SPI3->I2SCFGR;
    g_audio_debug.gpio_amp_state = GPIO_ReadOutputDataBit(AUDIO_AMP_PORT, AUDIO_AMP_PIN);
    board_audio_update_dma_debug();

    if (channels == 1U)
    {
        for (i = 0; i < samples; ++i)
            board_audio_dma_write_frame(board_audio_apply_volume(pcm[i]), board_audio_apply_volume(pcm[i]));
        board_audio_update_dma_debug();
        return 0;
    }

    if (channels == 2U)
    {
        for (i = 0; i < samples; ++i)
        {
            int32_t mixed = ((int32_t)pcm[i * 2U] + (int32_t)pcm[i * 2U + 1U]) / 2;
            int16_t sample = board_audio_apply_volume((int16_t)mixed);
            board_audio_dma_write_frame(sample, sample);
        }
        board_audio_update_dma_debug();
        return 0;
    }

    board_audio_set_error(-4);
    return -2;
}

void board_audio_drain(void)
{
    board_audio_dma_drain();
}

int board_audio_capture_pcm(int16_t *pcm, uint32_t samples, uint32_t channels)
{
    uint32_t i;
    int16_t left;
    int16_t right;

    if (pcm == RT_NULL || samples == 0U)
    {
        board_audio_set_error(-5);
        return -1;
    }

    if (channels != 1U && channels != 2U)
    {
        board_audio_set_error(-6);
        return -2;
    }

    g_audio_debug.last_channels = channels;

    for (i = 0; i < samples; ++i)
    {
        left = board_audio_read_word();
        right = board_audio_read_word();
        if (channels == 1U)
            pcm[i] = left;
        else
        {
            pcm[i * 2U] = left;
            pcm[i * 2U + 1U] = right;
        }
        g_audio_debug.rx_frames++;
    }

    return 0;
}

int board_audio_play_test_tone(uint32_t duration_ms)
{
    uint32_t total_samples = (AUDIO_SAMPLE_RATE * duration_ms) / 1000U;
    uint32_t index;

    for (index = 0; index < total_samples; ++index)
        board_audio_dma_write_frame(board_audio_apply_volume(audio_test_wave[index % AUDIO_TEST_TABLE_SIZE]), board_audio_apply_volume(audio_test_wave[index % AUDIO_TEST_TABLE_SIZE]));
    return 0;
}

const board_audio_debug_info_t *board_audio_get_debug_info(void)
{
    return &g_audio_debug;
}
