/*
 * system_cat1b.h
 *
 * Minimal replacement for the BSP system header expected by the PSC3 device
 * headers. AM32 provides its own SystemInit and clock setup in peripherals.c.
 */

#ifndef SYSTEM_CAT1B_H_
#define SYSTEM_CAT1B_H_

#include <stdint.h>

extern uint32_t SystemCoreClock;

void SystemInit(void);
void SystemCoreClockUpdate(void);

#endif /* SYSTEM_CAT1B_H_ */
