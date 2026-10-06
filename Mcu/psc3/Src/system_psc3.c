/*
 * system_psc3.c
 *
 * PSC3 clock setup, called from Reset_Handler before main(). Kept apart from
 * peripherals.c so the bring-up tests (Mcu/psc3/Test) can use it alone.
 */

#include "main.h"

#ifndef CPU_FREQUENCY_MHZ
#define CPU_FREQUENCY_MHZ 180
#endif

uint32_t SystemCoreClock = CPU_FREQUENCY_MHZ * 1000000UL;

void SystemInit(void)
{
    // DP3: clock tree, flash wait states
}

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = CPU_FREQUENCY_MHZ * 1000000UL;
}

void SystemClock_Config(void)
{
    // DP3: 180 MHz CLK_HF0 and the peripheral clocks for TCPWM and HPPASS
}
