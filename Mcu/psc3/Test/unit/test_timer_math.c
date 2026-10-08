/*
 * Host test for the PSC3 timer clocks (psc3_timing.h): exact integer PCLK
 * dividers, tick lengths and the control loop rate.
 */

#include <stdint.h>
#include <stdio.h>

#include "psc3_timing.h"
#include "unit.h"

int main(void)
{
    // the dividers must be exact and fit the 16 bit PCLK dividers
    CHECK(PCLK_DIVIDE_2MHZ * INTERVAL_TIMER_HZ == PSC3_COUNTER_CLOCK_HZ, "2 MHz divider %lu",
        (unsigned long)PCLK_DIVIDE_2MHZ);
    CHECK(PCLK_DIVIDE_1MHZ * UTILITY_TIMER_HZ == PSC3_COUNTER_CLOCK_HZ, "1 MHz divider %lu",
        (unsigned long)PCLK_DIVIDE_1MHZ);
    CHECK(PCLK_DIVIDE_2MHZ >= 1 && PCLK_DIVIDE_2MHZ <= 65536, "2 MHz divider range");
    CHECK(PCLK_DIVIDE_1MHZ >= 1 && PCLK_DIVIDE_1MHZ <= 65536, "1 MHz divider range");
    CHECK(COM_TIMER_HZ == INTERVAL_TIMER_HZ, "COM_TIMER and INTERVAL_TIMER share the 2 MHz divider");
    CHECK(TEN_KHZ_TIMER_HZ == UTILITY_TIMER_HZ, "TEN_KHZ_TIMER and UTILITY_TIMER share the 1 MHz divider");

    // HPPASS CSG clock from an 8 bit divider, 10..120 MHz
    CHECK(PCLK_DIVIDE_CSG * CSG_CLOCK_HZ == PSC3_COUNTER_CLOCK_HZ, "CSG divider %lu", (unsigned long)PCLK_DIVIDE_CSG);
    CHECK(PCLK_DIVIDE_CSG >= 1 && PCLK_DIVIDE_CSG <= 256, "CSG divider range");
    CHECK(CSG_CLOCK_HZ >= 10000000UL && CSG_CLOCK_HZ <= 120000000UL, "CSG clock range");

    // AM32 expects 0.5 us interval ticks and 1 us utility ticks
    CHECK(1e9 / INTERVAL_TIMER_HZ == 500.0, "interval tick %.1f ns", 1e9 / INTERVAL_TIMER_HZ);
    CHECK(1e9 / UTILITY_TIMER_HZ == 1000.0, "utility tick %.1f ns", 1e9 / UTILITY_TIMER_HZ);

    // the control loop runs at exactly LOOP_FREQUENCY_HZ
    const double loop_hz = (double)TEN_KHZ_TIMER_HZ / (TEN_KHZ_TIMER_PERIOD + 1);
    printf("control loop %.1f Hz, 16 bit interval timer wraps after %.3f ms\n", loop_hz,
        65536.0 * 1e3 / INTERVAL_TIMER_HZ);
    CHECK(loop_hz == LOOP_FREQUENCY_HZ, "loop %.1f Hz", loop_hz);
    CHECK(TEN_KHZ_TIMER_PERIOD <= 0xFFFF, "loop period fits 16 bits");

    // a commutation interval in 0.5 us ticks converts to us like on the STM32
    // targets: 4000 ticks = 2 ms per step
    CHECK(4000UL * 1000000UL / INTERVAL_TIMER_HZ == 2000UL, "tick conversion");

    // settings row (eeprom.c): one 512 byte row in flash sector 1, away from
    // the firmware in sector 0; an erased row reads 0x00 and eeprom.c returns
    // it as 0xFF, so the default ERASED_FLASH_BYTE of sounds.c applies
    CHECK((EEPROM_START_ADD & 511U) == 0, "EEPROM_START_ADD row aligned");
    CHECK(EEPROM_START_ADD >= 0x12020000UL && EEPROM_START_ADD + 512U <= 0x12040000UL, "settings in sector 1");
    CHECK(DEVINFO_ADDRESS + 32U == 0x12001000UL, "device info at the end of the 4 kB bootloader");
#ifdef ERASED_FLASH_BYTE
    CHECK(0, "ERASED_FLASH_BYTE must not be set for the PSC3");
#endif

    return report("test_timer_math");
}
