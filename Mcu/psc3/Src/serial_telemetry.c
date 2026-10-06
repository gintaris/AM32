/*
 * serial_telemetry.c
 *
 * KISS / ESC info telemetry on the SCB UART (SERIAL_TELEMETRY_SCB), transmit
 * only, 115200 8N1. On the KIT_PSC3M5_2GO this is SCB0 TX on P9.3, which the
 * on-board J-Link forwards as its virtual COM port.
 *
 * The SCB TX FIFO holds 128 bytes in byte mode and the largest AM32 packet
 * (ESC info) is 49 bytes, so send_telem_DMA() copies the packet into the FIFO
 * and the SCB sends it without DMA or interrupts. A packet that does not fit
 * because the previous one is still being sent is dropped.
 */

#include "serial_telemetry.h"

#include "kiss_telemetry.h"
#include "peripherals.h"
#include "psc3_timing.h"
#include "targets.h"

#define SCB_FIFO_BYTES      128U
#define SCB_MODE_UART       2UL
#define SCB_MEM_WIDTH_BYTE  0UL
#define UART_STOP_BITS_1    1UL     // UART_TX_CTRL.STOP_BITS: half bit periods - 1
#define PCLK_GROUP_SCB      ((uint32_t)PCLK_SCB0_CLOCK_SCB_EN >> 8)
#define PCLK_DIV_SCB        0U      // 16 bit divider 0 of PCLK group 4

void telem_UART_Init(void)
{
    CySCB_Type* scb = SERIAL_TELEMETRY_SCB;

    pclkDivider16(PCLK_GROUP_SCB, PCLK_DIV_SCB, PCLK_DIVIDE_UART);
    connectPclk(PCLK_SCB0_CLOCK_SCB_EN, PCLK_DIV_SCB);

    scb->CTRL = 0;  // disabled while configured
    scb->CTRL = ((TELEM_UART_OVS - 1UL) << SCB_CTRL_OVS_Pos)
        | (SCB_MEM_WIDTH_BYTE << SCB_CTRL_MEM_WIDTH_Pos)
        | (SCB_MODE_UART << SCB_CTRL_MODE_Pos);
    scb->UART_CTRL = 0;     // standard UART
    scb->UART_TX_CTRL = UART_STOP_BITS_1 << SCB_UART_TX_CTRL_STOP_BITS_Pos;    // no parity
    scb->TX_CTRL = 7UL << SCB_TX_CTRL_DATA_WIDTH_Pos;   // 8 bits, LSB first, push-pull
    scb->TX_FIFO_CTRL = SCB_TX_FIFO_CTRL_CLEAR_Msk;
    scb->TX_FIFO_CTRL = 0;
    scb->CTRL |= SCB_CTRL_ENABLED_Msk;

    // idle high, then hand the pin to the SCB
    gpioWrite(SERIAL_TELEMETRY_PORT, SERIAL_TELEMETRY_PIN, 1);
    gpioSetMode(SERIAL_TELEMETRY_PORT, SERIAL_TELEMETRY_PIN, GPIO_DM_STRONG, SERIAL_TELEMETRY_HSIOM_SEL);
}

void send_telem_DMA(uint8_t bytes)
{
    CySCB_Type* scb = SERIAL_TELEMETRY_SCB;
    const uint32_t used = (scb->TX_FIFO_STATUS & SCB_TX_FIFO_STATUS_USED_Msk) >> SCB_TX_FIFO_STATUS_USED_Pos;

    if (bytes > sizeof(aTxBuffer) || used + bytes > SCB_FIFO_BYTES) {
        return;
    }
    for (uint32_t i = 0; i < bytes; i++) {
        scb->TX_FIFO_WR = aTxBuffer[i];
    }
}
