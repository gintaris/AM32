/*
 * psc3_timing.h
 *
 * Counter clocks of the PSC3 MCU layer, without device dependencies so the
 * host unit tests can check them. All TCPWM counters are clocked from
 * CLK_HF3 = CPU_FREQUENCY_MHZ through 16 bit PCLK dividers.
 */

#ifndef PSC3_TIMING_H_
#define PSC3_TIMING_H_

#include "targets.h"

#define PSC3_COUNTER_CLOCK_HZ   (CPU_FREQUENCY_MHZ * 1000000UL)

#define INTERVAL_TIMER_HZ       2000000UL   // 0.5 us ticks like the STM32 targets
#define COM_TIMER_HZ            2000000UL
#define UTILITY_TIMER_HZ        1000000UL   // get_timer_us16()
#define TEN_KHZ_TIMER_HZ        1000000UL

// PCLK divider values (divide by), shared by the 2 MHz and 1 MHz counters
#define PCLK_DIVIDE_2MHZ        (PSC3_COUNTER_CLOCK_HZ / INTERVAL_TIMER_HZ)
#define PCLK_DIVIDE_1MHZ        (PSC3_COUNTER_CLOCK_HZ / UTILITY_TIMER_HZ)

// terminal count every PERIOD + 1 ticks
#define TEN_KHZ_TIMER_PERIOD    (TEN_KHZ_TIMER_HZ / LOOP_FREQUENCY_HZ - 1UL)

#endif /* PSC3_TIMING_H_ */
