#include "debug_uart.h"

#include <stdarg.h>
#include <stdio.h>

#include "stm32f10x.h"

#define DEBUG_UART USART1
#define DEBUG_UART_GPIO GPIOA
#define DEBUG_UART_TX_PIN GPIO_Pin_9
#define DEBUG_UART_RX_PIN GPIO_Pin_10
#define DEBUG_UART_BAUDRATE 115200U

static int g_debug_uart_ready;

void debug_uart_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1 | RCC_APB2Periph_AFIO, ENABLE);

    gpio.GPIO_Pin = DEBUG_UART_TX_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(DEBUG_UART_GPIO, &gpio);

    gpio.GPIO_Pin = DEBUG_UART_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(DEBUG_UART_GPIO, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = DEBUG_UART_BAUDRATE;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(DEBUG_UART, &usart);
    USART_Cmd(DEBUG_UART, ENABLE);

    g_debug_uart_ready = 1;
}

void debug_uart_puts(const char *text)
{
    if (!g_debug_uart_ready || text == 0)
        return;

    while (*text != '\0')
    {
        if (*text == '\n')
        {
            while (USART_GetFlagStatus(DEBUG_UART, USART_FLAG_TXE) == RESET) {}
            USART_SendData(DEBUG_UART, '\r');
        }
        while (USART_GetFlagStatus(DEBUG_UART, USART_FLAG_TXE) == RESET) {}
        USART_SendData(DEBUG_UART, (uint16_t)(unsigned char)(*text));
        ++text;
    }
}

void debug_uart_printf(const char *fmt, ...)
{
    char buffer[160];
    va_list args;
    int written;

    if (!g_debug_uart_ready || fmt == 0)
        return;

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written > 0)
        debug_uart_puts(buffer);
}