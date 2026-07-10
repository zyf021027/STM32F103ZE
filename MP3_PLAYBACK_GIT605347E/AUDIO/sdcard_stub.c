#include "sdcard_stub.h"

#include "stm32f10x.h"
#include <stdio.h>

#define SD_CMD_GO_IDLE_STATE 0U
#define SD_CMD_SEND_IF_COND 8U
#define SD_CMD_APP_CMD 55U
#define SD_CMD_SD_APP_OP_COND 41U
#define SD_CMD_ALL_SEND_CID 2U
#define SD_CMD_SET_REL_ADDR 3U
#define SD_CMD_SEND_CSD 9U
#define SD_CMD_SELECT_CARD 7U
#define SD_CMD_SET_BLOCKLEN 16U
#define SD_CMD_READ_SINGLE_BLOCK 17U
#define SD_CMD_APP_SET_BUS_WIDTH 6U

#define SD_CHECK_PATTERN 0x000001AAUL
#define SD_VOLTAGE_WINDOW 0x00FF8000UL
#define SD_HIGH_CAPACITY 0x40000000UL
#define SD_DEFAULT_BLOCK_LEN 512U
#define SD_INIT_RETRY_COUNT 5000U
#define SD_CMD_TIMEOUT 0x00FFFFFFUL
#define SD_DATA_TIMEOUT 0x0FFFFFFFUL
#define SD_READ_LOOP_TIMEOUT 0x00FFFFFFUL
#define SDIO_STATIC_CMD_FLAGS (SDIO_FLAG_CCRCFAIL | SDIO_FLAG_CTIMEOUT | SDIO_FLAG_CMDREND | SDIO_FLAG_CMDSENT)

static uint32_t g_sd_rca;
static int g_sd_ready;
static int g_sd_v2;
static int g_sd_high_capacity;
static const char *g_sd_last_error = "not initialized";
static int g_sd_debug_enabled;
static void (*g_sd_debug_printer)(const char *text);
static uint32_t g_sd_last_sta;
static uint32_t g_sd_last_resp1;
static uint32_t g_sd_last_cmdresp;
static uint32_t g_sd_read_count;
static uint32_t g_sd_read_error_count;
static uint32_t g_sd_last_read_sig;

static int sd_read_raw_address_debug(uint32_t address, uint8_t *buffer);

static void sd_debug_print(const char *text)
{
    if (g_sd_debug_enabled && g_sd_debug_printer != 0)
        g_sd_debug_printer(text);
}

static void sd_debug_step(const char *step, int result)
{
    char line[128];

    if (!g_sd_debug_enabled || g_sd_debug_printer == 0)
        return;

    sprintf(line, "[sd] %-8s ret=%d err=%s sta=0x%08lX cmdresp=%lu resp1=0x%08lX\r\n",
            step,
            result,
            g_sd_last_error,
            (unsigned long)g_sd_last_sta,
            (unsigned long)g_sd_last_cmdresp,
            (unsigned long)g_sd_last_resp1);
    g_sd_debug_printer(line);
}

static uint32_t sd_ld_dword(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void sd_debug_dump_sector_prefix(const uint8_t *sector)
{
    char line[160];

    if (!g_sd_debug_enabled || g_sd_debug_printer == 0)
        return;

    sprintf(line,
            "[fs] sector0 first32=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            sector[0], sector[1], sector[2], sector[3], sector[4], sector[5], sector[6], sector[7],
            sector[8], sector[9], sector[10], sector[11], sector[12], sector[13], sector[14], sector[15],
            sector[16], sector[17], sector[18], sector[19], sector[20], sector[21], sector[22], sector[23],
            sector[24], sector[25], sector[26], sector[27], sector[28], sector[29], sector[30], sector[31]);
    g_sd_debug_printer(line);
}

static void sd_debug_dump_regs(const char *tag)
{
    char line[160];

    if (!g_sd_debug_enabled || g_sd_debug_printer == 0)
        return;

    sprintf(line, "[sd] %s CLKCR=0x%08lX DCTRL=0x%08lX DCOUNT=%lu FIFOCNT=%lu STA=0x%08lX highcap=%d\r\n",
            tag,
            (unsigned long)SDIO->CLKCR,
            (unsigned long)SDIO->DCTRL,
            (unsigned long)SDIO->DCOUNT,
            (unsigned long)SDIO->FIFOCNT,
            (unsigned long)SDIO->STA,
            g_sd_high_capacity);
    g_sd_debug_printer(line);
}
static void sd_debug_dump_probe(const char *tag, uint32_t address, const uint8_t *sector, int result)
{
    char line[180];

    if (!g_sd_debug_enabled || g_sd_debug_printer == 0)
        return;

    sprintf(line,
            "[sd] probe %s addr=0x%08lX ret=%d r1=0x%08lX sig=%02X%02X first16=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            tag,
            (unsigned long)address,
            result,
            (unsigned long)g_sd_last_resp1,
            sector[511], sector[510],
            sector[0], sector[1], sector[2], sector[3], sector[4], sector[5], sector[6], sector[7],
            sector[8], sector[9], sector[10], sector[11], sector[12], sector[13], sector[14], sector[15]);
    g_sd_debug_printer(line);
}

static void sd_debug_probe_addresses(void)
{
    static uint8_t probe_sector[512];
    static int probed;
    int result;

    if (probed)
        return;
    probed = 1;

    result = sd_read_raw_address_debug(0U, probe_sector);
    sd_debug_dump_probe("raw0", 0U, probe_sector, result);

    result = sd_read_raw_address_debug(1U, probe_sector);
    sd_debug_dump_probe("raw1", 1U, probe_sector, result);

    result = sd_read_raw_address_debug(512U, probe_sector);
    sd_debug_dump_probe("raw512", 512U, probe_sector, result);

    result = sd_read_raw_address_debug(8192U, probe_sector);
    sd_debug_dump_probe("raw8192", 8192U, probe_sector, result);
}
static void sd_debug_sector0(const uint8_t *sector)
{
    char line[180];

    if (!g_sd_debug_enabled || g_sd_debug_printer == 0)
        return;

    sprintf(line, "[fs] sector0 sig=%02X%02X jump=%02X %02X %02X p0_type=0x%02X p0_lba=%lu p0_size=%lu reads=%lu read_err=%lu\r\n",
            sector[511],
            sector[510],
            sector[0],
            sector[1],
            sector[2],
            sector[450],
            (unsigned long)sd_ld_dword(&sector[454]),
            (unsigned long)sd_ld_dword(&sector[458]),
            (unsigned long)g_sd_read_count,
            (unsigned long)g_sd_read_error_count);
    g_sd_debug_printer(line);
    sd_debug_dump_sector_prefix(sector);
    sd_debug_dump_regs("sector0");
    sd_debug_probe_addresses();
}

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
    sd_delay(0x000FFFFFUL);
}

static void sd_clear_data_path(void)
{
    SDIO_DataInitTypeDef data;

    data.SDIO_DataTimeOut = 0;
    data.SDIO_DataLength = 0;
    data.SDIO_DataBlockSize = SDIO_DataBlockSize_1b;
    data.SDIO_TransferDir = SDIO_TransferDir_ToSDIO;
    data.SDIO_TransferMode = SDIO_TransferMode_Block;
    data.SDIO_DPSM = SDIO_DPSM_Disable;
    SDIO_DataConfig(&data);

    while (SDIO_GetFlagStatus(SDIO_FLAG_RXDAVL) != RESET)
        (void)SDIO_ReadData();

    SDIO_ClearFlag(SDIO_FLAG_DCRCFAIL | SDIO_FLAG_DTIMEOUT | SDIO_FLAG_DATAEND |
                   SDIO_FLAG_STBITERR | SDIO_FLAG_DBCKEND | SDIO_FLAG_RXOVERR |
                   SDIO_FLAG_RXFIFOHF | SDIO_FLAG_RXDAVL);
}

static void sd_send_command(uint32_t arg, uint32_t cmd_idx, uint32_t resp)
{
    SDIO_CmdInitTypeDef cmd;

    SDIO_ClearFlag(SDIO_STATIC_CMD_FLAGS);
    g_sd_last_sta = 0;
    g_sd_last_resp1 = 0;
    g_sd_last_cmdresp = 0;

    cmd.SDIO_Argument = arg;
    cmd.SDIO_CmdIndex = cmd_idx;
    cmd.SDIO_Response = resp;
    cmd.SDIO_Wait = SDIO_Wait_No;
    cmd.SDIO_CPSM = SDIO_CPSM_Enable;
    SDIO_SendCommand(&cmd);
}

static int sd_wait_cmd_no_response(void)
{
    uint32_t timeout = SD_CMD_TIMEOUT;

    while (timeout--)
    {
        uint32_t sta = SDIO->STA;
        g_sd_last_sta = sta;
        if (sta & SDIO_FLAG_CTIMEOUT)
        {
            SDIO_ClearFlag(SDIO_FLAG_CTIMEOUT);
            g_sd_last_error = "cmd timeout";
            return SDCARD_ERR_TIMEOUT;
        }
        if (sta & SDIO_FLAG_CMDSENT)
        {
            g_sd_last_cmdresp = SDIO_GetCommandResponse();
            g_sd_last_resp1 = SDIO_GetResponse(SDIO_RESP1);
            SDIO_ClearFlag(SDIO_FLAG_CMDSENT);
            return SDCARD_OK;
        }
    }

    g_sd_last_error = "cmd wait timeout";
    return SDCARD_ERR_TIMEOUT;
}

static int sd_wait_cmd_response(uint32_t cmd_idx, int allow_crc_fail, int check_cmd_index)
{
    uint32_t timeout = SD_CMD_TIMEOUT;

    while (timeout--)
    {
        uint32_t sta = SDIO->STA;
        g_sd_last_sta = sta;
        if (sta & SDIO_FLAG_CTIMEOUT)
        {
            SDIO_ClearFlag(SDIO_FLAG_CTIMEOUT);
            g_sd_last_error = "cmd timeout";
            return SDCARD_ERR_TIMEOUT;
        }
        if (sta & SDIO_FLAG_CCRCFAIL)
        {
            g_sd_last_cmdresp = SDIO_GetCommandResponse();
            g_sd_last_resp1 = SDIO_GetResponse(SDIO_RESP1);
            SDIO_ClearFlag(SDIO_FLAG_CCRCFAIL);
            if (allow_crc_fail)
                return SDCARD_OK;
            g_sd_last_error = "cmd crc fail";
            return SDCARD_ERR_RESP;
        }
        if (sta & SDIO_FLAG_CMDREND)
        {
            g_sd_last_cmdresp = SDIO_GetCommandResponse();
            g_sd_last_resp1 = SDIO_GetResponse(SDIO_RESP1);
            SDIO_ClearFlag(SDIO_FLAG_CMDREND);
            if (check_cmd_index && g_sd_last_cmdresp != cmd_idx)
            {
                g_sd_last_error = "unexpected cmd response";
                return SDCARD_ERR_RESP;
            }
            return SDCARD_OK;
        }
        if (sta & SDIO_FLAG_CMDSENT)
        {
            g_sd_last_cmdresp = SDIO_GetCommandResponse();
            g_sd_last_resp1 = SDIO_GetResponse(SDIO_RESP1);
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
    return sd_wait_cmd_no_response();
}

static int sd_cmd8(void)
{
    uint32_t r;

    sd_send_command(SD_CHECK_PATTERN, SD_CMD_SEND_IF_COND, SDIO_Response_Short);
    if (sd_wait_cmd_response(SD_CMD_SEND_IF_COND, 0, 1) != SDCARD_OK)
        return SDCARD_ERR_UNSUPPORTED;

    r = SDIO_GetResponse(SDIO_RESP1);
    if ((r & 0xFFFU) != 0x1AAU)
    {
        g_sd_last_error = "cmd8 pattern mismatch";
        return SDCARD_ERR_UNSUPPORTED;
    }
    g_sd_v2 = 1;
    return SDCARD_OK;
}

static int sd_cmd2(void)
{
    sd_send_command(0, SD_CMD_ALL_SEND_CID, SDIO_Response_Long);
    return sd_wait_cmd_response(SD_CMD_ALL_SEND_CID, 0, 0);
}

static int sd_acmd41_init(void)
{
    uint32_t retry;

    for (retry = 0; retry < SD_INIT_RETRY_COUNT; ++retry)
    {
        sd_send_command(0, SD_CMD_APP_CMD, SDIO_Response_Short);
        if (sd_wait_cmd_response(SD_CMD_APP_CMD, 0, 1) != SDCARD_OK)
            return SDCARD_ERR_RESP;

        sd_send_command(SD_VOLTAGE_WINDOW | (g_sd_v2 ? SD_HIGH_CAPACITY : 0U), SD_CMD_SD_APP_OP_COND, SDIO_Response_Short);
        if (sd_wait_cmd_response(SD_CMD_SD_APP_OP_COND, 1, 0) != SDCARD_OK)
            return SDCARD_ERR_RESP;

        if (SDIO_GetResponse(SDIO_RESP1) & 0x80000000UL)
        {
            g_sd_high_capacity = g_sd_v2 && ((SDIO_GetResponse(SDIO_RESP1) & SD_HIGH_CAPACITY) != 0U);
            return SDCARD_OK;
        }
        sd_delay(10000);
    }

    g_sd_last_error = "acmd41 init timeout";
    return SDCARD_ERR_TIMEOUT;
}

static int sd_get_rca(void)
{
    sd_send_command(0, SD_CMD_SET_REL_ADDR, SDIO_Response_Short);
    if (sd_wait_cmd_response(SD_CMD_SET_REL_ADDR, 0, 1) != SDCARD_OK)
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
    return sd_wait_cmd_response(SD_CMD_SELECT_CARD, 0, 1);
}

static int sd_set_bus_width_4bit(void)
{
    sd_debug_print("[sd] keep 1-bit bus for filesystem test\r\n");
    return SDCARD_OK;

#if 0
    sd_send_command(g_sd_rca, SD_CMD_APP_CMD, SDIO_Response_Short);
    if (sd_wait_cmd_response(SD_CMD_APP_CMD, 0, 1) != SDCARD_OK)
        return SDCARD_ERR_RESP;

    sd_send_command(2, SD_CMD_APP_SET_BUS_WIDTH, SDIO_Response_Short);
    if (sd_wait_cmd_response(SD_CMD_APP_SET_BUS_WIDTH, 0, 1) != SDCARD_OK)
        return SDCARD_ERR_RESP;

    sd_sdio_clock_config(1U, SDIO_BusWide_4b);
    return SDCARD_OK;
#endif
}

static int sd_set_block_length(void)
{
    if (g_sd_high_capacity)
        return SDCARD_OK;

    sd_send_command(SD_DEFAULT_BLOCK_LEN, SD_CMD_SET_BLOCKLEN, SDIO_Response_Short);
    return sd_wait_cmd_response(SD_CMD_SET_BLOCKLEN, 0, 1);
}

static int sd_read_data_polling(uint8_t *buffer)
{
    uint32_t idx = 0;
    uint32_t sta;
    uint32_t timeout = SD_READ_LOOP_TIMEOUT;

    while (timeout-- && idx < SDCARD_SECTOR_SIZE)
    {
        sta = SDIO->STA;
        g_sd_last_sta = sta;

        if (sta & (SDIO_FLAG_DTIMEOUT | SDIO_FLAG_DCRCFAIL | SDIO_FLAG_RXOVERR | SDIO_FLAG_STBITERR))
        {
            SDIO_ClearFlag(SDIO_FLAG_DTIMEOUT | SDIO_FLAG_DCRCFAIL | SDIO_FLAG_RXOVERR | SDIO_FLAG_STBITERR);
            g_sd_last_error = "data read error";
            return SDCARD_ERR_IO;
        }

        while ((SDIO->STA & SDIO_FLAG_RXDAVL) && idx < SDCARD_SECTOR_SIZE)
        {
            uint32_t word = SDIO_ReadData();
            buffer[idx++] = (uint8_t)(word & 0xFFU);
            buffer[idx++] = (uint8_t)((word >> 8) & 0xFFU);
            buffer[idx++] = (uint8_t)((word >> 16) & 0xFFU);
            buffer[idx++] = (uint8_t)((word >> 24) & 0xFFU);
        }

        if ((SDIO->STA & SDIO_FLAG_DATAEND) && idx >= SDCARD_SECTOR_SIZE)
        {
            SDIO_ClearFlag(SDIO_FLAG_DATAEND);
            break;
        }
    }

    if (idx != SDCARD_SECTOR_SIZE)
    {
        g_sd_last_error = timeout == 0U ? "data wait timeout" : "short sector read";
        return SDCARD_ERR_IO;
    }

    SDIO_ClearFlag(SDIO_FLAG_DATAEND | SDIO_FLAG_DBCKEND);
    return SDCARD_OK;
}

int sdcard_stub_init(void)
{
    int result;

    if (g_sd_ready)
    {
        sd_debug_print("[sd] init skipped, already ready\r\n");
        return SDCARD_OK;
    }

    SDIO_DeInit();
    g_sd_ready = 0;
    g_sd_rca = 0;
    g_sd_v2 = 0;
    g_sd_high_capacity = 0;
    g_sd_read_count = 0;
    g_sd_read_error_count = 0;
    g_sd_last_read_sig = 0;
    g_sd_last_error = "init start";

    sd_gpio_init();
    sd_sdio_clock_config(178U, SDIO_BusWide_1b);
    sd_debug_print("[sd] init start\r\n");

    result = sd_cmd0();
    sd_debug_step("CMD0", result);
    if (result != SDCARD_OK)
        return SDCARD_ERR_TIMEOUT;

    result = sd_cmd8();
    sd_debug_step("CMD8", result);

    result = sd_acmd41_init();
    sd_debug_step("ACMD41", result);
    if (result != SDCARD_OK)
        return SDCARD_ERR_TIMEOUT;

    result = sd_cmd2();
    sd_debug_step("CMD2", result);
    if (result != SDCARD_OK)
        return SDCARD_ERR_RESP;

    result = sd_get_rca();
    sd_debug_step("CMD3", result);
    if (result != SDCARD_OK)
        return SDCARD_ERR_RESP;

    result = sd_select_card();
    sd_debug_step("CMD7", result);
    if (result != SDCARD_OK)
        return SDCARD_ERR_RESP;

    result = sd_set_block_length();
    sd_debug_step("CMD16", result);
    if (result != SDCARD_OK)
        return SDCARD_ERR_RESP;

    result = sd_set_bus_width_4bit();
    sd_debug_step("ACMD6", result);
    if (result != SDCARD_OK)
        return SDCARD_ERR_RESP;

    sd_sdio_clock_config(16U, SDIO_BusWide_1b);
    sd_debug_print("[sd] transfer clock div=16 bus=1-bit\r\n");

    g_sd_ready = 1;
    g_sd_last_error = "ok";
    sd_debug_print("[sd] init ok\r\n");
    return SDCARD_OK;
}

void sdcard_stub_set_debug(int enabled, void (*printer)(const char *text))
{
    g_sd_debug_enabled = enabled;
    g_sd_debug_printer = printer;
}

int sdcard_stub_is_ready(void)
{
    return g_sd_ready;
}

static int sd_read_raw_address_debug(uint32_t address, uint8_t *buffer)
{
    SDIO_DataInitTypeDef data;
    int result;
    int attempt;

    for (attempt = 0; attempt < 3; ++attempt)
    {
        sd_clear_data_path();

        data.SDIO_DataTimeOut = SD_DATA_TIMEOUT;
        data.SDIO_DataLength = SDCARD_SECTOR_SIZE;
        data.SDIO_DataBlockSize = SDIO_DataBlockSize_512b;
        data.SDIO_TransferDir = SDIO_TransferDir_ToSDIO;
        data.SDIO_TransferMode = SDIO_TransferMode_Block;
        data.SDIO_DPSM = SDIO_DPSM_Enable;
        SDIO_DataConfig(&data);

        sd_send_command(address, SD_CMD_READ_SINGLE_BLOCK, SDIO_Response_Short);
        if (sd_wait_cmd_response(SD_CMD_READ_SINGLE_BLOCK, 0, 1) != SDCARD_OK)
        {
            result = SDCARD_ERR_RESP;
            continue;
        }

        result = sd_read_data_polling(buffer);
        if (result == SDCARD_OK)
            return SDCARD_OK;
    }

    return result;
}
int sdcard_stub_read_sector(uint32_t sector, uint8_t *buffer)
{
    SDIO_DataInitTypeDef data;
    uint32_t address = g_sd_high_capacity ? sector : (sector * SDCARD_SECTOR_SIZE);
    int result;
    int attempt;
    char line[128];

    if (!g_sd_ready)
        return SDCARD_ERR_NOT_READY;

    for (attempt = 0; attempt < 3; ++attempt)
    {
        sd_clear_data_path();

        data.SDIO_DataTimeOut = SD_DATA_TIMEOUT;
        data.SDIO_DataLength = SDCARD_SECTOR_SIZE;
        data.SDIO_DataBlockSize = SDIO_DataBlockSize_512b;
        data.SDIO_TransferDir = SDIO_TransferDir_ToSDIO;
        data.SDIO_TransferMode = SDIO_TransferMode_Block;
        data.SDIO_DPSM = SDIO_DPSM_Enable;
        SDIO_DataConfig(&data);

        sd_send_command(address, SD_CMD_READ_SINGLE_BLOCK, SDIO_Response_Short);
        if (sd_wait_cmd_response(SD_CMD_READ_SINGLE_BLOCK, 0, 1) != SDCARD_OK)
        {
            result = SDCARD_ERR_RESP;
            continue;
        }

        result = sd_read_data_polling(buffer);
        if (result == SDCARD_OK)
        {
            g_sd_read_count++;
            g_sd_last_read_sig = ((uint32_t)buffer[511] << 8) | buffer[510];
            if (sector == 0U)
                sd_debug_sector0(buffer);
            return SDCARD_OK;
        }
    }

    g_sd_read_error_count++;
    if (g_sd_debug_enabled && g_sd_debug_printer != 0)
    {
        sprintf(line, "[sd] READ sect=%lu addr=0x%08lX ret=%d err=%s sta=0x%08lX sig=0x%04lX attempt=%d\r\n",
                (unsigned long)sector,
                (unsigned long)address,
                result,
                g_sd_last_error,
                (unsigned long)g_sd_last_sta,
                (unsigned long)g_sd_last_read_sig,
                attempt);
        g_sd_debug_printer(line);
    }
    return result;
}

const char *sdcard_stub_last_error_string(void)
{
    return g_sd_last_error;
}
