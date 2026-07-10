#include "record_board_audio.h"

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
#define AUDIO_PLAYBACK_VOLUME_PERCENT 20U
#define AUDIO_MONITOR_DAC_VOLUME_REG 0xA3U
#define AUDIO_MODE_NONE 0
#define AUDIO_MODE_PLAYBACK 1
#define AUDIO_MODE_RECORD 2
#define AUDIO_DMA_BUFFER_HALFWORDS 4096U
#define AUDIO_DMA_PREROLL_HALFWORDS 1024U
#define AUDIO_DMA_GUARD_HALFWORDS 32U
#define AUDIO_DMA_CHANNEL DMA2_Channel2
#define AUDIO_I2S_READ_TIMEOUT 1000000U
#define AUDIO_RECORD_USE_SOFT_I2S_RX 1U
#define AUDIO_RECORD_SOFT_AUTO_SELECT 1U
#define AUDIO_I2S_SOFT_BCLK_TIMEOUT 20000U
#define AUDIO_I2S_SOFT_LRCK_TIMEOUT 2000000U
#define AUDIO_I2S_SOFT_SAMPLE_DELAY_NOPS 8U
#define AUDIO_RECORD_ADC_SCALE_REG 0x20U
#define AUDIO_RECORD_ADC_VOLUME_REG 0xAFU
#define AUDIO_RECORD_MIC_PATH_REG 0x1AU
#define AUDIO_PLAYBACK_MIC_PATH_REG 0x1AU
#define AUDIO_RECORD_MONO_USE_RIGHT 0U
#define AUDIO_I2C_PROBE_ATTEMPTS 5U
#define AUDIO_I2C_PROBE_RETRY_MS 20U
#define AUDIO_I2C_RECOVERY_CLOCKS 18U
#define AUDIO_RECORD_SOFT_PEAK_WARN 24000U
#define AUDIO_RECORD_SOFT_PEAK_CLIP 30000U
#define AUDIO_RECORD_SOFT_IMBALANCE_RATIO 2U
#define AUDIO_RECORD_SOFT_PREFERRED_VARIANT 2U
#define AUDIO_DMA_WAIT_FREE_LIMIT 8000000U
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
static uint32_t board_audio_abs16(int16_t sample)
{
    if (sample < 0)
        return (sample <= -32768) ? 32768U : (uint32_t)(-sample);
    return (uint32_t)sample;
}

static board_audio_debug_info_t g_audio_debug;
static uint32_t board_audio_dma_used_halfwords(void);
static void board_audio_update_dma_debug(void)
{
    g_audio_debug.dma_underruns = g_audio_dma_underruns;
    g_audio_debug.dma_used_halfwords = board_audio_dma_used_halfwords();
    g_audio_debug.dma_write_index = g_audio_dma_write_index;
}
static void board_audio_capture_mcu_debug(void)
{
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

static uint8_t g_es8311_addr_write = ES8311_I2C_ADDR_WRITE_LOW;
static uint32_t g_audio_soft_i2s_variant = AUDIO_RECORD_SOFT_PREFERRED_VARIANT;

static void audio_delay_short(void)
{
    volatile int i;
    for (i = 0; i < 220; ++i)
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
    g_audio_debug.i2c_probe_low_result = 99;
    g_audio_debug.i2c_probe_high_result = 99;
    g_audio_debug.i2c_probe30_addr_ack = 99;
    g_audio_debug.i2c_probe30_reg_ack = 99;
    g_audio_debug.i2c_probe30_data_ack = 99;
    g_audio_debug.i2c_probe32_addr_ack = 99;
    g_audio_debug.i2c_probe32_reg_ack = 99;
    g_audio_debug.i2c_probe32_data_ack = 99;
}

static void i2c_line_input_pullup(uint16_t pin)
{
    GPIO_InitTypeDef gpio;

    GPIO_SetBits(AUDIO_I2C_PORT, pin);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = pin;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(AUDIO_I2C_PORT, &gpio);
    audio_delay_short();
}

static void i2c_line_drive_low(uint16_t pin)
{
    GPIO_InitTypeDef gpio;

    GPIO_ResetBits(AUDIO_I2C_PORT, pin);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(AUDIO_I2C_PORT, &gpio);
    audio_delay_short();
}

static void i2c_scl_high(void)
{
    i2c_line_input_pullup(AUDIO_I2C_SCL);
}
static void i2c_scl_low(void)
{
    i2c_line_drive_low(AUDIO_I2C_SCL);
}
static void i2c_sda_high(void)
{
    i2c_line_input_pullup(AUDIO_I2C_SDA);
}
static void i2c_sda_low(void)
{
    i2c_line_drive_low(AUDIO_I2C_SDA);
}
static int i2c_sda_read(void) { return GPIO_ReadInputDataBit(AUDIO_I2C_PORT, AUDIO_I2C_SDA); }
static int i2c_scl_read(void) { return GPIO_ReadInputDataBit(AUDIO_I2C_PORT, AUDIO_I2C_SCL); }
static void i2c_stop(void);

static void i2c_capture_bus_state(void)
{
    g_audio_debug.i2c_scl_idle = i2c_scl_read() ? 1U : 0U;
    g_audio_debug.i2c_sda_idle = i2c_sda_read() ? 1U : 0U;
}

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
    i2c_capture_bus_state();
    return result;
}

static int es8311_read_reg_raw(uint8_t reg, uint8_t *value)
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
    i2c_capture_bus_state();
    return result;
}

static int es8311_write_reg(uint8_t reg, uint8_t value)
{
    int result = es8311_write_reg_to(g_es8311_addr_write, reg, value);

    if (result != 0)
        g_audio_debug.codec_fail_reg = reg;
    return result;
}

static int es8311_write_reg_retry(uint8_t reg, uint8_t value)
{
    uint32_t attempt;
    int result = -1;

    for (attempt = 0U; attempt < 3U; ++attempt)
    {
        result = es8311_write_reg(reg, value);
        if (result == 0)
            return 0;
        audio_delay_ms(2);
    }
    return result;
}
static int es8311_read_reg(uint8_t reg, uint8_t *value)
{
    int result = es8311_read_reg_raw(reg, value);

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
    if (es8311_read_reg(0x15, &value) == 0)
        g_audio_debug.codec_reg15 = value;
    if (es8311_read_reg(0x16, &value) == 0)
        g_audio_debug.codec_reg16 = value;
    if (es8311_read_reg(0x17, &value) == 0)
        g_audio_debug.codec_reg17 = value;
    if (es8311_read_reg(0x18, &value) == 0)
        g_audio_debug.codec_reg18 = value;
    if (es8311_read_reg(0x19, &value) == 0)
        g_audio_debug.codec_reg19 = value;
    if (es8311_read_reg(0x1A, &value) == 0)
        g_audio_debug.codec_reg1a = value;
    if (es8311_read_reg(0x1B, &value) == 0)
        g_audio_debug.codec_reg1b = value;
    if (es8311_read_reg(0x1C, &value) == 0)
        g_audio_debug.codec_reg1c = value;
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
    g_audio_debug.i2c_scl_idle = GPIO_ReadInputDataBit(AUDIO_I2C_PORT, AUDIO_I2C_SCL);
    g_audio_debug.i2c_sda_idle = GPIO_ReadInputDataBit(AUDIO_I2C_PORT, AUDIO_I2C_SDA);
    board_audio_update_dma_debug();
}

static int es8311_probe_addr(uint8_t addr)
{
    int addr_ack;
    int reg_ack;
    int data_ack;

    i2c_stop();
    i2c_capture_bus_state();
    i2c_start();
    addr_ack = i2c_write_byte(addr);
    reg_ack = i2c_write_byte(0x00);
    data_ack = i2c_write_byte(0x80);
    i2c_stop();
    i2c_capture_bus_state();

    if (addr == ES8311_I2C_ADDR_WRITE_LOW)
    {
        g_audio_debug.i2c_probe30_addr_ack = addr_ack;
        g_audio_debug.i2c_probe30_reg_ack = reg_ack;
        g_audio_debug.i2c_probe30_data_ack = data_ack;
    }
    else
    {
        g_audio_debug.i2c_probe32_addr_ack = addr_ack;
        g_audio_debug.i2c_probe32_reg_ack = reg_ack;
        g_audio_debug.i2c_probe32_data_ack = data_ack;
    }

    return addr_ack | reg_ack | data_ack;
}
static int es8311_probe(void)
{
    uint32_t attempt;

    g_audio_debug.i2c_probe_low_result = 99;
    g_audio_debug.i2c_probe_high_result = 99;
    g_audio_debug.i2c_probe30_addr_ack = 99;
    g_audio_debug.i2c_probe30_reg_ack = 99;
    g_audio_debug.i2c_probe30_data_ack = 99;
    g_audio_debug.i2c_probe32_addr_ack = 99;
    g_audio_debug.i2c_probe32_reg_ack = 99;
    g_audio_debug.i2c_probe32_data_ack = 99;

    for (attempt = 0U; attempt < AUDIO_I2C_PROBE_ATTEMPTS; ++attempt)
    {
        g_audio_debug.i2c_probe_attempts = attempt + 1U;

        g_audio_debug.i2c_probe_low_result = es8311_probe_addr(ES8311_I2C_ADDR_WRITE_LOW);
        if (g_audio_debug.i2c_probe_low_result == 0)
        {
            g_es8311_addr_write = ES8311_I2C_ADDR_WRITE_LOW;
            g_audio_debug.codec_addr = g_es8311_addr_write;
            g_audio_debug.codec_fail_reg = 0U;
            return 0;
        }

        g_audio_debug.i2c_probe_high_result = es8311_probe_addr(ES8311_I2C_ADDR_WRITE_HIGH);
        if (g_audio_debug.i2c_probe_high_result == 0)
        {
            g_es8311_addr_write = ES8311_I2C_ADDR_WRITE_HIGH;
            g_audio_debug.codec_addr = g_es8311_addr_write;
            g_audio_debug.codec_fail_reg = 0U;
            return 0;
        }

        audio_delay_ms(AUDIO_I2C_PROBE_RETRY_MS);
    }

    i2c_capture_bus_state();
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
    audio_delay_ms(5);

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

static void board_audio_gpio_init_record_inputs(void)
{
    GPIO_InitTypeDef gpio;

    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = AUDIO_I2S_RX_PIN | AUDIO_I2S_SD_PIN;
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

static int board_audio_dma_wait_free(uint32_t halfwords)
{
    uint32_t stagnant = 0U;
    uint32_t last_read_total = board_audio_dma_update_read_total();

    while (board_audio_dma_free_halfwords() < halfwords)
    {
        uint32_t read_total = board_audio_dma_update_read_total();

        if (read_total != last_read_total)
        {
            stagnant = 0U;
            last_read_total = read_total;
        }
        else if (++stagnant > AUDIO_DMA_WAIT_FREE_LIMIT)
        {
            g_audio_dma_underruns++;
            board_audio_set_error(-10);
            board_audio_update_dma_debug();
            return -1;
        }
        board_audio_dma_recover_if_underrun();
        __NOP();
    }
    return 0;
}

static int board_audio_dma_write_frame(int16_t left, int16_t right)
{
    if (!g_audio_dma_started)
    {
        board_audio_write_frame(left, right);
        return 0;
    }

    if (board_audio_dma_wait_free(2U) != 0)
        return -1;
    board_audio_dma_recover_if_underrun();
    g_audio_dma_buffer[g_audio_dma_write_index] = left;
    g_audio_dma_write_index = (g_audio_dma_write_index + 1U) % AUDIO_DMA_BUFFER_HALFWORDS;
    g_audio_dma_write_total++;
    g_audio_dma_buffer[g_audio_dma_write_index] = right;
    g_audio_dma_write_index = (g_audio_dma_write_index + 1U) % AUDIO_DMA_BUFFER_HALFWORDS;
    g_audio_dma_write_total++;
    g_audio_debug.tx_frames++;
    return 0;
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

static void board_audio_i2s_stop_quiet(void)
{
    board_audio_dma_stop();
    SPI_I2S_DMACmd(SPI3, SPI_I2S_DMAReq_Tx, DISABLE);
    SPI_I2S_DMACmd(SPI3, SPI_I2S_DMAReq_Rx, DISABLE);
    I2S_Cmd(SPI3, DISABLE);
    SPI_I2S_DeInit(SPI3);
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
    uint32_t stagnant = 0U;
    uint32_t last_read_total = board_audio_dma_update_read_total();

    while (g_audio_dma_started && g_audio_dma_write_total > g_audio_dma_read_total)
    {
        uint32_t read_total = board_audio_dma_update_read_total();

        if (read_total != last_read_total)
        {
            stagnant = 0U;
            last_read_total = read_total;
        }
        else if (++stagnant > 8000000U)
        {
            break;
        }
        __NOP();
    }
    board_audio_update_dma_debug();
}
static uint32_t board_audio_i2s_freq(uint32_t sample_rate)
{
    if (sample_rate <= 9500U)
        return I2S_AudioFreq_8k;
    if (sample_rate <= 13500U)
        return I2S_AudioFreq_11k;
    if (sample_rate <= 19000U)
        return I2S_AudioFreq_16k;
    if (sample_rate <= 27000U)
        return I2S_AudioFreq_22k;
    if (sample_rate <= 38000U)
        return I2S_AudioFreq_32k;
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
    g_audio_debug.spi3_i2spr = SPI3->I2SPR;
    g_audio_debug.spi3_cr2 = SPI3->CR2;
}

static void board_audio_i2s_init_tx(void)
{
    board_audio_i2s_init_tx_rate(AUDIO_SAMPLE_RATE);
}

static uint32_t board_audio_i2s_record_freq(uint32_t sample_rate)
{
    return board_audio_i2s_freq(sample_rate);
}
static void board_audio_i2s_init_rx_rate(uint32_t sample_rate)
{
    I2S_InitTypeDef i2s;

    board_audio_dma_stop();
    SPI_I2S_DeInit(SPI3);
    i2s.I2S_Mode = I2S_Mode_MasterRx;
    i2s.I2S_Standard = I2S_Standard_Phillips;
    i2s.I2S_DataFormat = I2S_DataFormat_16b;
    i2s.I2S_MCLKOutput = I2S_MCLKOutput_Enable;
    i2s.I2S_AudioFreq = board_audio_i2s_record_freq(sample_rate);
    i2s.I2S_CPOL = I2S_CPOL_Low;
    I2S_Init(SPI3, &i2s);
    I2S_Cmd(SPI3, ENABLE);
    g_audio_debug.last_sample_rate = sample_rate;
    g_audio_debug.spi3_sr = SPI3->SR;
    g_audio_debug.spi3_i2scfgr = SPI3->I2SCFGR;
    g_audio_debug.spi3_i2spr = SPI3->I2SPR;
    g_audio_debug.spi3_cr2 = SPI3->CR2;
}

static int board_audio_playback_key_ready(void)
{
    return (g_audio_debug.codec_addr != 0U) &&
           ((g_audio_debug.codec_reg01 & 0x3FU) == 0x3FU) &&
           ((g_audio_debug.codec_reg09 & 0x3FU) == 0x0CU);
}

static int board_audio_record_key_ready(void)
{
    return (g_audio_debug.codec_addr != 0U) &&
           ((g_audio_debug.codec_reg01 & 0x3FU) == 0x3FU) &&
           ((g_audio_debug.codec_reg0a & 0x3FU) == 0x0CU) &&
           ((g_audio_debug.codec_reg0d & 0xF0U) == 0U) &&
           ((g_audio_debug.codec_reg0e & 0x70U) == 0U) &&
           ((g_audio_debug.codec_reg14 & 0x40U) == 0U) &&
           ((g_audio_debug.codec_reg14 & 0x10U) != 0U) &&
           ((g_audio_debug.codec_reg07 & 0x10U) == 0U) &&
           (((g_audio_debug.codec_reg44 & 0x70U) == 0U) || ((g_audio_debug.codec_reg44 & 0x70U) == 0x50U));
}
static int board_audio_codec_init_playback(void)
{
    int result = 0;
    uint32_t init_fail_reg;

    if (es8311_probe() != 0)
        return -1;

    result |= es8311_write_reg(0x0D, 0xFA);
    result |= es8311_write_reg(0x44, 0x08);
    result |= es8311_write_reg(0x44, 0x08);
    result |= es8311_write_reg(0x00, 0x80);
    audio_delay_ms(5);
    result |= es8311_write_reg(0x00, 0x80);
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
    result |= es8311_write_reg(0x0A, 0x0C);
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
    result |= es8311_write_reg(0x14, AUDIO_PLAYBACK_MIC_PATH_REG);
    result |= es8311_write_reg(0x0D, 0x06);
    audio_delay_ms(30);
    (void)es8311_write_reg(0x25, 0x00);
    result |= es8311_write_reg(0x01, 0x3F);

    init_fail_reg = g_audio_debug.codec_fail_reg;
    board_audio_capture_debug_regs();
    if (result == 0 || board_audio_playback_key_ready())
    {
        g_audio_debug.codec_fail_reg = 0U;
        return 0;
    }

    g_audio_debug.codec_fail_reg = init_fail_reg;
    return result;
}

static int board_audio_codec_init_record(void)
{
    int result = 0;
    uint32_t init_fail_reg;

    if (es8311_probe() != 0)
        return -1;

    result |= es8311_write_reg(0x0D, 0xFA);
    result |= es8311_write_reg(0x44, 0x08);
    result |= es8311_write_reg(0x44, 0x08);
    result |= es8311_write_reg(0x00, 0x80);
    audio_delay_ms(5);
    result |= es8311_write_reg(0x00, 0x80);
    result |= es8311_write_reg(0x01, 0x30);
    result |= es8311_write_reg(0x02, 0x00);
    result |= es8311_write_reg(0x03, 0x10);
    result |= es8311_write_reg(0x04, 0x20);
    result |= es8311_write_reg(0x05, 0x00);
    result |= es8311_write_reg(0x06, 0x03);
    result |= es8311_write_reg(0x07, 0x00);
    result |= es8311_write_reg(0x08, 0xFF);
    result |= es8311_write_reg(0x09, 0x0C);
    result |= es8311_write_reg(0x0A, 0x0C);
    result |= es8311_write_reg(0x0B, 0x00);
    result |= es8311_write_reg(0x0C, 0x00);
    result |= es8311_write_reg(0x0D, 0x01);
    audio_delay_ms(120);
    result |= es8311_write_reg(0x0E, 0x02);
    result |= es8311_write_reg(0x0F, 0x00);
    result |= es8311_write_reg(0x12, 0x00);
    result |= es8311_write_reg(0x13, 0x10);
    result |= es8311_write_reg(0x14, AUDIO_RECORD_MIC_PATH_REG);
    result |= es8311_write_reg(0x15, 0x40);
    result |= es8311_write_reg(0x16, AUDIO_RECORD_ADC_SCALE_REG);
    result |= es8311_write_reg(0x17, AUDIO_RECORD_ADC_VOLUME_REG);
    result |= es8311_write_reg(0x18, 0x00);
    result |= es8311_write_reg(0x19, 0x00);
    result |= es8311_write_reg(0x1A, 0x00);
    result |= es8311_write_reg(0x1B, 0x0A);
    result |= es8311_write_reg(0x1C, 0x6A);
    result |= es8311_write_reg(0x31, 0x00);
    result |= es8311_write_reg(0x32, 0xBF);
    result |= es8311_write_reg(0x33, 0x00);
    result |= es8311_write_reg(0x34, 0x00);
    result |= es8311_write_reg(0x37, 0x08);
    result |= es8311_write_reg(0x44, 0x00);
    result |= es8311_write_reg(0x0D, 0x01);
    audio_delay_ms(30);
    result |= es8311_write_reg(0x01, 0x3F);

    init_fail_reg = g_audio_debug.codec_fail_reg;
    board_audio_capture_debug_regs();
    if (result == 0 || board_audio_record_key_ready())
    {
        g_audio_debug.codec_fail_reg = 0U;
        return 0;
    }

    g_audio_debug.codec_fail_reg = init_fail_reg;
    return result;
}

static void board_audio_write_word(int16_t value)
{
    while (SPI_I2S_GetFlagStatus(SPI3, SPI_I2S_FLAG_TXE) == RESET)
    {
    }
    SPI_I2S_SendData(SPI3, (uint16_t)value);
}

#if !AUDIO_RECORD_USE_SOFT_I2S_RX
static void board_audio_clear_rx_overrun(void)
{
    volatile uint16_t discard;

    if (SPI_I2S_GetFlagStatus(SPI3, SPI_I2S_FLAG_OVR) != RESET)
    {
        discard = SPI_I2S_ReceiveData(SPI3);
        discard = SPI3->SR;
        (void)discard;
        g_audio_debug.rx_overruns++;
    }
}

static int board_audio_read_word(int16_t *value)
{
    uint32_t timeout = AUDIO_I2S_READ_TIMEOUT;

    board_audio_clear_rx_overrun();

    while (SPI_I2S_GetFlagStatus(SPI3, SPI_I2S_FLAG_RXNE) == RESET)
    {
        if (timeout-- == 0U)
        {
            *value = 0;
            g_audio_debug.rx_read_timeouts++;
            board_audio_set_error(-7);
            return -1;
        }
    }

    *value = (int16_t)SPI_I2S_ReceiveData(SPI3);
    return 0;
}

#endif

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
    board_audio_i2s_stop_quiet();
    audio_delay_ms(10);
    if (board_audio_codec_init_playback() != 0)
    {
        board_audio_capture_mcu_debug();
        board_audio_set_error(-1);
        return -1;
    }
    audio_delay_ms(10);
    board_audio_i2s_init_tx();
    audio_delay_ms(20);
    AUDIO_AMP_ENABLE();
    board_audio_capture_mcu_debug();
    return 0;
}

int board_audio_init_record_rate(uint32_t sample_rate)
{
    if (sample_rate == 0U)
        sample_rate = AUDIO_SAMPLE_RATE;

    board_audio_gpio_init();
    board_audio_reset_debug(AUDIO_MODE_RECORD);
    g_audio_debug.last_sample_rate = sample_rate;
    AUDIO_AMP_DISABLE();
    board_audio_i2s_stop_quiet();
    board_audio_gpio_init_record_inputs();
    audio_delay_ms(10);
    if (board_audio_codec_init_record() != 0)
    {
        board_audio_capture_mcu_debug();
        board_audio_set_error(-2);
        return -1;
    }
    audio_delay_ms(10);
    board_audio_i2s_init_rx_rate(sample_rate);
    board_audio_gpio_init_record_inputs();
    audio_delay_ms(20);
    AUDIO_AMP_DISABLE();
    board_audio_capture_debug_regs();
    return 0;
}

int board_audio_init_record(void)
{
    return board_audio_init_record_rate(AUDIO_SAMPLE_RATE);
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
    g_audio_debug.i2c_scl_idle = GPIO_ReadInputDataBit(AUDIO_I2C_PORT, AUDIO_I2C_SCL);
    g_audio_debug.i2c_sda_idle = GPIO_ReadInputDataBit(AUDIO_I2C_PORT, AUDIO_I2C_SDA);
    board_audio_update_dma_debug();

    if (channels == 1U)
    {
        for (i = 0; i < samples; ++i)
        {
            if (board_audio_dma_write_frame(board_audio_apply_volume(pcm[i]), board_audio_apply_volume(pcm[i])) != 0)
                return -1;
        }
        board_audio_update_dma_debug();
        return 0;
    }

    if (channels == 2U)
    {
        for (i = 0; i < samples; ++i)
        {
            int32_t mixed = ((int32_t)pcm[i * 2U] + (int32_t)pcm[i * 2U + 1U]) / 2;
            int16_t sample = board_audio_apply_volume((int16_t)mixed);
            if (board_audio_dma_write_frame(sample, sample) != 0)
                return -1;
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

void board_audio_stop_playback(void)
{
    board_audio_i2s_stop_quiet();
    AUDIO_AMP_DISABLE();
    board_audio_capture_mcu_debug();
}

static void board_audio_capture_update_raw_debug(uint32_t frame_index, int16_t left, int16_t right)
{
    uint32_t i;
    uint32_t left_abs = board_audio_abs16(left);
    uint32_t right_abs = board_audio_abs16(right);

    if (left_abs > g_audio_debug.rx_left_abs_peak)
        g_audio_debug.rx_left_abs_peak = left_abs;
    if (right_abs > g_audio_debug.rx_right_abs_peak)
        g_audio_debug.rx_right_abs_peak = right_abs;
    if (left != 0)
        g_audio_debug.rx_left_nonzero++;
    if (right != 0)
        g_audio_debug.rx_right_nonzero++;

    if (frame_index < 8U)
    {
        g_audio_debug.rx_first_left[frame_index] = left;
        g_audio_debug.rx_first_right[frame_index] = right;
    }

    for (i = 0U; i < 7U; ++i)
    {
        g_audio_debug.rx_last_left[i] = g_audio_debug.rx_last_left[i + 1U];
        g_audio_debug.rx_last_right[i] = g_audio_debug.rx_last_right[i + 1U];
    }
    g_audio_debug.rx_last_left[7] = left;
    g_audio_debug.rx_last_right[7] = right;
}

static int board_audio_soft_wait_lrck_level(int high)
{
    uint32_t timeout = AUDIO_I2S_SOFT_LRCK_TIMEOUT;
    uint32_t mask = AUDIO_I2S_WS_PIN;

    while (((AUDIO_I2S_WS_PORT->IDR & mask) != 0U) != (high != 0))
    {
        if (timeout-- == 0U)
        {
            g_audio_debug.rx_soft_lrck_timeouts++;
            board_audio_set_error(-9);
            return -1;
        }
    }
    return 0;
}

static void board_audio_soft_variant_params(uint32_t variant,
                                            GPIO_TypeDef **data_port,
                                            uint16_t *data_pin_mask,
                                            uint32_t *data_pin_number,
                                            uint32_t *skip_edges,
                                            int *sample_falling)
{
    uint32_t sub_variant;

    if (variant >= BOARD_AUDIO_SOFT_I2S_VARIANT_COUNT)
        variant = 0U;

    if (variant >= 6U)
    {
        *data_port = AUDIO_I2S_SD_PORT;
        *data_pin_mask = AUDIO_I2S_SD_PIN;
        *data_pin_number = 5U;
        sub_variant = variant - 6U;
    }
    else
    {
        *data_port = AUDIO_I2S_RX_PORT;
        *data_pin_mask = AUDIO_I2S_RX_PIN;
        *data_pin_number = 4U;
        sub_variant = variant;
    }

    *skip_edges = sub_variant / 2U;
    *sample_falling = (sub_variant & 1U) ? 1 : 0;
}

static int board_audio_soft_wait_sample_edge_inline(int sample_falling)
{
    uint32_t timeout;

    if (sample_falling)
    {
        timeout = AUDIO_I2S_SOFT_BCLK_TIMEOUT;
        while ((AUDIO_I2S_CK_PORT->IDR & AUDIO_I2S_CK_PIN) == 0U)
        {
            if (timeout-- == 0U)
            {
                g_audio_debug.rx_soft_bclk_timeouts++;
                board_audio_set_error(-8);
                return -1;
            }
        }
        timeout = AUDIO_I2S_SOFT_BCLK_TIMEOUT;
        while ((AUDIO_I2S_CK_PORT->IDR & AUDIO_I2S_CK_PIN) != 0U)
        {
            if (timeout-- == 0U)
            {
                g_audio_debug.rx_soft_bclk_timeouts++;
                board_audio_set_error(-8);
                return -1;
            }
        }
    }
    else
    {
        timeout = AUDIO_I2S_SOFT_BCLK_TIMEOUT;
        while ((AUDIO_I2S_CK_PORT->IDR & AUDIO_I2S_CK_PIN) != 0U)
        {
            if (timeout-- == 0U)
            {
                g_audio_debug.rx_soft_bclk_timeouts++;
                board_audio_set_error(-8);
                return -1;
            }
        }
        timeout = AUDIO_I2S_SOFT_BCLK_TIMEOUT;
        while ((AUDIO_I2S_CK_PORT->IDR & AUDIO_I2S_CK_PIN) == 0U)
        {
            if (timeout-- == 0U)
            {
                g_audio_debug.rx_soft_bclk_timeouts++;
                board_audio_set_error(-8);
                return -1;
            }
        }
    }
    return 0;
}

static int board_audio_soft_read_channel_variant(int16_t *value,
                                                 GPIO_TypeDef *data_port,
                                                 uint16_t data_pin_mask,
                                                 uint32_t skip_edges,
                                                 int sample_falling,
                                                 uint32_t *pin_high,
                                                 uint32_t *pin_edges)
{
    uint32_t bit;
    uint32_t word = 0U;
    uint32_t skipped;
    uint32_t last_pin = data_port->IDR & data_pin_mask;

    for (skipped = 0U; skipped < skip_edges; ++skipped)
    {
        if (board_audio_soft_wait_sample_edge_inline(sample_falling) != 0)
            return -1;
    }

    for (bit = 0U; bit < 16U; ++bit)
    {
        uint32_t pin;
        uint32_t settle;

        if (board_audio_soft_wait_sample_edge_inline(sample_falling) != 0)
            return -1;
        for (settle = 0U; settle < AUDIO_I2S_SOFT_SAMPLE_DELAY_NOPS; ++settle)
            __NOP();

        pin = data_port->IDR & data_pin_mask;
        word <<= 1;
        if (pin != 0U)
        {
            word |= 1U;
            if (pin_high != RT_NULL)
                (*pin_high)++;
        }
        if (((pin != 0U) != (last_pin != 0U)) && pin_edges != RT_NULL)
            (*pin_edges)++;
        last_pin = pin;
    }

    *value = (int16_t)(uint16_t)word;
    return 0;
}
static int board_audio_capture_soft_i2s_frame_variant(int16_t *left,
                                                      int16_t *right,
                                                      GPIO_TypeDef *data_port,
                                                      uint16_t data_pin_mask,
                                                      uint32_t skip_edges,
                                                      int sample_falling,
                                                      uint32_t *pin_high,
                                                      uint32_t *pin_edges)
{
    if (board_audio_soft_wait_lrck_level(1) != 0)
        return -1;
    if (board_audio_soft_wait_lrck_level(0) != 0)
        return -1;
    if (board_audio_soft_read_channel_variant(left, data_port, data_pin_mask, skip_edges, sample_falling, pin_high, pin_edges) != 0)
        return -1;
    if (board_audio_soft_wait_lrck_level(1) != 0)
        return -1;
    if (board_audio_soft_read_channel_variant(right, data_port, data_pin_mask, skip_edges, sample_falling, pin_high, pin_edges) != 0)
        return -1;
    return 0;
}

static int board_audio_capture_soft_i2s_frame(int16_t *left, int16_t *right)
{
    uint32_t skip_edges;
    uint32_t data_pin_number;
    uint16_t data_pin_mask;
    GPIO_TypeDef *data_port;
    int sample_falling;

    board_audio_soft_variant_params(g_audio_soft_i2s_variant, &data_port, &data_pin_mask, &data_pin_number, &skip_edges, &sample_falling);
    g_audio_debug.rx_soft_data_pin = data_pin_number;
    if (board_audio_capture_soft_i2s_frame_variant(left,
                                                   right,
                                                   data_port,
                                                   data_pin_mask,
                                                   skip_edges,
                                                   sample_falling,
                                                   &g_audio_debug.rx_soft_pin_high,
                                                   &g_audio_debug.rx_soft_pin_edges) != 0)
        return -1;

    g_audio_debug.rx_soft_frames++;
    return 0;
}
static int board_audio_soft_variant_is_clean(const board_audio_soft_i2s_variant_info_t *variant)
{
    uint32_t peak;
    uint32_t low_peak;

    if (variant == RT_NULL || variant->score == 0U || variant->data_pin != 4U)
        return 0;
    if (variant->sample_falling != 0U)
        return 0;

    peak = variant->left_abs_peak > variant->right_abs_peak ? variant->left_abs_peak : variant->right_abs_peak;
    low_peak = variant->left_abs_peak < variant->right_abs_peak ? variant->left_abs_peak : variant->right_abs_peak;

    if (peak > AUDIO_RECORD_SOFT_PEAK_CLIP || low_peak == 0U)
        return 0;
    if (peak > (low_peak * AUDIO_RECORD_SOFT_IMBALANCE_RATIO))
        return 0;
    return 1;
}

int board_audio_probe_soft_i2s_variants(board_audio_soft_i2s_variant_info_t *info, uint32_t count)
{
    uint32_t variant;
    uint32_t best_variant = g_audio_soft_i2s_variant;
    uint32_t best_score = 0U;
    const uint32_t frames_per_variant = 64U;

    if (info == RT_NULL || count < BOARD_AUDIO_SOFT_I2S_VARIANT_COUNT)
        return -1;

    memset(info, 0, sizeof(board_audio_soft_i2s_variant_info_t) * count);

    for (variant = 0U; variant < BOARD_AUDIO_SOFT_I2S_VARIANT_COUNT; ++variant)
    {
        uint32_t frame;
        uint32_t skip_edges;
        uint32_t energy_sum = 0U;
        uint32_t left_delta_sum = 0U;
        uint32_t right_delta_sum = 0U;
        uint32_t data_pin_number;
        uint16_t data_pin_mask;
        GPIO_TypeDef *data_port;
        int sample_falling;
        int16_t prev_left = 0;
        int16_t prev_right = 0;
        int have_prev = 0;
        board_audio_soft_i2s_variant_info_t *cur = &info[variant];

        board_audio_soft_variant_params(variant, &data_port, &data_pin_mask, &data_pin_number, &skip_edges, &sample_falling);
        cur->variant = variant;
        cur->data_pin = data_pin_number;
        cur->skip_edges = skip_edges;
        cur->sample_falling = (uint32_t)sample_falling;

        for (frame = 0U; frame < frames_per_variant; ++frame)
        {
            int16_t left;
            int16_t right;
            uint32_t left_abs;
            uint32_t right_abs;

            if (board_audio_capture_soft_i2s_frame_variant(&left,
                                                           &right,
                                                           data_port,
                                                           data_pin_mask,
                                                           skip_edges,
                                                           sample_falling,
                                                           &cur->pin_high,
                                                           &cur->pin_edges) != 0)
                break;

            left_abs = board_audio_abs16(left);
            right_abs = board_audio_abs16(right);
            energy_sum += left_abs + right_abs;
            if (left_abs > cur->left_abs_peak)
                cur->left_abs_peak = left_abs;
            if (right_abs > cur->right_abs_peak)
                cur->right_abs_peak = right_abs;
            if (left != 0)
                cur->left_nonzero++;
            if (right != 0)
                cur->right_nonzero++;
            if (have_prev)
            {
                int32_t left_delta = (int32_t)left - (int32_t)prev_left;
                int32_t right_delta = (int32_t)right - (int32_t)prev_right;

                if (left != prev_left)
                    cur->left_changes++;
                if (right != prev_right)
                    cur->right_changes++;
                if (left_delta < 0)
                    left_delta = -left_delta;
                if (right_delta < 0)
                    right_delta = -right_delta;
                left_delta_sum += (uint32_t)left_delta;
                right_delta_sum += (uint32_t)right_delta;
            }
            else
            {
                cur->first_left = left;
                cur->first_right = right;
                have_prev = 1;
            }
            prev_left = left;
            prev_right = right;
            cur->last_left = left;
            cur->last_right = right;
        }

        if (frame < frames_per_variant || (cur->left_nonzero + cur->right_nonzero) == 0U || cur->pin_edges == 0U)
        {
            cur->score = 0U;
        }
        else
        {
            uint32_t delta_sum = left_delta_sum + right_delta_sum;
            uint32_t peak = cur->left_abs_peak > cur->right_abs_peak ? cur->left_abs_peak : cur->right_abs_peak;
            uint32_t low_peak = cur->left_abs_peak < cur->right_abs_peak ? cur->left_abs_peak : cur->right_abs_peak;
            uint32_t smooth_score = (energy_sum * 32U) / (delta_sum + 1U);

            cur->score = smooth_score + cur->left_nonzero + cur->right_nonzero + (cur->pin_edges / 16U);
            if (peak > AUDIO_RECORD_SOFT_PEAK_CLIP)
                cur->score /= 8U;
            else if (peak > AUDIO_RECORD_SOFT_PEAK_WARN)
                cur->score /= 2U;
            if (low_peak > 0U && peak > (low_peak * AUDIO_RECORD_SOFT_IMBALANCE_RATIO))
                cur->score /= 2U;
            if (cur->left_nonzero < (frames_per_variant / 2U) || cur->right_nonzero < (frames_per_variant / 2U))
                cur->score /= 2U;
            if (energy_sum > 0U && delta_sum > (energy_sum * 4U))
                cur->score /= 2U;
        }

        if (board_audio_soft_variant_is_clean(cur) && cur->score > best_score)
        {
            best_score = cur->score;
            best_variant = variant;
        }
    }

    if (AUDIO_RECORD_SOFT_PREFERRED_VARIANT < count &&
        board_audio_soft_variant_is_clean(&info[AUDIO_RECORD_SOFT_PREFERRED_VARIANT]))
    {
        best_variant = AUDIO_RECORD_SOFT_PREFERRED_VARIANT;
        best_score = info[AUDIO_RECORD_SOFT_PREFERRED_VARIANT].score;
    }
#if AUDIO_RECORD_SOFT_AUTO_SELECT
    if (best_score > 0U)
        g_audio_soft_i2s_variant = best_variant;
#else
    (void)best_variant;
    (void)best_score;
#endif
    return 0;
}
uint32_t board_audio_get_soft_i2s_variant(void)
{
    return g_audio_soft_i2s_variant;
}

void board_audio_scan_i2s_pins(uint32_t samples)
{
    uint32_t i;
    uint32_t last_mclk;
    uint32_t last_bclk;
    uint32_t last_lrck;
    uint32_t last_pb4;
    uint32_t last_pb5;

    if (samples == 0U)
        samples = 4096U;

    g_audio_debug.pin_scan_samples = samples;
    g_audio_debug.pin_scan_mclk_high = 0U;
    g_audio_debug.pin_scan_mclk_edges = 0U;
    g_audio_debug.pin_scan_bclk_high = 0U;
    g_audio_debug.pin_scan_bclk_edges = 0U;
    g_audio_debug.pin_scan_lrck_high = 0U;
    g_audio_debug.pin_scan_lrck_edges = 0U;
    g_audio_debug.pin_scan_pb4_high = 0U;
    g_audio_debug.pin_scan_pb4_edges = 0U;
    g_audio_debug.pin_scan_pb5_high = 0U;
    g_audio_debug.pin_scan_pb5_edges = 0U;

    last_mclk = AUDIO_I2S_MCLK_PORT->IDR & AUDIO_I2S_MCLK_PIN;
    last_bclk = AUDIO_I2S_CK_PORT->IDR & AUDIO_I2S_CK_PIN;
    last_lrck = AUDIO_I2S_WS_PORT->IDR & AUDIO_I2S_WS_PIN;
    last_pb4 = AUDIO_I2S_RX_PORT->IDR & AUDIO_I2S_RX_PIN;
    last_pb5 = AUDIO_I2S_SD_PORT->IDR & AUDIO_I2S_SD_PIN;

    for (i = 0U; i < samples; ++i)
    {
        uint32_t mclk = AUDIO_I2S_MCLK_PORT->IDR & AUDIO_I2S_MCLK_PIN;
        uint32_t bclk = AUDIO_I2S_CK_PORT->IDR & AUDIO_I2S_CK_PIN;
        uint32_t lrck = AUDIO_I2S_WS_PORT->IDR & AUDIO_I2S_WS_PIN;
        uint32_t pb4 = AUDIO_I2S_RX_PORT->IDR & AUDIO_I2S_RX_PIN;
        uint32_t pb5 = AUDIO_I2S_SD_PORT->IDR & AUDIO_I2S_SD_PIN;

        if (mclk != 0U)
            g_audio_debug.pin_scan_mclk_high++;
        if (bclk != 0U)
            g_audio_debug.pin_scan_bclk_high++;
        if (lrck != 0U)
            g_audio_debug.pin_scan_lrck_high++;
        if (pb4 != 0U)
            g_audio_debug.pin_scan_pb4_high++;
        if (pb5 != 0U)
            g_audio_debug.pin_scan_pb5_high++;

        if ((mclk != 0U) != (last_mclk != 0U))
            g_audio_debug.pin_scan_mclk_edges++;
        if ((bclk != 0U) != (last_bclk != 0U))
            g_audio_debug.pin_scan_bclk_edges++;
        if ((lrck != 0U) != (last_lrck != 0U))
            g_audio_debug.pin_scan_lrck_edges++;
        if ((pb4 != 0U) != (last_pb4 != 0U))
            g_audio_debug.pin_scan_pb4_edges++;
        if ((pb5 != 0U) != (last_pb5 != 0U))
            g_audio_debug.pin_scan_pb5_edges++;

        last_mclk = mclk;
        last_bclk = bclk;
        last_lrck = lrck;
        last_pb4 = pb4;
        last_pb5 = pb5;
    }

    board_audio_capture_mcu_debug();
}
static int16_t board_audio_select_mono_sample(int16_t left, int16_t right)
{
    uint32_t left_abs = board_audio_abs16(left);
    uint32_t right_abs = board_audio_abs16(right);

    if (left_abs == 0U && right_abs != 0U)
        return right;
    if (left_abs > AUDIO_RECORD_SOFT_PEAK_WARN && right_abs > 0U &&
        left_abs > (right_abs * AUDIO_RECORD_SOFT_IMBALANCE_RATIO))
        return right;
    return left;
}
int board_audio_capture_mono_sample(int16_t *sample)
{
    uint32_t frame_index;
    int16_t left;
    int16_t right;

    if (sample == RT_NULL)
    {
        board_audio_set_error(-5);
        return -1;
    }

    g_audio_debug.last_channels = 1U;
    frame_index = g_audio_debug.rx_frames;
#if AUDIO_RECORD_USE_SOFT_I2S_RX
    if (board_audio_capture_soft_i2s_frame(&left, &right) != 0)
        return -3;
#else
    if (board_audio_read_word(&left) != 0 || board_audio_read_word(&right) != 0)
        return -3;
#endif
    board_audio_capture_update_raw_debug(frame_index, left, right);
#if AUDIO_RECORD_MONO_USE_RIGHT
    *sample = right;
#else
    *sample = board_audio_select_mono_sample(left, right);
#endif
    g_audio_debug.rx_frames++;
    return 0;
}

int board_audio_capture_pcm(int16_t *pcm, uint32_t samples, uint32_t channels)
{
    uint32_t i;
    uint32_t frame_index;
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
        frame_index = g_audio_debug.rx_frames;
#if AUDIO_RECORD_USE_SOFT_I2S_RX
        if (board_audio_capture_soft_i2s_frame(&left, &right) != 0)
            return -3;
#else
        if (board_audio_read_word(&left) != 0 || board_audio_read_word(&right) != 0)
            return -3;
#endif
        board_audio_capture_update_raw_debug(frame_index, left, right);
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
    uint32_t rate = g_audio_debug.last_sample_rate;
    uint32_t total_samples;
    uint32_t index;

    if (rate == 0U)
        rate = AUDIO_SAMPLE_RATE;
    total_samples = (rate * duration_ms) / 1000U;

    for (index = 0; index < total_samples; ++index)
    {
        if (board_audio_dma_write_frame(board_audio_apply_volume(audio_test_wave[index % AUDIO_TEST_TABLE_SIZE]), board_audio_apply_volume(audio_test_wave[index % AUDIO_TEST_TABLE_SIZE])) != 0)
            return -1;
    }
    return 0;
}

int board_audio_adc_dac_monitor(uint32_t duration_ms)
{
    uint32_t rate = g_audio_debug.last_sample_rate;
    uint32_t total_samples;
    uint32_t index;
    uint8_t old_reg44 = 0U;
    uint8_t old_reg32 = 0U;
    int result = 0;

    if (rate == 0U)
        rate = AUDIO_SAMPLE_RATE;
    total_samples = (rate * duration_ms) / 1000U;

    if (es8311_read_reg(0x44, &old_reg44) != 0)
        return -1;
    if (es8311_read_reg(0x32, &old_reg32) != 0)
        return -1;
    result |= es8311_write_reg_retry(0x32, AUDIO_MONITOR_DAC_VOLUME_REG);
    result |= es8311_write_reg_retry(0x44, (uint8_t)(old_reg44 | 0x80U));
    audio_delay_ms(20);
    AUDIO_AMP_ENABLE();
    board_audio_capture_debug_regs();

    if (g_audio_debug.last_mode == AUDIO_MODE_RECORD)
    {
        audio_delay_ms(duration_ms);
    }
    else
    {
        for (index = 0; index < total_samples; ++index)
        {
            if (board_audio_dma_write_frame(0, 0) != 0)
            {
                result = -1;
                break;
            }
        }
        board_audio_dma_drain();
    }

    result |= es8311_write_reg_retry(0x44, (uint8_t)(old_reg44 & (uint8_t)~0x80U));
    result |= es8311_write_reg_retry(0x32, old_reg32);
    AUDIO_AMP_DISABLE();
    board_audio_capture_debug_regs();
    return result == 0 ? 0 : -1;
}
int board_audio_disable_adc_dac_monitor(void)
{
    uint8_t reg44 = 0U;
    int result;

    result = es8311_read_reg(0x44, &reg44);
    if (result != 0)
        reg44 = 0U;
    result |= es8311_write_reg_retry(0x44, (uint8_t)(reg44 & (uint8_t)~0x80U));
    board_audio_capture_debug_regs();
    return result == 0 ? 0 : -1;
}
uint32_t board_audio_get_playback_volume_percent(void)
{
    return AUDIO_PLAYBACK_VOLUME_PERCENT;
}

uint32_t board_audio_get_monitor_dac_volume_reg(void)
{
    return AUDIO_MONITOR_DAC_VOLUME_REG;
}
uint32_t board_audio_get_record_adc_volume_reg(void)
{
    return AUDIO_RECORD_ADC_VOLUME_REG;
}

uint32_t board_audio_get_record_adc_scale_reg(void)
{
    return AUDIO_RECORD_ADC_SCALE_REG;
}

uint32_t board_audio_get_record_mic_path_reg(void)
{
    return AUDIO_RECORD_MIC_PATH_REG;
}
const board_audio_debug_info_t *board_audio_get_debug_info(void)
{
    return &g_audio_debug;
}
