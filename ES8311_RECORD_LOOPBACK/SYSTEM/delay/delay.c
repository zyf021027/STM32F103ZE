#include "delay.h"

void delay_Init(void)
{
	SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);   //时钟 8 分频

}

void delay_us(uint32_t us ) //最大值为 1,864,135
{
	uint32_t temp = 0;
	
	SysTick->LOAD = us * fac_us;								//LOAD寄存器赋初值
	SysTick->VAL  = 0;													//VAL寄存器清空
	SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;   //使能SysTick
	do
	{
		temp = SysTick->CTRL;
	}
	while(  (temp & 0x01 ) &&(!(temp&(0x01<<16)))  );							//判断SysTic是否启动 && 判断延时是否结束
	SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;   //关闭SysTick
	SysTick->VAL  = 0;													//VAL寄存器清空
}



void delay_ms(uint32_t ms)  //最大值为 1,864
{
    while (ms > 0U)
    {
        uint32_t temp = 0;
        uint32_t chunk = (ms > 1000U) ? 1000U : ms;

        SysTick->LOAD = chunk * fac_ms;
        SysTick->VAL  = 0;
        SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
        do
        {
            temp = SysTick->CTRL;
        }
        while ((temp & 0x01U) && (!(temp & (0x01U << 16))));
        SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
        SysTick->VAL  = 0;
        ms -= chunk;
    }
}

void delay_s(uint32_t s) 
{
	uint32_t temp = s;
	
	while(temp--)
	{
		delay_ms(1000);
	}
}
