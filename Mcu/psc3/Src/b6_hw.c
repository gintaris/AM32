/*
 * b6_hw.c
 *
 * PSC3 side of protocol B6 (targets with USE_B6_PROTOCOL, b6_am32.h):
 *
 * UART on an SCB (EVK_PSC3: SCB3, P6.2 RX / P6.3 TX, the KitProg3 USB-UART),
 * 8N1 at B6_UART_BAUD. The clock comes from an 8 bit divider of the SCB
 * PCLK group: 921600 baud = 180 MHz / 15 / 13 oversampling (923077 baud,
 * +0.16 %). Both directions go through ring buffers and the SCB interrupt
 * (priority 3, the lowest): the receive FIFO is emptied on NOT_EMPTY, the
 * transmit FIFO refilled while it holds less than TX_TRIGGER bytes. The main
 * loop takes received bytes and queues whole frames, so blocking calls in the
 * main loop (tones, settings) only delay the protocol.
 *
 * Also the identity for HELLO (die ID, BOARD_ID), the reset cause, the fault
 * flags of the 6EDL7141 and the TLI4971 OCD inputs, and the signal input
 * switch.
 */

#include "b6_am32.h"

#include "ADC.h"
#include "IO.h"
#include "b6_protocol.h"
#include "edl7141.h"
#include "ocd.h"
#include "peripherals.h"
#include "targets.h"

#ifdef USE_B6_PROTOCOL

#define SCB_MODE_UART       2UL
#define SCB_MEM_WIDTH_BYTE  0UL
#define SCB_DATA_WIDTH_8    7UL
#define UART_STOP_BITS_1    1UL     // half bit periods - 1
#define B6_UART_OVS         13UL
#define B6_PCLK_GROUP       ((uint32_t)B6_UART_PCLK >> 8)
#define B6_PCLK_DIV8        1U      // 8 bit divider 1 of the SCB group (0 is the 6EDL7141 SPI)
#define B6_PCLK_DIVIDE      ((CPU_FREQUENCY_MHZ * 1000000UL + B6_UART_BAUD * B6_UART_OVS / 2UL) / (B6_UART_BAUD * B6_UART_OVS))
#define SCB_FIFO_BYTES      128U
#define TX_TRIGGER          64UL    // refill below this FIFO level

#define RX_RING             512U    // 5.5 ms at 921600 baud
#define TX_RING             2048U   // 22 ms, 39 telemetry frames
#define RING_MASK(n)        ((n) - 1U)

#if (B6_PCLK_DIVIDE > 256) || (B6_PCLK_DIVIDE < 1)
#error "B6_UART_BAUD needs a divider outside the 8 bit range"
#endif

static uint8_t rx_buf[RX_RING];
static volatile uint16_t rx_head;   // written by the interrupt
static uint16_t rx_tail;
static uint8_t tx_buf[TX_RING];
static volatile uint16_t tx_head;   // written by the main loop
static volatile uint16_t tx_tail;   // written by the interrupt

volatile uint32_t b6_uart_rx_overflow;  // ring or FIFO full, bytes lost
volatile uint32_t b6_uart_rx_errors;    // frame / parity errors

void b6HwInit(void)
{
    CySCB_Type* scb = B6_UART_SCB;

    enablePeriSlave(B6_UART_PERI_GROUP, B6_UART_PERI_SLAVE);
    pclkDivider8(B6_PCLK_GROUP, B6_PCLK_DIV8, B6_PCLK_DIVIDE);
    connectPclk8(B6_UART_PCLK, B6_PCLK_DIV8);

    scb->CTRL = 0;  // disabled while configured
    scb->CTRL = ((B6_UART_OVS - 1UL) << SCB_CTRL_OVS_Pos)
        | (SCB_MEM_WIDTH_BYTE << SCB_CTRL_MEM_WIDTH_Pos)
        | (SCB_MODE_UART << SCB_CTRL_MODE_Pos);
    scb->UART_CTRL = 0;     // standard UART
    scb->UART_TX_CTRL = UART_STOP_BITS_1 << SCB_UART_TX_CTRL_STOP_BITS_Pos;
    scb->UART_RX_CTRL = UART_STOP_BITS_1 << SCB_UART_RX_CTRL_STOP_BITS_Pos;
    scb->TX_CTRL = SCB_DATA_WIDTH_8 << SCB_TX_CTRL_DATA_WIDTH_Pos;     // LSB first
    scb->RX_CTRL = (SCB_DATA_WIDTH_8 << SCB_RX_CTRL_DATA_WIDTH_Pos) | SCB_RX_CTRL_MEDIAN_Msk;
    scb->TX_FIFO_CTRL = SCB_TX_FIFO_CTRL_CLEAR_Msk;
    scb->TX_FIFO_CTRL = TX_TRIGGER << SCB_TX_FIFO_CTRL_TRIGGER_LEVEL_Pos;
    scb->RX_FIFO_CTRL = SCB_RX_FIFO_CTRL_CLEAR_Msk;
    scb->RX_FIFO_CTRL = 0;
    scb->INTR_TX_MASK = 0;
    scb->INTR_RX = 0xFFFFFFFFUL;
    scb->INTR_RX_MASK = SCB_INTR_RX_NOT_EMPTY_Msk | SCB_INTR_RX_OVERFLOW_Msk | SCB_INTR_RX_FRAME_ERROR_Msk;
    scb->CTRL |= SCB_CTRL_ENABLED_Msk;

    // TX idles high, then both pins to the SCB
    gpioWrite(B6_UART_PORT, B6_UART_TX_PIN, 1);
    gpioSetMode(B6_UART_PORT, B6_UART_TX_PIN, GPIO_DM_STRONG, B6_UART_HSIOM_SEL);
    gpioSetMode(B6_UART_PORT, B6_UART_RX_PIN, GPIO_DM_HIGHZ, B6_UART_HSIOM_SEL);

    NVIC_SetPriority(B6_UART_IRQ, 3);
    NVIC_ClearPendingIRQ(B6_UART_IRQ);
    NVIC_EnableIRQ(B6_UART_IRQ);
}

uint8_t b6HwRead(uint8_t* byte)
{
    if (rx_tail == rx_head) {
        return 0;
    }
    *byte = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) & RING_MASK(RX_RING));
    return 1;
}

uint8_t b6HwWrite(const uint8_t* data, uint32_t len)
{
    uint16_t head = tx_head;
    const uint32_t used = (uint16_t)(head - tx_tail) & RING_MASK(TX_RING);

    if (len > TX_RING - 1U - used) {
        return 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        tx_buf[head] = data[i];
        head = (uint16_t)((head + 1U) & RING_MASK(TX_RING));
    }
    tx_head = head;
    B6_UART_SCB->INTR_TX_MASK = SCB_INTR_TX_TRIGGER_Msk;
    return 1;
}

void B6_UART_IRQ_HANDLER(void)
{
    CySCB_Type* scb = B6_UART_SCB;
    const uint32_t rx = scb->INTR_RX & (SCB_INTR_RX_OVERFLOW_Msk | SCB_INTR_RX_FRAME_ERROR_Msk);

    if (rx & SCB_INTR_RX_OVERFLOW_Msk) {
        b6_uart_rx_overflow++;
    }
    if (rx & SCB_INTR_RX_FRAME_ERROR_Msk) {
        b6_uart_rx_errors++;
    }
    while ((scb->RX_FIFO_STATUS & SCB_RX_FIFO_STATUS_USED_Msk) != 0U) {
        const uint8_t c = (uint8_t)scb->RX_FIFO_RD;
        const uint16_t next = (uint16_t)((rx_head + 1U) & RING_MASK(RX_RING));
        if (next == rx_tail) {
            b6_uart_rx_overflow++;
        } else {
            rx_buf[rx_head] = c;
            rx_head = next;
        }
    }
    scb->INTR_RX = rx | SCB_INTR_RX_NOT_EMPTY_Msk;

    if (scb->INTR_TX_MASK) {
        uint16_t tail = tx_tail;
        uint32_t used = (scb->TX_FIFO_STATUS & SCB_TX_FIFO_STATUS_USED_Msk) >> SCB_TX_FIFO_STATUS_USED_Pos;
        while (tail != tx_head && used < SCB_FIFO_BYTES) {
            scb->TX_FIFO_WR = tx_buf[tail];
            tail = (uint16_t)((tail + 1U) & RING_MASK(TX_RING));
            used++;
        }
        tx_tail = tail;
        if (tail == tx_head) {
            scb->INTR_TX_MASK = 0;
        }
        scb->INTR_TX = SCB_INTR_TX_TRIGGER_Msk;
    }
    (void)scb->INTR_RX;
}

void b6HwInputCapture(uint8_t on)
{
    inputCaptureEnable(on);
}

// SRSS reset cause (RES_CAUSE, bit 0 WDT, 4 SOFT, ...), then cleared so the
// next reset reports only its own cause
uint32_t b6HwResetCause(void)
{
    const uint32_t cause = SRSS->RES_CAUSE;
    SRSS->RES_CAUSE = 0xFFFFFFFFUL;
    SRSS->RES_CAUSE2 = 0xFFFFFFFFUL;
    return cause;
}

// die ID from SFLASH: lot (24 bit), wafer, X, Y, day, month (4 bit), year (4 bit)
uint64_t b6HwMcuId(void)
{
    const SFLASH_Type* sf = PSC3_SFLASH;
    return (uint64_t)sf->DIE_LOT[0] | ((uint64_t)sf->DIE_LOT[1] << 8) | ((uint64_t)sf->DIE_LOT[2] << 16)
        | ((uint64_t)sf->DIE_WAFER << 24) | ((uint64_t)sf->DIE_X << 32) | ((uint64_t)sf->DIE_Y << 40)
        | ((uint64_t)sf->DIE_DAY << 48) | ((uint64_t)(sf->DIE_MONTH & 0x0FU) << 56)
        | ((uint64_t)(sf->DIE_YEAR & 0x0FU) << 60);
}

/*
 * BOARD_ID: divider from the board's 3.3 V on BOARD_ID_ADC_CHANNEL.
 * 10k / 10k (half scale) is power stage #1, the DEV ESC 8S 60 A Rev B, which
 * also carries the power input of #1 (TLI4971, VBAT 20k / 1.2k). Anything
 * else, or no board (the input floats), reads as 0.
 */
#define BOARD_ID_HALF_SCALE 2048U
#define BOARD_ID_TOLERANCE  160U    // +-4 % of full scale: 1 % resistors, VDDA vs board 3.3 V

uint16_t b6HwPowerStageId(void)
{
#ifdef BOARD_ID_ADC_CHANNEL
    const uint16_t raw = ADC_raw_board_id;
    if (raw > BOARD_ID_HALF_SCALE - BOARD_ID_TOLERANCE && raw < BOARD_ID_HALF_SCALE + BOARD_ID_TOLERANCE) {
        return 1;
    }
#endif
    return 0;
}

uint16_t b6HwPowerInputId(void)
{
    return b6HwPowerStageId();     // one board since Rev B
}

uint16_t b6HwFlags(void)
{
    uint16_t flags = 0;
#ifdef USE_OCD_INPUTS
    const uint8_t ocd = ocdLatched();
    if (ocd & OCD1_BIT) {
        flags |= B6_FLAG_OCD1;
    }
    if (ocd & OCD2_BIT) {
        flags |= B6_FLAG_OCD2;
    }
#endif
#ifdef USE_6EDL7141
    if (edl7141_status != EDL_OK || edl7141FaultActive()) {
        flags |= B6_FLAG_DRIVER_FAULT;
    }
    if (edl7141_status == EDL_OK) {
        flags |= B6_FLAG_DRIVER_ENABLED;
    }
    if (gpioRead(EDL_BRAKE_PORT, EDL_BRAKE_PIN) == 0U) {
        flags |= B6_FLAG_NBRAKE;
    }
#endif
    return flags;
}

uint8_t b6HwFault(void)
{
#ifdef USE_OCD_INPUTS
    if (ocdLatched()) {
        return 1;
    }
#endif
#ifdef USE_6EDL7141
    if (edl7141_status != EDL_OK || edl7141FaultActive()) {
        return 1;
    }
#endif
    return 0;
}

uint8_t b6HwOcdTripped(void)
{
#ifdef USE_OCD_INPUTS
    return ocdTakeNew();
#else
    return 0;
#endif
}

void b6HwClearFaults(void)
{
#ifdef USE_OCD_INPUTS
    ocdClear();
#endif
}

#endif // USE_B6_PROTOCOL
