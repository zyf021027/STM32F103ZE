#ifndef _USART_H_
#define _USART_H_

#include "stm32f10x.h"
#include <stdio.h>

#include "stdbool.h"


void MX_USART1_Init( uint32_t BaudRate,uint8_t Preemption, uint8_t Sub );

#define U1RX_MAX_LEN 128

extern char 		U1Rx_Data[ U1RX_MAX_LEN ] ; 	//数据接收区
extern uint8_t  U1Rx_LEN 									;		//数据接收长度
extern bool 		U1Rx_IDLE_FLAG 						;	  //数据接收完成标志位
	
	
	
#endif

