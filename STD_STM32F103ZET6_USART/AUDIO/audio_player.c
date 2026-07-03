#include "audio_player.h"

#include <stdio.h>
#include <string.h>

#include "board_audio.h"
#include "sdcard_stub.h"
#include "pff/pff.h"
#include "minimp3/minimp3.h"

#define MP3_STREAM_BUFFER_SIZE 4096U
#define MP3_READ_CHUNK_SIZE 1024U

static FATFS g_fs;
static char g_last_error[96];
static mp3dec_t g_mp3d;
static audio_player_stats_t g_stats;
static int g_audio_debug_enabled;
static void (*g_audio_debug_printer)(const char *text);

static void audio_player_debug_scan_filesystem(void);

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

static void audio_player_debug_progress(void)
{
    char line[128];

    if (!g_audio_debug_enabled || g_audio_debug_printer == 0)
        return;

    sprintf(line, "[mp3] progress bytes=%lu frames=%lu sr=%lu ch=%lu\r\n",
            (unsigned long)g_stats.bytes_read,
            (unsigned long)g_stats.frames_decoded,
            (unsigned long)g_stats.last_sample_rate,
            (unsigned long)g_stats.last_channels);
    g_audio_debug_printer(line);
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

    if (board_audio_init() != 0)
    {
        audio_player_set_error("board audio init failed");
        return -1;
    }

    if (sdcard_stub_init() != 0)
    {
        audio_player_set_error(sdcard_stub_last_error_string());
        return -2;
    }

    mount_result = pf_mount(&g_fs);
    if (mount_result != FR_OK)
    {
        audio_player_debug_sector0();
        sprintf(mount_error, "pf_mount failed %d", mount_result);
        audio_player_set_error(mount_error);
        return -3;
    }

    mp3dec_init(&g_mp3d);
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
    static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t info;
    UINT br = 0;
    unsigned int buffered = 0;
    unsigned int eof = 0;
    int samples;

    audio_player_reset_stats();

    if (pf_open(path) != FR_OK)
    {
        audio_player_set_error("pf_open failed");
        return -1;
    }

    mp3dec_init(&g_mp3d);

    while (1)
    {
        if (!eof && buffered < (MP3_STREAM_BUFFER_SIZE / 2U))
        {
            UINT space = (UINT)(MP3_STREAM_BUFFER_SIZE - buffered);
            UINT request = MP3_READ_CHUNK_SIZE;

            if (request > space)
                request = space;

            if (request > 0U)
            {
                if (pf_read(file_buf + buffered, request, &br) != FR_OK)
                {
                    audio_player_set_error("pf_read failed");
                    return -2;
                }
                if (br == 0U)
                    eof = 1U;
                else
                {
                    buffered += br;
                    g_stats.bytes_read += br;
                }
            }
        }

        if (buffered == 0U)
            break;

        memset(&info, 0, sizeof(info));
        g_stats.decode_calls++;
        samples = mp3dec_decode_frame(&g_mp3d, file_buf, (int)buffered, pcm, &info);
        if (samples > 0)
        {
            if (info.channels != 1 && info.channels != 2)
            {
                audio_player_set_error("unsupported channel count");
                return -3;
            }

            board_audio_set_sample_rate((uint32_t)info.hz);

            if (board_audio_play_pcm(pcm, (uint32_t)samples, (uint32_t)info.channels) != 0)
            {
                audio_player_set_error("pcm output failed");
                return -4;
            }

            if (info.frame_bytes <= 0 || (unsigned int)info.frame_bytes > buffered)
            {
                audio_player_set_error("mp3 frame size invalid");
                return -5;
            }

            g_stats.frames_decoded++;
            g_stats.pcm_blocks_sent++;
            g_stats.last_sample_rate = (uint32_t)info.hz;
            g_stats.last_channels = (uint32_t)info.channels;
            if ((g_stats.frames_decoded % 64U) == 1U)
                audio_player_debug_progress();
            memmove(file_buf, file_buf + info.frame_bytes, buffered - (unsigned int)info.frame_bytes);
            buffered -= (unsigned int)info.frame_bytes;
            continue;
        }

        if (info.frame_bytes > 0 && (unsigned int)info.frame_bytes <= buffered)
        {
            g_stats.frames_skipped++;
            memmove(file_buf, file_buf + info.frame_bytes, buffered - (unsigned int)info.frame_bytes);
            buffered -= (unsigned int)info.frame_bytes;
            continue;
        }

        if (eof)
            break;
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
