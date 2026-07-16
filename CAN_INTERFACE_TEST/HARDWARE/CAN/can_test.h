#ifndef __CAN_TEST_H
#define __CAN_TEST_H

#include "stm32f10x.h"

typedef enum
{
    CAN_TEST_TX_OK = 0,
    CAN_TEST_TX_FAILED = -1,
    CAN_TEST_TX_NO_MAILBOX = -2,
    CAN_TEST_TX_TIMEOUT = -3
} can_test_tx_result_t;

typedef struct
{
    uint32_t standard_id;
    uint8_t dlc;
    uint8_t data[8];
} can_test_frame_t;

typedef struct
{
    uint8_t receive_error_counter;
    uint8_t transmit_error_counter;
    uint8_t last_error_code;
    uint8_t error_warning;
    uint8_t error_passive;
    uint8_t bus_off;
} can_test_error_t;

int can_test_internal_loopback(void);
int can_test_normal_init(void);
can_test_tx_result_t can_test_send(uint32_t standard_id,
                                   const uint8_t *data,
                                   uint8_t dlc);
int can_test_receive(can_test_frame_t *frame);
void can_test_get_error(can_test_error_t *error);

#endif
