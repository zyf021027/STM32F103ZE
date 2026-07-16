#ifndef _DELAY_H_
#define _DELAY_H_

#include "stm32f10x.h"
#include <stdio.h>

#define fac_us 9
#define fac_ms 9*1000


void delay_Init(void);
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);
void delay_s(uint32_t s) ;

#endif

