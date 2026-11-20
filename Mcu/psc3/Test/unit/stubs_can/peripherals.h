/*
 * host test stub for Src/DroneCAN/sys_can_psc3.c: the real CANFD and SFLASH
 * register layouts (cyip_canfd_v3.h, cyip_sflash_psc3.h) over plain memory,
 * the clock / pin / NVIC helpers recorded by the test
 */
#ifndef PERIPHERALS_H_
#define PERIPHERALS_H_

#include <stdbool.h>
#include <stdint.h>

#include "ip/cyip_canfd_v3.h"
#include "ip/cyip_sflash_psc3.h"

// CANFD0 registers and its message RAM at + 0x10000
extern uint32_t fake_canfd[(0x10000 + 4096) / 4];
#define CANFD0              ((CANFD_Type*)(void*)fake_canfd)

extern SFLASH_Type fake_sflash;
#define PSC3_SFLASH         ((const SFLASH_Type*)&fake_sflash)

typedef enum {
    PCLK_CANFD0_CLOCK_CAN_EN0 = 0x0405u,
    PCLK_CANFD0_CLOCK_CAN_EN1 = 0x0406u,
} en_clk_dst_t;

#define CY_MMIO_CANFD0_GROUP_NR     3u
#define CY_MMIO_CANFD0_SLAVE_NR     0u
#define canfd_0_interrupts0_1_IRQn  101

#define GPIO_DM_HIGHZ       0UL
#define GPIO_DM_STRONG      6UL
#define HSIOM_SEL_GPIO      0UL

void gpioWrite(uint32_t port, uint32_t pin, uint32_t level);
void gpioSetMode(uint32_t port, uint32_t pin, uint32_t drive, uint32_t hsiom);
void enablePeriSlave(uint32_t group, uint32_t slave);
void pclkDivider16_5(uint32_t group, uint32_t div, uint32_t divide_by);
void connectPclk16_5(en_clk_dst_t dst, uint32_t div);
void NVIC_SetPriority(int irq, uint32_t prio);
void NVIC_ClearPendingIRQ(int irq);
void NVIC_EnableIRQ(int irq);
void NVIC_DisableIRQ(int irq);

#endif
