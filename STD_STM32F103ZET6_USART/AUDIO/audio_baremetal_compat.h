#ifndef AUDIO_BAREMETAL_COMPAT_H
#define AUDIO_BAREMETAL_COMPAT_H

#include <stddef.h>
#include "delay.h"

#ifndef RT_NULL
#define RT_NULL ((void *)0)
#endif

static inline void audio_delay_ms(unsigned int ms)
{
    delay_ms(ms);
}

#endif
