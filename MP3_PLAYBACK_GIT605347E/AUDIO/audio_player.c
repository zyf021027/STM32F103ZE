#include "audio_player.h"

#include <stdio.h>
#include <string.h>

#include "stm32f10x.h"

#include "board_audio.h"
#include "sdcard_stub.h"
#include "pff/pff.h"
#include "helix/pub/mp3dec.h"

#define MP3_STREAM_BUFFER_SIZE (MAINBUF_SIZE * 4U)
#define MP3_READ_CHUNK_SIZE 1536U
#define MP3_PROGRESS_FRAME_INTERVAL 128U
#define AUDIO_PLAYER_DWT_CTRL (*(volatile uint32_t *)0xE0001000U)
#define AUDIO_PLAYER_DWT_CYCCNT (*(volatile uint32_t *)0xE0001004U)
#define AUDIO_PLAYER_DWT_CYCCNTENA 0x00000001U

extern uint32_t SystemCoreClock;
static FATFS g_fs;
static char g_last_error[96];
static HMP3Decoder g_mp3_decoder;
static audio_player_stats_t g_stats;
static int g_audio_debug_enabled;
static void (*g_audio_debug_printer)(const char *text);

static void audio_player_debug_scan_filesystem(void);
static void audio_player_debug_progress(void)
{
    char line[200];
    const board_audio_debug_info_t *dbg;
    uint32_t work_us;
    uint32_t gap_pct = 0U;

    if (!g_audio_debug_enabled || g_audio_debug_printer == 0)
        return;
    if (g_stats.frames_decoded == 0U || (g_stats.frames_decoded % MP3_PROGRESS_FRAME_INTERVAL) != 0U)
        return;

    work_us = g_stats.read_time_us + g_stats.decode_time_us;
    if (g_stats.total_audio_ms > 0U)
        gap_pct = (uint32_t)(((uint64_t)work_us * 100ULL) / ((uint64_t)g_stats.total_audio_ms * 1000ULL));

    dbg = board_audio_get_debug_info();
    sprintf(line,
            "[mp3-progress] frames=%lu audio_ms=%lu read_us=%lu decode_us=%lu max_decode_us=%lu gap_pct=%lu dma_under=%lu dma_used=%lu dec_err=%ld\r\n",
            (unsigned long)g_stats.frames_decoded,
            (unsigned long)g_stats.total_audio_ms,
            (unsigned long)g_stats.read_time_us,
            (unsigned long)g_stats.decode_time_us,
            (unsigned long)g_stats.max_decode_us,
            (unsigned long)gap_pct,
            (unsigned long)dbg->dma_underruns,
            (unsigned long)dbg->dma_used_halfwords,
            (long)g_stats.last_decode_error);
    g_audio_debug_printer(line);
}

static void audio_player_timer_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    AUDIO_PLAYER_DWT_CYCCNT = 0U;
    AUDIO_PLAYER_DWT_CTRL |= AUDIO_PLAYER_DWT_CYCCNTENA;
}

static uint32_t audio_player_timer_now(void)
{
    return AUDIO_PLAYER_DWT_CYCCNT;
}

static uint32_t audio_player_cycles_to_us(uint32_t cycles)
{
    uint32_t cycles_per_us = SystemCoreClock / 1000000U;

    if (cycles_per_us == 0U)
        cycles_per_us = 72U;
    return cycles / cycles_per_us;
}

static void audio_player_add_time(uint32_t *total_us, uint32_t *max_us, uint32_t start_cycles)
{
    uint32_t elapsed_us = audio_player_cycles_to_us(audio_player_timer_now() - start_cycles);

    *total_us += elapsed_us;
    if (elapsed_us > *max_us)
        *max_us = elapsed_us;
}

static uint32_t audio_player_ld_dword(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int audio_player_sector_has_aa55(const unsigned char *sector)
{
    return sector[510] == 0x55U && sector[511] == 0xAAU;
}

static int audio_player_sector_has_fat_name(const unsigned char *sector)
{
    return memcmp(&sector[54], "FAT", 3) == 0 || memcmp(&sector[82], "FAT32", 5) == 0;
}

static int audio_player_sector_has_exfat_name(const unsigned char *sector)
{
    return memcmp(&sector[3], "EXFAT   ", 8) == 0;
}

static void audio_player_debug_sector_summary(const char *tag, uint32_t sector_no, const unsigned char *sector)
{
    char line[220];
    const char *kind = "none";

    if (!g_audio_debug_enabled || g_audio_debug_printer == 0)
        return;

    if (audio_player_sector_has_aa55(sector) && audio_player_sector_has_fat_name(sector))
        kind = "fat";
    else if (audio_player_sector_has_aa55(sector) && audio_player_sector_has_exfat_name(sector))
        kind = "exfat";
    else if (audio_player_sector_has_aa55(sector))
        kind = "mbr/boot";

    sprintf(line,
            "[fs] %s sect=%lu kind=%s sig=%02X%02X jump=%02X %02X %02X oem=%02X %02X %02X %02X %02X %02X %02X %02X p0_type=0x%02X p0_lba=%lu p0_size=%lu\r\n",
            tag,
            (unsigned long)sector_no,
            kind,
            sector[511], sector[510],
            sector[0], sector[1], sector[2],
            sector[3], sector[4], sector[5], sector[6], sector[7], sector[8], sector[9], sector[10],
            sector[450],
            (unsigned long)audio_player_ld_dword(&sector[454]),
            (unsigned long)audio_player_ld_dword(&sector[458]));
    g_audio_debug_printer(line);
}

static int audio_player_sector_interesting(const unsigned char *sector)
{
    if (!audio_player_sector_has_aa55(sector))
        return 0;

    if (audio_player_sector_has_fat_name(sector) || audio_player_sector_has_exfat_name(sector))
        return 1;

    if (sector[450] != 0U || audio_player_ld_dword(&sector[454]) != 0U)
        return 1;

    return 0;
}

static void audio_player_debug_scan_filesystem(void)
{
    static unsigned char scan_sector[512];
    static int scanned;
    static const uint32_t candidates[] = {1U, 2U, 32U, 63U, 128U, 256U, 512U, 1024U, 2048U, 4096U, 8192U, 32768U};
    uint32_t i;
    uint32_t found = 0;
    char line[160];

    if (!g_audio_debug_enabled || g_audio_debug_printer == 0)
        return;
    if (scanned)
        return;
    scanned = 1;

    g_audio_debug_printer("[fs] mount failed: probing common partition starts\r\n");
    for (i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i)
    {
        if (sdcard_stub_read_sector(candidates[i], scan_sector) == 0)
            audio_player_debug_sector_summary("candidate", candidates[i], scan_sector);
        else
        {
            sprintf(line, "[fs] candidate sect=%lu read failed err=%s\r\n",
                    (unsigned long)candidates[i], sdcard_stub_last_error_string());
            g_audio_debug_printer(line);
        }
    }

    g_audio_debug_printer("[fs] scanning sectors 1..2047 for AA55/FAT/exFAT\r\n");
    for (i = 1U; i < 2048U; ++i)
    {
        if (sdcard_stub_read_sector(i, scan_sector) != 0)
            continue;

        if (audio_player_sector_interesting(scan_sector))
        {
            audio_player_debug_sector_summary("scan-hit", i, scan_sector);
            ++found;
            if (found >= 8U)
                break;
        }
    }

    if (found == 0U)
        g_audio_debug_printer("[fs] scan-hit none in first 2048 sectors\r\n");
}
static void audio_player_debug_sector0(void)
{
    static unsigned char sector[512];
    char line[160];

    if (!g_audio_debug_enabled || g_audio_debug_printer == 0)
        return;

    if (sdcard_stub_read_sector(0, sector) != 0)
    {
        sprintf(line, "[fs] sector0 read failed err=%s\r\n", sdcard_stub_last_error_string());
        g_audio_debug_printer(line);
        return;
    }

    sprintf(line, "[fs] mount-fail sector0 sig=%02X%02X jump=%02X %02X %02X p0_type=0x%02X p0_lba=%lu p0_size=%lu\r\n",
            sector[511],
            sector[510],
            sector[0],
            sector[1],
            sector[2],
            sector[450],
            (unsigned long)audio_player_ld_dword(&sector[454]),
            (unsigned long)audio_player_ld_dword(&sector[458]));
    g_audio_debug_printer(line);
    audio_player_debug_scan_filesystem();
}

static void audio_player_set_error(const char *text)
{
    unsigned int i = 0;

    while (text[i] != '\0' && i < (sizeof(g_last_error) - 1U))
    {
        g_last_error[i] = text[i];
        ++i;
    }
    g_last_error[i] = '\0';
}

static void audio_player_reset_stats(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
}

static int audio_player_has_suffix(const char *path, const char *suffix)
{
    unsigned int path_len = 0;
    unsigned int suffix_len = 0;
    unsigned int i;

    while (path[path_len] != '\0')
        ++path_len;
    while (suffix[suffix_len] != '\0')
        ++suffix_len;

    if (path_len < suffix_len)
        return 0;

    for (i = 0; i < suffix_len; ++i)
    {
        char a = path[path_len - suffix_len + i];
        char b = suffix[i];

        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }

    return 1;
}

int audio_player_init(void)
{
    FRESULT mount_result;
    char mount_error[32];

    if (g_audio_debug_enabled && g_audio_debug_printer != 0)
        g_audio_debug_printer("[init] board_audio_init start\r\n");
    if (board_audio_init() != 0)
    {
        audio_player_set_error("board audio init failed");
        return -1;
    }

    if (g_audio_debug_enabled && g_audio_debug_printer != 0)
        g_audio_debug_printer("[init] board_audio_init ok\r\n");

    if (g_audio_debug_enabled && g_audio_debug_printer != 0)
        g_audio_debug_printer("[init] sdcard_stub_init start\r\n");
    if (sdcard_stub_init() != 0)
    {
        audio_player_set_error(sdcard_stub_last_error_string());
        return -2;
    }

    if (g_audio_debug_enabled && g_audio_debug_printer != 0)
        g_audio_debug_printer("[init] sdcard_stub_init ok\r\n");

    if (g_audio_debug_enabled && g_audio_debug_printer != 0)
        g_audio_debug_printer("[init] pf_mount start\r\n");
    mount_result = pf_mount(&g_fs);
    if (mount_result != FR_OK)
    {
        audio_player_debug_sector0();
        sprintf(mount_error, "pf_mount failed %d", mount_result);
        audio_player_set_error(mount_error);
        return -3;
    }

    if (g_mp3_decoder == 0)
        g_mp3_decoder = MP3InitDecoder();
    if (g_mp3_decoder == 0)
    {
        audio_player_set_error("mp3 decoder init failed");
        return -4;
    }
    audio_player_reset_stats();
    audio_player_set_error("ok");
    return 0;
}

void audio_player_set_debug(int enabled, void (*printer)(const char *text))
{
    g_audio_debug_enabled = enabled;
    g_audio_debug_printer = printer;
}

int audio_player_find_first_mp3(char *path, uint32_t path_size)
{
    DIR dir;
    FILINFO file_info;
    FRESULT result;
    uint32_t i;

    if (path == 0 || path_size == 0U)
    {
        audio_player_set_error("invalid path buffer");
        return -1;
    }

    result = pf_opendir(&dir, "/");
    if (result != FR_OK)
    {
        audio_player_set_error("pf_opendir failed");
        return -2;
    }

    while (1)
    {
        result = pf_readdir(&dir, &file_info);
        if (result != FR_OK)
        {
            audio_player_set_error("pf_readdir failed");
            return -3;
        }

        if (file_info.fname[0] == '\0')
            break;

        if ((file_info.fattrib & AM_DIR) == 0U && audio_player_has_suffix(file_info.fname, ".mp3"))
        {
            for (i = 0; file_info.fname[i] != '\0' && i < (path_size - 1U); ++i)
                path[i] = file_info.fname[i];
            path[i] = '\0';

            if (file_info.fname[i] != '\0')
            {
                audio_player_set_error("mp3 filename too long");
                return -4;
            }

            audio_player_set_error("mp3 file found");
            return 0;
        }
    }

    audio_player_set_error("no mp3 file found");
    return -5;
}

int audio_player_play_mp3_from_sd(const char *path)
{
    static unsigned char file_buf[MP3_STREAM_BUFFER_SIZE];
    static short pcm[MAX_NCHAN * MAX_NGRAN * MAX_NSAMP];
    MP3FrameInfo frame_info;
    unsigned char *read_ptr;
    UINT br = 0;
    int bytes_left = 0;
    int eof = 0;
    int offset;
    int err;
    uint32_t timing_start;

    audio_player_reset_stats();
    audio_player_timer_init();

    if (g_mp3_decoder == 0)
        g_mp3_decoder = MP3InitDecoder();
    if (g_mp3_decoder == 0)
    {
        audio_player_set_error("mp3 decoder init failed");
        return -8;
    }

    if (pf_open(path) != FR_OK)
    {
        audio_player_set_error("pf_open failed");
        return -1;
    }

    MP3FreeDecoder(g_mp3_decoder);
    g_mp3_decoder = MP3InitDecoder();
    if (g_mp3_decoder == 0)
    {
        audio_player_set_error("mp3 decoder init failed");
        return -8;
    }

    while (1)
    {
        if (!eof && bytes_left < (int)(MP3_STREAM_BUFFER_SIZE - MP3_READ_CHUNK_SIZE))
        {
            timing_start = audio_player_timer_now();
            if (pf_read(file_buf + bytes_left, MP3_READ_CHUNK_SIZE, &br) != FR_OK)
            {
                audio_player_set_error("pf_read failed");
                return -2;
            }
            audio_player_add_time(&g_stats.read_time_us, &g_stats.max_read_us, timing_start);

            if (br == 0U)
            {
                eof = 1;
            }
            else
            {
                bytes_left += (int)br;
                g_stats.bytes_read += br;
            }
        }

        if (bytes_left <= 0)
            break;

        offset = MP3FindSyncWord(file_buf, bytes_left);
        if (offset < 0)
        {
            if (eof)
                break;

            if (bytes_left > 1)
            {
                file_buf[0] = file_buf[bytes_left - 1];
                bytes_left = 1;
            }
            continue;
        }

        if (offset > 0)
        {
            memmove(file_buf, file_buf + offset, (uint32_t)(bytes_left - offset));
            bytes_left -= offset;
            if (g_stats.frames_decoded == 0U)
                g_stats.first_frame_offset += (uint32_t)offset;
        }

        read_ptr = file_buf;
        memset(&frame_info, 0, sizeof(frame_info));
        g_stats.decode_calls++;
        timing_start = audio_player_timer_now();
        err = MP3Decode(g_mp3_decoder, &read_ptr, &bytes_left, pcm, 0);
        audio_player_add_time(&g_stats.decode_time_us, &g_stats.max_decode_us, timing_start);
        g_stats.last_decode_error = err;

        if (err == ERR_MP3_INDATA_UNDERFLOW)
        {
            if (eof)
                break;
            continue;
        }

        if (err == ERR_MP3_MAINDATA_UNDERFLOW)
        {
            MP3GetLastFrameInfo(g_mp3_decoder, &frame_info);
            g_stats.frames_skipped++;
        }
        else if (err != ERR_MP3_NONE)
        {
            if (bytes_left > 1)
            {
                memmove(file_buf, file_buf + 1, (uint32_t)(bytes_left - 1));
                bytes_left -= 1;
            }
            else
            {
                bytes_left = 0;
            }
            g_stats.frames_skipped++;
            continue;
        }
        else
        {
            uint32_t samples_per_channel;

            MP3GetLastFrameInfo(g_mp3_decoder, &frame_info);
            if (frame_info.nChans != 1 && frame_info.nChans != 2)
            {
                audio_player_set_error("unsupported channel count");
                return -3;
            }
            if (frame_info.samprate <= 0 || frame_info.outputSamps <= 0)
            {
                audio_player_set_error("mp3 frame info invalid");
                return -5;
            }

            samples_per_channel = (uint32_t)(frame_info.outputSamps / frame_info.nChans);
            board_audio_set_sample_rate((uint32_t)frame_info.samprate);

            timing_start = audio_player_timer_now();
            if (board_audio_play_pcm(pcm, samples_per_channel, (uint32_t)frame_info.nChans) != 0)
            {
                audio_player_set_error("pcm output failed");
                return -4;
            }
            audio_player_add_time(&g_stats.pcm_time_us, &g_stats.max_pcm_us, timing_start);

            if (g_stats.frames_decoded == 0U)
                g_stats.first_frame_offset += (uint32_t)(read_ptr - file_buf);
            g_stats.frames_decoded++;
            g_stats.pcm_blocks_sent++;
            g_stats.last_sample_rate = (uint32_t)frame_info.samprate;
            g_stats.last_channels = (uint32_t)frame_info.nChans;
            g_stats.last_layer = (uint32_t)frame_info.layer;
            g_stats.last_bitrate_kbps = (uint32_t)(frame_info.bitrate / 1000);
            g_stats.last_frame_bytes = (uint32_t)(read_ptr - file_buf);
            g_stats.last_samples_per_frame = samples_per_channel;
            g_stats.total_audio_ms += (uint32_t)(((uint64_t)samples_per_channel * 1000ULL) / (uint32_t)frame_info.samprate);
            audio_player_debug_progress();
        }

        if (bytes_left > 0 && read_ptr != file_buf)
            memmove(file_buf, read_ptr, (uint32_t)bytes_left);
    }

    if (g_stats.bytes_read == 0U)
    {
        audio_player_set_error("mp3 file is empty");
        return -6;
    }

    if (g_stats.frames_decoded == 0U)
    {
        audio_player_set_error("no valid mp3 frame decoded");
        return -7;
    }

    board_audio_drain();
    g_stats.total_wall_us = g_stats.read_time_us + g_stats.decode_time_us + g_stats.pcm_time_us;
    audio_player_set_error("mp3 playback done");
    return 0;
}
int audio_player_play_from_sd(const char *path)
{
#if AUDIO_PLAYER_ENABLE_MP3
    if (audio_player_has_suffix(path, ".mp3"))
        return audio_player_play_mp3_from_sd(path);
#endif

    if (audio_player_has_suffix(path, ".mp4") || audio_player_has_suffix(path, ".m4a"))
    {
#if AUDIO_PLAYER_ENABLE_MP4
        audio_player_set_error("mp4 playback not implemented yet");
        return -20;
#else
        audio_player_set_error("mp4/m4a support disabled");
        return -21;
#endif
    }

    audio_player_set_error("unsupported file extension");
    return -22;
}

const char *audio_player_last_error(void)
{
    return g_last_error;
}

const audio_player_stats_t *audio_player_get_stats(void)
{
    return &g_stats;
}
