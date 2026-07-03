#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdint.h>

#define AUDIO_PLAYER_ENABLE_MP3 1
#define AUDIO_PLAYER_ENABLE_MP4 0

typedef struct
{
    uint32_t bytes_read;
    uint32_t decode_calls;
    uint32_t frames_decoded;
    uint32_t frames_skipped;
    uint32_t pcm_blocks_sent;
    uint32_t last_sample_rate;
    uint32_t last_channels;
} audio_player_stats_t;

int audio_player_init(void);
void audio_player_set_debug(int enabled, void (*printer)(const char *text));
int audio_player_find_first_mp3(char *path, uint32_t path_size);
int audio_player_play_mp3_from_sd(const char *path);
int audio_player_play_from_sd(const char *path);
const char *audio_player_last_error(void);
const audio_player_stats_t *audio_player_get_stats(void);

#endif
