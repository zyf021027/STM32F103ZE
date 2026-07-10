#include "exti.h"
#include "gpio.h"
#include "delay.h"



void MX_EXTI1_Init(uint8_t Preemption, uint8_t Sub)
{
//---------------------GPIO Init------------------------//
	RCC_APB2PeriphClockCmd( RCC_APB2Periph_AFIO , ENABLE);  				//使能功能复用时钟
	MX_KeyInit();
	
	GPIO_EXTILineConfig( GPIO_PortSourceGPIOE , GPIO_PinSource1 );  //将 PE1 与外部中断绑定
	
//---------------------EXTI Init------------------------//
	EXTI_InitTypeDef EXTI_InitStruct;
	
	EXTI_InitStruct.EXTI_Line 		= EXTI_Line1;   				//外部中断线 1
	EXTI_InitStruct.EXTI_LineCmd 	= ENABLE;								//使能外部中断线
	EXTI_InitStruct.EXTI_Mode 		= EXTI_Mode_Interrupt;	//使用中断模式
	EXTI_InitStruct.EXTI_Trigger 	= EXTI_Trigger_Rising;	//上升沿触发方式
	
	EXTI_Init( &EXTI_InitStruct);
	
//---------------------NVIC Init------------------------//
	NVIC_InitTypeDef NVIC_InitStruct;
	
	NVIC_InitStruct.NVIC_IRQChannel 									= EXTI1_IRQn;	//中断线 1
	NVIC_InitStruct.NVIC_IRQChannelCmd 								= ENABLE ;		//使能中断
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = Preemption; //抢占优先级
	NVIC_InitStruct.NVIC_IRQChannelSubPriority 				= Sub;				//相应优先级
	
	NVIC_Init(&NVIC_InitStruct);
}


BitAction LED2_BitVal = Bit_SET;

void EXTI1_IRQHandler(void)
{
	if( GPIO_ReadInputDataBit(  GPIOE,  GPIO_Pin_1 ) == Bit_SET )
	{
		delay_ms(10); //消抖
		if( GPIO_ReadInputDataBit(  GPIOE,  GPIO_Pin_1 ) == Bit_SET )
		{
			//处理中断逻辑
			LED2_BitVal = (BitAction)!LED2_BitVal;
			GPIO_WriteBit(GPIOD, GPIO_Pin_14, LED2_BitVal);  //输出电平
		}
	}
	//清除中断线
	EXTI_ClearITPendingBit( EXTI_Line1 );
	
}





