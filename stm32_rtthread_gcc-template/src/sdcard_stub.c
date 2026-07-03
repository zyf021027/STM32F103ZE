#include "sdcard_stub.h"

#include "stm32f10x.h"

#define SD_CMD_GO_IDLE_STATE          0U
#define SD_CMD_SEND_IF_COND           8U
#define SD_CMD_APP_CMD               55U
#define SD_CMD_SD_APP_OP_COND        41U
#define SD_CMD_ALL_SEND_CID           2U
#define SD_CMD_SET_REL_ADDR           3U
#define SD_CMD_SEND_CSD               9U
#define SD_CMD_SELECT_CARD            7U
#define SD_CMD_SET_BLOCKLEN          16U
#define SD_CMD_READ_SINGLE_BLOCK     17U
#define SD_CMD_APP_SET_BUS_WIDTH      6U

#define SD_CHECK_PATTERN           0x000001AAUL
#define SD_VOLTAGE_WINDOW          0x80100000UL
#define SD_HIGH_CAPACITY           0x40000000UL
#define SD_DEFAULT_BLOCK_LEN       512U
#define SD_INIT_RETRY_COUNT        1000U
#define SD_CMD_TIMEOUT             0x00FFFFFFUL
#define SD_DATA_TIMEOUT            0x0FFFFFFFUL

static uint32_t g_sd_rca;
static int g_sd_ready;
static int g_sd_high_capacity;
static const char *g_sd_last_error = "not initialized";

static void sd_delay(volatile uint32_t count)
{
    while (count--)
    {
        __NOP();
    }
}

static void sd_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_SDIO, ENABLE);

    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12;
    GPIO_Init(GPIOC, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_2;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOD, &gpio);
}

static void sd_sdio_clock_config(uint8_t div, uint32_t bus_width)
{
    SDIO_InitTypeDef sdio;

    sdio.SDIO_ClockDiv = div;
    sdio.SDIO_ClockEdge = SDIO_ClockEdge_Rising;
    sdio.SDIO_ClockBypass = SDIO_ClockBypass_Disable;
    sdio.SDIO_ClockPowerSave = SDIO_ClockPowerSave_Disable;
    sdio.SDIO_BusWide = bus_width;
    sdio.SDIO_HardwareFlowControl = SDIO_HardwareFlowControl_Disable;
    SDIO_Init(&sdio);
    SDIO_SetPowerState(SDIO_PowerState_ON);
    SDIO_ClockCmd(ENABLE);
}

static void sd_send_command(uint32_t arg, uint32_t cmd_idx, uint32_t resp)
{
    SDIO_CmdInitTypeDef cmd;

    cmd.SDIO_Argument = arg;
    cmd.SDIO_CmdIndex = cmd_idx;
    cmd.SDIO_Response = resp;
    cmd.SDIO_Wait = SDIO_Wait_No;
    cmd.SDIO_CPSM = SDIO_CPSM_Enable;
    SDIO_SendCommand(&cmd);
}

static int sd_wait_cmd_response(uint32_t cmd_idx)
{
    uint32_t timeout = SD_CMD_TIMEOUT;

    while (timeout--)
    {
        uint32_t sta = SDIO->STA;
        if (sta & SDIO_FLAG_CTIMEOUT)
        {
            SDIO_ClearFlag(SDIO_FLAG_CTIMEOUT);
            g_sd_last_error = "cmd timeout";
            return SDCARD_ERR_TIMEOUT;
        }
        if (sta & SDIO_FLAG_CCRCFAIL)
        {
            SDIO_ClearFlag(SDIO_FLAG_CCRCFAIL);
            g_sd_last_error = "cmd crc fail";
            return SDCARD_ERR_RESP;
        }
        if (sta & SDIO_FLAG_CMDREND)
        {
            SDIO_ClearFlag(SDIO_FLAG_CMDREND);
            if (SDIO_GetCommandResponse() != cmd_idx)
            {
                g_sd_last_error = "unexpected cmd response";
                return SDCARD_ERR_RESP;
            }
            return SDCARD_OK;
        }
        if (sta & SDIO_FLAG_CMDSENT)
        {
            SDIO_ClearFlag(SDIO_FLAG_CMDSENT);
            return SDCARD_OK;
        }
    }

    g_sd_last_error = "cmd wait timeout";
    return SDCARD_ERR_TIMEOUT;
}

static int sd_cmd0(void)
{
    sd_send_command(0, SD_CMD_GO_IDLE_STATE, SDIO_Response_No);
    return sd_wait_cmd_response(SD_CMD_GO_IDLE_STATE);
}

static int sd_cmd8(void)
{
    uint32_t r;

    sd_send_command(SD_CHECK_PATTERN, SD_CMD_SEND_IF_COND, SDIO_Response_Short);
    if (sd_wait_cmd_response(SD_CMD_SEND_IF_COND) != SDCARD_OK)
        return SDCARD_ERR_UNSUPPORTED;

    r = SDIO_GetResponse(SDIO_RESP1);
    if ((r & 0xFFFU) != 0x1AAU)
    {
        g_sd_last_error = "cmd8 pattern mismatch";
        return SDCARD_ERR_UNSUPPORTED;
    }
    return SDCARD_OK;
}

static int sd_acmd41_init(void)
{
    uint32_t retry;

    for (retry = 0; retry < SD_INIT_RETRY_COUNT; ++retry)
    {
        sd_send_command(0, SD_CMD_APP_CMD, SDIO_Response_Short);
        if (sd_wait_cmd_response(SD_CMD_APP_CMD) != SDCARD_OK)
            return SDCARD_ERR_RESP;

        sd_send_command(SD_VOLTAGE_WINDOW | SD_HIGH_CAPACITY, SD_CMD_SD_APP_OP_COND, SDIO_Response_Short);
        if (sd_wait_cmd_response(SD_CMD_SD_APP_OP_COND) != SDCARD_OK)
            return SDCARD_ERR_RESP;

        if (SDIO_GetResponse(SDIO_RESP1) & 0x80000000UL)
        {
            g_sd_high_capacity = ((SDIO_GetResponse(SDIO_RESP1) & SD_HIGH_CAPACITY) != 0U);
            return SDCARD_OK;
        }
        sd_delay(2000);
    }

    g_sd_last_error = "acmd41 init timeout";
    return SDCARD_ERR_TIMEOUT;
}

static int sd_get_rca(void)
{
    sd_send_command(0, SD_CMD_SET_REL_ADDR, SDIO_Response_Short);
    if (sd_wait_cmd_response(SD_CMD_SET_REL_ADDR) != SDCARD_OK)
        return SDCARD_ERR_RESP;

    g_sd_rca = SDIO_GetResponse(SDIO_RESP1) & 0xFFFF0000UL;
    if (g_sd_rca == 0U)
    {
        g_sd_last_error = "invalid rca";
        return SDCARD_ERR_RESP;
    }
    return SDCARD_OK;
}

static int sd_select_card(void)
{
    sd_send_command(g_sd_rca, SD_CMD_SELECT_CARD, SDIO_Response_Short);
    return sd_wait_cmd_response(SD_CMD_SELECT_CARD);
}

static int sd_set_bus_width_4bit(void)
{
    sd_send_command(g_sd_rca, SD_CMD_APP_CMD, SDIO_Response_Short);
    if (sd_wait_cmd_response(SD_CMD_APP_CMD) != SDCARD_OK)
        return SDCARD_ERR_RESP;

    sd_send_command(2, SD_CMD_APP_SET_BUS_WIDTH, SDIO_Response_Short);
    if (sd_wait_cmd_response(SD_CMD_APP_SET_BUS_WIDTH) != SDCARD_OK)
        return SDCARD_ERR_RESP;

    sd_sdio_clock_config(1U, SDIO_BusWide_4b);
    return SDCARD_OK;
}

static int sd_set_block_length(void)
{
    if (g_sd_high_capacity)
        return SDCARD_OK;

    sd_send_command(SD_DEFAULT_BLOCK_LEN, SD_CMD_SET_BLOCKLEN, SDIO_Response_Short);
    return sd_wait_cmd_response(SD_CMD_SET_BLOCKLEN);
}

static int sd_read_data_polling(uint8_t *buffer)
{
    uint32_t idx = 0;
    uint32_t sta;

    while (idx < SDCARD_SECTOR_SIZE)
    {
        sta = SDIO->STA;

        if (sta & SDIO_FLAG_RXDAVL)
        {
            uint32_t word = SDIO_ReadData();
            buffer[idx++] = (uint8_t)(word & 0xFFU);
            buffer[idx++] = (uint8_t)((word >> 8) & 0xFFU);
            buffer[idx++] = (uint8_t)((word >> 16) & 0xFFU);
            buffer[idx++] = (uint8_t)((word >> 24) & 0xFFU);
            continue;
        }

        if (sta & SDIO_FLAG_DATAEND)
        {
            SDIO_ClearFlag(SDIO_FLAG_DATAEND);
            break;
        }

        if (sta & (SDIO_FLAG_DTIMEOUT | SDIO_FLAG_DCRCFAIL | SDIO_FLAG_RXOVERR | SDIO_FLAG_STBITERR))
        {
            SDIO_ClearFlag(SDIO_FLAG_DTIMEOUT | SDIO_FLAG_DCRCFAIL | SDIO_FLAG_RXOVERR | SDIO_FLAG_STBITERR);
            g_sd_last_error = "data read error";
            return SDCARD_ERR_IO;
        }
    }

    return idx == SDCARD_SECTOR_SIZE ? SDCARD_OK : SDCARD_ERR_IO;
}

int sdcard_stub_init(void)
{
    SDIO_DeInit();
    g_sd_ready = 0;
    g_sd_rca = 0;
    g_sd_high_capacity = 0;
    g_sd_last_error = "init start";

    sd_gpio_init();
    sd_sdio_clock_config(118U, SDIO_BusWide_1b);

    if (sd_cmd0() != SDCARD_OK)
        return SDCARD_ERR_TIMEOUT;

    if (sd_cmd8() != SDCARD_OK)
        return SDCARD_ERR_UNSUPPORTED;

    if (sd_acmd41_init() != SDCARD_OK)
        return SDCARD_ERR_TIMEOUT;

    if (sd_get_rca() != SDCARD_OK)
        return SDCARD_ERR_RESP;

    if (sd_select_card() != SDCARD_OK)
        return SDCARD_ERR_RESP;

    if (sd_set_block_length() != SDCARD_OK)
        return SDCARD_ERR_RESP;

    if (sd_set_bus_width_4bit() != SDCARD_OK)
        return SDCARD_ERR_RESP;

    g_sd_ready = 1;
    g_sd_last_error = "ok";
    return SDCARD_OK;
}

int sdcard_stub_is_ready(void)
{
    return g_sd_ready;
}

int sdcard_stub_read_sector(uint32_t sector, uint8_t *buffer)
{
    SDIO_DataInitTypeDef data;
    uint32_t address = g_sd_high_capacity ? sector : (sector * SDCARD_SECTOR_SIZE);

    if (!g_sd_ready)
        return SDCARD_ERR_NOT_READY;

    data.SDIO_DataTimeOut = SD_DATA_TIMEOUT;
    data.SDIO_DataLength = SDCARD_SECTOR_SIZE;
    data.SDIO_DataBlockSize = SDIO_DataBlockSize_512b;
    data.SDIO_TransferDir = SDIO_TransferDir_ToSDIO;
    data.SDIO_TransferMode = SDIO_TransferMode_Block;
    data.SDIO_DPSM = SDIO_DPSM_Enable;
    SDIO_DataConfig(&data);

    sd_send_command(address, SD_CMD_READ_SINGLE_BLOCK, SDIO_Response_Short);
    if (sd_wait_cmd_response(SD_CMD_READ_SINGLE_BLOCK) != SDCARD_OK)
        return SDCARD_ERR_RESP;

    return sd_read_data_polling(buffer);
}

const char *sdcard_stub_last_error_string(void)
{
    return g_sd_last_error;
}