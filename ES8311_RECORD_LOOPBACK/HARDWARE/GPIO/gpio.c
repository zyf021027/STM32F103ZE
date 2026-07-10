#include "gpio.h"

void MX_GPIO_Init(void)
{
	/* Configure all the GPIOA in Input Floating mode */ 
	GPIO_InitTypeDef GPIO_InitStruct;
	
	RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOD, ENABLE);
	
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15; 				
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz; 
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP; 
	GPIO_Init(GPIOD, &GPIO_InitStruct);
	
	GPIO_WriteBit(GPIOD, GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15, Bit_RESET);  //默认输出电平
}



void MX_KeyInit(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	
	RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOE, ENABLE);
	
	GPIO_InitStruct.GPIO_Pin 		= GPIO_Pin_1; 				
	GPIO_InitStruct.GPIO_Speed 	= GPIO_Speed_50MHz; 
	GPIO_InitStruct.GPIO_Mode 	= GPIO_Mode_IPD;     //下拉工作模式
	GPIO_Init(GPIOE, &GPIO_InitStruct);
	
}


