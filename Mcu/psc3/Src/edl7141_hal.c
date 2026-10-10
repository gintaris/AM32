/*
 * edl7141_hal.c
 *
 * PSC3 hooks of the 6EDL7141 driver (edl7141.c): SPI master on an SCB,
 * nSCS, EN_DRV and nFAULT as GPIOs (targets with USE_6EDL7141).
 *
 * The SCB moves bytes (its frames are at most 16 bits), so one 24 bit
 * 6EDL7141 frame is three bytes with nSCS held low by software. SPI mode:
 * SCLK idles low, the 6EDL7141 samples SDI on the falling edge and drives
 * SDO on the rising one (CPOL 0, CPHA 1). Timing (datasheet table 7, SPI):
 * clock period >= 60 ns, nSCS setup and hold >= 50 ns, nSCS high between
 * frames >= 450 ns; 2 MHz and 1 us gaps leave a wide margin.
 */

#include "edl7141.h"

#include "functions.h"
#include "peripherals.h"
#include "targets.h"

#ifdef USE_6EDL7141

#define SCB_MODE_SPI            1UL
#define SCB_SPI_MOTOROLA        0UL
#define SCB_MEM_WIDTH_BYTE      0UL
#define SCB_DATA_WIDTH_8        7UL
#define EDL_SPI_PCLK_GROUP      ((uint32_t)EDL_SPI_PCLK >> 8)
#define EDL_SPI_PCLK_DIV        1U      // 16 bit divider 1 of the SCB PCLK group (0 is the telemetry UART)
#define EDL_SPI_CLK_DIVIDE      9U      // 180 MHz / 9 = 20 MHz clk_scb
#define EDL_SPI_OVS             10U     // 20 MHz / 10 = 2 MHz SCLK (SPI master: 4..16)
#define EDL_SPI_TIMEOUT         2000U   // RX polls for 3 bytes, ~12 us at 2 MHz

void edl7141HwInit(void)
{
    CySCB_Type* scb = EDL_SPI_SCB;

    // EN_DRV low first (the 6EDL7141 has an internal pull-down), nSCS high
    gpioWrite(EDL_EN_PORT, EDL_EN_PIN, 0);
    gpioSetMode(EDL_EN_PORT, EDL_EN_PIN, GPIO_DM_STRONG, HSIOM_SEL_GPIO);
    gpioWrite(EDL_CS_PORT, EDL_CS_PIN, 1);
    gpioSetMode(EDL_CS_PORT, EDL_CS_PIN, GPIO_DM_STRONG, HSIOM_SEL_GPIO);
    // nFAULT: open drain with an external pull-up to DVDD
    gpioSetMode(EDL_FAULT_PORT, EDL_FAULT_PIN, GPIO_DM_HIGHZ, HSIOM_SEL_GPIO);

    enablePeriSlave(EDL_SPI_PERI_GROUP, EDL_SPI_PERI_SLAVE);
    pclkDivider16(EDL_SPI_PCLK_GROUP, EDL_SPI_PCLK_DIV, EDL_SPI_CLK_DIVIDE);
    connectPclk(EDL_SPI_PCLK, EDL_SPI_PCLK_DIV);

    scb->CTRL = 0;  // disabled while configured
    scb->CTRL = ((EDL_SPI_OVS - 1UL) << SCB_CTRL_OVS_Pos)
        | (SCB_MEM_WIDTH_BYTE << SCB_CTRL_MEM_WIDTH_Pos)
        | (SCB_MODE_SPI << SCB_CTRL_MODE_Pos);
    scb->SPI_CTRL = SCB_SPI_CTRL_MASTER_MODE_Msk
        | (SCB_SPI_MOTOROLA << SCB_SPI_CTRL_MODE_Pos)
        | SCB_SPI_CTRL_CPHA_Msk;                // CPOL 0
    scb->TX_CTRL = (SCB_DATA_WIDTH_8 << SCB_TX_CTRL_DATA_WIDTH_Pos) | SCB_TX_CTRL_MSB_FIRST_Msk;
    scb->RX_CTRL = (SCB_DATA_WIDTH_8 << SCB_RX_CTRL_DATA_WIDTH_Pos) | SCB_RX_CTRL_MSB_FIRST_Msk;
    scb->TX_FIFO_CTRL = SCB_TX_FIFO_CTRL_CLEAR_Msk;
    scb->TX_FIFO_CTRL = 0;
    scb->RX_FIFO_CTRL = SCB_RX_FIFO_CTRL_CLEAR_Msk;
    scb->RX_FIFO_CTRL = 0;
    scb->CTRL |= SCB_CTRL_ENABLED_Msk;

    // SCLK low (CPOL 0) and MOSI driven by the SCB, MISO as input
    gpioSetMode(EDL_SPI_PORT, EDL_SPI_CLK_PIN, GPIO_DM_STRONG, EDL_SPI_HSIOM_SEL);
    gpioSetMode(EDL_SPI_PORT, EDL_SPI_MOSI_PIN, GPIO_DM_STRONG, EDL_SPI_HSIOM_SEL);
    gpioSetMode(EDL_SPI_PORT, EDL_SPI_MISO_PIN, GPIO_DM_HIGHZ, EDL_SPI_HSIOM_SEL);
}

uint32_t edl7141SpiFrame(uint32_t frame)
{
    CySCB_Type* scb = EDL_SPI_SCB;
    uint32_t in = 0;

    scb->RX_FIFO_CTRL = SCB_RX_FIFO_CTRL_CLEAR_Msk;
    scb->RX_FIFO_CTRL = 0;

    gpioWrite(EDL_CS_PORT, EDL_CS_PIN, 0);
    delayMicros(1);
    scb->TX_FIFO_WR = (frame >> 16) & 0xFFUL;
    scb->TX_FIFO_WR = (frame >> 8) & 0xFFUL;
    scb->TX_FIFO_WR = frame & 0xFFUL;
    for (uint32_t i = 0; i < EDL_SPI_TIMEOUT; i++) {
        if (((scb->RX_FIFO_STATUS & SCB_RX_FIFO_STATUS_USED_Msk) >> SCB_RX_FIFO_STATUS_USED_Pos) >= 3U) {
            break;
        }
        delayMicros(1);
    }
    for (uint32_t i = 0; i < 3U; i++) {
        in = (in << 8) | (scb->RX_FIFO_RD & 0xFFUL);
    }
    delayMicros(1);
    gpioWrite(EDL_CS_PORT, EDL_CS_PIN, 1);
    delayMicros(1);     // nSCS high >= 450 ns before the next frame
    return in;
}

void edl7141SetEnable(uint8_t on)
{
    gpioWrite(EDL_EN_PORT, EDL_EN_PIN, on ? 1U : 0U);
}

uint8_t edl7141FaultActive(void)
{
    return gpioRead(EDL_FAULT_PORT, EDL_FAULT_PIN) == 0U;
}

void edl7141DelayUs(uint32_t us)
{
    delayMicros(us);
}

#endif /* USE_6EDL7141 */
