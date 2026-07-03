#ifndef SDCARD_STUB_H
#define SDCARD_STUB_H

#include <stdint.h>

#define SDCARD_SECTOR_SIZE 512U

typedef enum
{
    SDCARD_OK = 0,
    SDCARD_ERR_NO_CARD = -1,
    SDCARD_ERR_TIMEOUT = -2,
    SDCARD_ERR_RESP = -3,
    SDCARD_ERR_UNSUPPORTED = -4,
    SDCARD_ERR_IO = -5,
    SDCARD_ERR_NOT_READY = -6
} sdcard_result_t;

int sdcard_stub_init(void);
void sdcard_stub_set_debug(int enabled, void (*printer)(const char *text));
int sdcard_stub_is_ready(void);
int sdcard_stub_read_sector(uint32_t sector, uint8_t *buffer);
const char *sdcard_stub_last_error_string(void);

#endif
