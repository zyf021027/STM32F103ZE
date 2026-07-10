#include "usart.h"

char 		U1Rx_Data[ U1RX_MAX_LEN ] = {0}; 	//数据接收区
uint8_t U1Rx_LEN 									= 0;		//数据接收长度
bool 		U1Rx_IDLE_FLAG 						= 0;	  //数据接收完成标志位



void MX_USART1_Init( uint32_t BaudRate,uint8_t Preemption, uint8_t Sub )
{
//----------------------GPIO Init----------------------------//
	GPIO_InitTypeDef GPIO_InitStruct;
	
	RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
	GPIO_PinRemapConfig(GPIO_Remap_USART1, DISABLE);
	
	//PA9  TX
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9; 				
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz; 
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;     //复用推挽输出
	GPIO_Init(GPIOA, &GPIO_InitStruct);
	
	//PA10  RX
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10; 				
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz; 
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;     //上拉输入
	GPIO_Init(GPIOA, &GPIO_InitStruct);
	
//----------------------USART Init----------------------------//
	USART_InitTypeDef USART_InitStruct;
	
	RCC_APB2PeriphClockCmd( RCC_APB2Periph_USART1, ENABLE);   //使能串口1 时钟
	
	USART_InitStruct.USART_BaudRate 						= BaudRate;  //波特率
	USART_InitStruct.USART_HardwareFlowControl 	= USART_HardwareFlowControl_None; //不使用硬件流控制
	USART_InitStruct.USART_Mode 								= USART_Mode_Rx | USART_Mode_Tx; //串口收发工作模式
	USART_InitStruct.USART_Parity 							= USART_Parity_No; //不使用校验
	USART_InitStruct.USART_StopBits 						= USART_StopBits_1;	//1位停止位
	USART_InitStruct.USART_WordLength 					= USART_WordLength_8b; //8位数据位
	
	USART_Init( USART1 , &USART_InitStruct);
	USART_Cmd( USART1, ENABLE);              //使能串口
	
//----------------------NVIC Init----------------------------//
	NVIC_InitTypeDef NVIC_InitStruct;
	
	USART_ITConfig( USART1, USART_IT_RXNE, ENABLE);
	USART_ITConfig( USART1, USART_IT_IDLE, ENABLE);
	
	NVIC_InitStruct.NVIC_IRQChannel 									= USART1_IRQn;//USART1 中断
	NVIC_InitStruct.NVIC_IRQChannelCmd 								= ENABLE ;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = Preemption; //抢占优先级
	NVIC_InitStruct.NVIC_IRQChannelSubPriority 				= Sub;				//相应优先级
	
	NVIC_Init(&NVIC_InitStruct);
	
}


void USART1_IRQHandler(void)
{
	if( USART_GetITStatus( USART1, USART_IT_RXNE ) == SET )
	{
		//接收保存数据
		if( U1Rx_LEN < U1RX_MAX_LEN )
		{
			U1Rx_Data[ U1Rx_LEN++ ] = USART_ReceiveData( USART1 );
		}
		else
		{
			U1Rx_IDLE_FLAG = 1; //接收数据长度溢出，强制进行数据处理
		}
	}
	if( USART_GetITStatus( USART1, USART_IT_IDLE ) == SET )
	{
		//由软件序列清除该位(先读USART_SR，然后读USART_DR)。
		USART1->SR;
		USART1->DR;
		
	  //接收完成处理过程
		U1Rx_IDLE_FLAG = 1; //数据正常接收完成，进行数据处理
	}
}







//加入以下代码,支持printf函数,而不需要选择use MicroLIB	  
#if 1
#pragma import(__use_no_semihosting)             
//标准库需要的支持函数                 
struct __FILE 
{ int handle; 
}; 

FILE __stdout;       
//定义_sys_exit()以避免使用半主机模式    
void _sys_exit(int x) 
{ x = x; } 

//重定义fputc函数 
int fputc(int ch, FILE *f)
{
    (void)f;
    while ((USART1->SR & 0x80U) == 0U)
    {
    }
    USART1->DR = (u8)ch;
    return ch;
}
#endif 

//原文链接：https://blog.csdn.net/weixin_41882419/article/details/153742246




