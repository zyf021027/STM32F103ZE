#ifndef AUDIO_RECORD_H
#define AUDIO_RECORD_H

#include <stdint.h>

typedef struct
{
    uint32_t samples_per_channel;
    uint32_t channels;
    uint32_t sample_rate;
    int last_result;
} audio_record_debug_info_t;

int audio_record_demo_once(void);
const audio_record_debug_info_t *audio_record_get_debug_info(void);

#endif