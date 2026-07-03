#include "diskio.h"
#include "sdcard_stub.h"

DSTATUS disk_initialize(void)
{
    return sdcard_stub_init() == 0 ? 0 : STA_NOINIT;
}

DRESULT disk_readp(BYTE *buff, DWORD sector, UINT offset, UINT count)
{
    unsigned char sector_buf[512];
    unsigned int index;

    if (!sdcard_stub_is_ready())
        return RES_NOTRDY;

    if (sdcard_stub_read_sector((unsigned int)sector, sector_buf) != 0)
        return RES_ERROR;

    if (offset + count > 512U)
        return RES_PARERR;

    if (buff)
    {
        for (index = 0; index < count; ++index)
            buff[index] = sector_buf[offset + index];
    }

    return RES_OK;
}

DRESULT disk_writep(const BYTE *buff, DWORD sc)
{
    (void)buff;
    (void)sc;
    return RES_PARERR;
}
