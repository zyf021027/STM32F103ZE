#include "stm32f10x.h"

#include <stdio.h>

#include "can_test.h"
#include "delay.h"
#include "usart.h"

#define CAN_HEARTBEAT_ID 0x321U
#define CAN_REQUEST_ID   0x123U
#define CAN_RESPONSE_ID  0x124U

static uint32_t tx_ok_count;
static uint32_t tx_fail_count;
static uint32_t rx_count;

static void led_toggle(uint16_t pin)
{
    if ((GPIOD->ODR & pin) != 0U)
        GPIO_ResetBits(GPIOD, pin);
    else
        GPIO_SetBits(GPIOD, pin);
}

static void led_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOD, &gpio);
    GPIO_SetBits(GPIOD, GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15);
}

static void print_frame(const char *tag, const can_test_frame_t *frame)
{
    uint8_t i;

    printf("[%s] id=0x%03lX dlc=%u data=",
           tag,
           (unsigned long)frame->standard_id,
           (unsigned int)frame->dlc);
    for (i = 0; i < frame->dlc; ++i)
        printf("%02X%s", frame->data[i], (i + 1U == frame->dlc) ? "" : " ");
    printf("\r\n");
}

static void print_bus_status(can_test_tx_result_t result)
{
    can_test_error_t error;

    can_test_get_error(&error);
    printf("[can-status] tx_result=%d tx_ok=%lu tx_fail=%lu rx=%lu "
           "tec=%u rec=%u lec=%u warning=%u passive=%u bus_off=%u\r\n",
           result,
           (unsigned long)tx_ok_count,
           (unsigned long)tx_fail_count,
           (unsigned long)rx_count,
           (unsigned int)error.transmit_error_counter,
           (unsigned int)error.receive_error_counter,
           (unsigned int)error.last_error_code,
           (unsigned int)error.error_warning,
           (unsigned int)error.error_passive,
           (unsigned int)error.bus_off);
}

static void send_heartbeat(uint32_t sequence)
{
    uint8_t data[8];
    can_test_tx_result_t result;

    data[0] = (uint8_t)(sequence >> 24);
    data[1] = (uint8_t)(sequence >> 16);
    data[2] = (uint8_t)(sequence >> 8);
    data[3] = (uint8_t)sequence;
    data[4] = 0x53U;
    data[5] = 0x54U;
    data[6] = 0x4DU;
    data[7] = 0x32U;

    result = can_test_send(CAN_HEARTBEAT_ID, data, sizeof(data));
    if (result == CAN_TEST_TX_OK)
    {
        ++tx_ok_count;
        led_toggle(GPIO_Pin_13);
        GPIO_SetBits(GPIOD, GPIO_Pin_15);
    }
    else
    {
        ++tx_fail_count;
        GPIO_ResetBits(GPIOD, GPIO_Pin_15);
    }
    print_bus_status(result);
}

static void process_received_frames(void)
{
    can_test_frame_t frame;

    while (can_test_receive(&frame))
    {
        ++rx_count;
        led_toggle(GPIO_Pin_14);
        print_frame("can-rx", &frame);

        if (frame.standard_id == CAN_REQUEST_ID)
        {
            can_test_tx_result_t result;

            result = can_test_send(CAN_RESPONSE_ID, frame.data, frame.dlc);
            if (result == CAN_TEST_TX_OK)
                ++tx_ok_count;
            else
                ++tx_fail_count;
            printf("[can-reply] request=0x123 response=0x124 result=%d\r\n",
                   result);
        }
    }
}

int main(void)
{
    int result;
    uint32_t sequence = 0U;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    delay_Init();
    MX_USART1_Init(115200, 1, 2);
    led_init();

    printf("\r\n[can-demo] baremetal boot\r\n");
    printf("[can-demo] build %s %s\r\n", __DATE__, __TIME__);
    printf("[can-demo] USART1 PA9/PA10 115200 8N1\r\n");
    printf("[can-demo] CAN1 PD0/RX PD1/TX, CN2 pin2=CANH pin1=CANL\r\n");

    result = can_test_internal_loopback();
    printf("[can-loopback] result=%d %s\r\n",
           result,
           (result == 0) ? "PASS" : "FAIL");
    if (result != 0)
    {
        GPIO_ResetBits(GPIOD, GPIO_Pin_15);
        while (1)
            delay_ms(1000);
    }

    result = can_test_normal_init();
    printf("[can-normal] init=%d bitrate=500000\r\n", result);
    if (result != 0)
    {
        GPIO_ResetBits(GPIOD, GPIO_Pin_15);
        while (1)
            delay_ms(1000);
    }

    printf("[can-demo] send 0x321 every second; send 0x123 to get 0x124 echo\r\n");
    while (1)
    {
        uint8_t tick;

        for (tick = 0; tick < 10U; ++tick)
        {
            process_received_frames();
            delay_ms(100);
        }
        send_heartbeat(sequence++);
    }
}
