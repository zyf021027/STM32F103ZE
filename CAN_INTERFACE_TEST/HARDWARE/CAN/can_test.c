#include "can_test.h"

#include <string.h>

#define CAN_TEST_TX_WAIT_LIMIT 1000000UL
#define CAN_TEST_LOOPBACK_ID   0x5A5U

static void can_test_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOD,
                           ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);

    /* CAN1 full remap: RX=PD0, TX=PD1. H7 must bridge 3-4 and 1-2. */
    GPIO_PinRemapConfig(GPIO_Remap2_CAN1, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_1;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOD, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_0;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOD, &gpio);
}

static void can_test_filter_init(void)
{
    CAN_FilterInitTypeDef filter;

    filter.CAN_FilterNumber = 0;
    filter.CAN_FilterMode = CAN_FilterMode_IdMask;
    filter.CAN_FilterScale = CAN_FilterScale_32bit;
    filter.CAN_FilterIdHigh = 0x0000;
    filter.CAN_FilterIdLow = 0x0000;
    filter.CAN_FilterMaskIdHigh = 0x0000;
    filter.CAN_FilterMaskIdLow = 0x0000;
    filter.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    filter.CAN_FilterActivation = ENABLE;
    CAN_FilterInit(&filter);
}

static int can_test_init(uint8_t mode)
{
    CAN_InitTypeDef can;

    can_test_gpio_init();
    CAN_DeInit(CAN1);
    CAN_StructInit(&can);

    can.CAN_TTCM = DISABLE;
    can.CAN_ABOM = ENABLE;
    can.CAN_AWUM = DISABLE;
    can.CAN_NART = ENABLE;
    can.CAN_RFLM = DISABLE;
    can.CAN_TXFP = DISABLE;
    can.CAN_Mode = mode;

    /* APB1=36 MHz, 18 time quanta: 36 MHz / (4 * 18) = 500 kbit/s. */
    can.CAN_SJW = CAN_SJW_1tq;
    can.CAN_BS1 = CAN_BS1_13tq;
    can.CAN_BS2 = CAN_BS2_4tq;
    can.CAN_Prescaler = 4;

    if (CAN_Init(CAN1, &can) != CAN_InitStatus_Success)
        return -1;

    can_test_filter_init();
    return 0;
}

can_test_tx_result_t can_test_send(uint32_t standard_id,
                                   const uint8_t *data,
                                   uint8_t dlc)
{
    CanTxMsg tx;
    uint8_t mailbox;
    uint8_t status;
    uint32_t wait_count;

    if ((standard_id > 0x7FFU) || (dlc > 8U) || ((dlc > 0U) && (data == 0)))
        return CAN_TEST_TX_FAILED;

    memset(&tx, 0, sizeof(tx));
    tx.StdId = standard_id;
    tx.IDE = CAN_Id_Standard;
    tx.RTR = CAN_RTR_Data;
    tx.DLC = dlc;
    if (dlc > 0U)
        memcpy(tx.Data, data, dlc);

    mailbox = CAN_Transmit(CAN1, &tx);
    if (mailbox == CAN_TxStatus_NoMailBox)
        return CAN_TEST_TX_NO_MAILBOX;

    for (wait_count = 0; wait_count < CAN_TEST_TX_WAIT_LIMIT; ++wait_count)
    {
        status = CAN_TransmitStatus(CAN1, mailbox);
        if (status == CAN_TxStatus_Ok)
            return CAN_TEST_TX_OK;
        if (status == CAN_TxStatus_Failed)
            return CAN_TEST_TX_FAILED;
    }

    CAN_CancelTransmit(CAN1, mailbox);
    return CAN_TEST_TX_TIMEOUT;
}

int can_test_receive(can_test_frame_t *frame)
{
    CanRxMsg rx;

    if ((frame == 0) || (CAN_MessagePending(CAN1, CAN_FIFO0) == 0U))
        return 0;

    CAN_Receive(CAN1, CAN_FIFO0, &rx);
    if ((rx.IDE != CAN_Id_Standard) || (rx.RTR != CAN_RTR_Data))
        return 0;

    frame->standard_id = rx.StdId;
    frame->dlc = rx.DLC;
    memcpy(frame->data, rx.Data, rx.DLC);
    return 1;
}

void can_test_get_error(can_test_error_t *error)
{
    uint32_t esr;

    if (error == 0)
        return;

    esr = CAN1->ESR;
    error->receive_error_counter = (uint8_t)((esr >> 24) & 0xFFU);
    error->transmit_error_counter = (uint8_t)((esr >> 16) & 0xFFU);
    error->last_error_code = (uint8_t)((esr >> 4) & 0x07U);
    error->error_warning = (esr & CAN_ESR_EWGF) ? 1U : 0U;
    error->error_passive = (esr & CAN_ESR_EPVF) ? 1U : 0U;
    error->bus_off = (esr & CAN_ESR_BOFF) ? 1U : 0U;
}

int can_test_internal_loopback(void)
{
    static const uint8_t expected[8] =
        {0x10U, 0x32U, 0x54U, 0x76U, 0x98U, 0xBAU, 0xDCU, 0xFEU};
    can_test_frame_t received;
    uint32_t wait_count;

    if (can_test_init(CAN_Mode_LoopBack) != 0)
        return -1;
    if (can_test_send(CAN_TEST_LOOPBACK_ID, expected, sizeof(expected)) !=
        CAN_TEST_TX_OK)
        return -2;

    for (wait_count = 0; wait_count < CAN_TEST_TX_WAIT_LIMIT; ++wait_count)
    {
        if (can_test_receive(&received))
        {
            if ((received.standard_id == CAN_TEST_LOOPBACK_ID) &&
                (received.dlc == sizeof(expected)) &&
                (memcmp(received.data, expected, sizeof(expected)) == 0))
                return 0;
            return -3;
        }
    }

    return -4;
}

int can_test_normal_init(void)
{
    return can_test_init(CAN_Mode_Normal);
}
