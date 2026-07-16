# STM32F103ZET6 CAN Interface Test

This Keil MDK project tests the CAN interface fitted to this board. It uses
the STM32F10x Standard Peripheral Library and the same USART1 debug output as
the existing audio test projects.

## Board connections confirmed from the schematic

| Signal | Board connection |
| --- | --- |
| CAN1 RX | PD0, connected to TJA1050 RXD through H7 pins 3-4 |
| CAN1 TX | PD1, connected to TJA1050 TXD through H7 pins 1-2 |
| CANH | CN2 pin 2 |
| CANL | CN2 pin 1 |
| Transceiver | TJA1050, powered from the board 5 V rail |
| Termination | Fixed 120 ohm resistor between CANH and CANL |

H7 is the 2x2 header next to the CAN circuit. Fit two jumpers horizontally:
one across pins 1-2 and one across pins 3-4. Without both jumpers, the MCU is
disconnected from the CAN transceiver.

## Equipment

- The STM32F103ZET6 board and its normal USB power/debug connection.
- One USB-CAN adapter that supports classic CAN at 500 kbit/s.
- Three wires: CANH, CANL, and GND.
- The USB-CAN vendor application (or another application that can transmit
  and receive classic CAN frames).

A real CAN-bus transmit test needs at least two active CAN nodes. The internal
loopback test can run with only this board, but it does not test the TJA1050,
CN2, or the CANH/CANL wiring.

## Wiring

Turn off the board and USB-CAN adapter before making the connections.

| Board | USB-CAN adapter |
| --- | --- |
| CN2 pin 2, CANH | CANH |
| CN2 pin 1, CANL | CANL |
| Any board GND pin | GND |

Do not connect an adapter VCC or 5 V pin to CN2. CN2 carries only CANH and
CANL. The extra GND wire must go to a GND pin elsewhere on the board.

The board already has a fixed 120 ohm termination resistor. Enable the USB-CAN
adapter's 120 ohm termination, or add a 120 ohm resistor between CANH and CANL
at the adapter end. With all power off, a multimeter should read about 60 ohms
between CANH and CANL when both 120 ohm terminations are present.

For a short bench cable, ordinary wires are sufficient. Keep CANH and CANL
twisted together and shorter than about one metre.

## Build and flash

1. Open `MDK-ARM/CAN_Test.uvprojx` in Keil MDK.
2. Build target `CAN_Test`.
3. Flash the generated program with the same debugger used for the previous
   audio projects.
4. Open the board serial port at 115200 baud, 8 data bits, no parity, 1 stop
   bit, and no flow control.

At boot, the serial output should contain:

```text
[can-loopback] result=0 PASS
[can-normal] init=0 bitrate=500000
```

`PASS` proves that the MCU CAN peripheral, clock, filter, transmit mailbox,
and receive FIFO work. Continue with the external test to verify the physical
CAN interface.

## USB-CAN settings

Configure the USB-CAN channel as follows:

| Setting | Value |
| --- | --- |
| Protocol | Classic CAN (CAN 2.0), not CAN FD |
| Bit rate | 500 kbit/s |
| Frame format | Standard 11-bit identifier |
| Channel mode | Normal/active mode, not listen-only or silent mode |
| Termination | 120 ohm enabled at the USB-CAN end |

Start the USB-CAN channel before judging the board transmit result. A CAN
transmitter requires another active node to send the ACK bit; a listen-only
adapter cannot ACK frames.

## Test 1: board to computer

After the USB-CAN channel starts, it should receive one frame per second:

| Field | Expected value |
| --- | --- |
| Identifier | `0x321` |
| Frame | Standard data frame |
| DLC | 8 |
| Data bytes 0-3 | Increasing 32-bit counter, big-endian |
| Data bytes 4-7 | `53 54 4D 32` (ASCII `STM2`) |

The serial log should show `tx_result=0`, and `tx_ok` should increase once per
second. LED1 (PD13) toggles after each acknowledged heartbeat.

## Test 2: computer to board and reply

Use the USB-CAN application to transmit this frame once or periodically:

| Field | Value |
| --- | --- |
| Identifier | `0x123` |
| Frame | Standard data frame |
| DLC | 8 |
| Data | `01 02 03 04 05 06 07 08` |

The board should print the received `0x123` frame, then transmit identifier
`0x124` with exactly the same DLC and data. The USB-CAN receive list should
therefore show:

```text
0x124  01 02 03 04 05 06 07 08
```

LED2 (PD14) toggles whenever a standard data frame is received. Passing both
tests verifies the MCU pins, H7 jumpers, TJA1050 transmit and receive paths,
CN2, CANH, CANL, and termination.

## Troubleshooting

| Symptom | Most likely checks |
| --- | --- |
| No serial output | Select the correct COM port and use 115200 8N1 |
| Internal loopback FAIL | Rebuild/flash the correct project; this is not an external wiring problem |
| `tx_result=-1`, `tx_fail` and TEC increase | Start the second node in normal mode; check 500 kbit/s, H7, CANH/CANL, and GND |
| USB-CAN receives nothing | Check that CANH is not swapped with CANL and the adapter is not using CAN FD |
| Board receives nothing | Confirm the PC transmits standard ID `0x123`, not an extended frame |
| Unstable or intermittent frames | Check both 120 ohm terminations and use a short twisted CANH/CANL pair |
| `bus_off=1` | Correct wiring/bit rate, then reset the board; automatic recovery is also enabled |

LED3 (PD15) turns on when a heartbeat is not acknowledged or initialization
fails. On this board the three LEDs are active-low, so a low GPIO level turns
an LED on.
