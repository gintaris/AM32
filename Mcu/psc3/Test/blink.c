/*
 * blink.c
 *
 * Bring-up test for the KIT_PSC3M5_2GO: uses the AM32 startup, linker script
 * and SystemInit, then toggles the LED at 1 Hz from SysTick. A correct blink
 * rate confirms the boot stub, the startup code and the clock setup.
 */

#include "main.h"

#define BLINK_LED_PORT  LED_PORT
#define BLINK_LED_PIN   LED_PIN

static volatile uint32_t ms_ticks;

void SysTick_Handler(void)
{
    ms_ticks++;
}

int main(void)
{
    GPIO_PRT_Type* port = &GPIO->PRT[BLINK_LED_PORT];
    const uint32_t shift = BLINK_LED_PIN * 4U;

    // HSIOM: GPIO (0), drive mode strong (6) with the input buffer on (bit 3)
    HSIOM->PRT[BLINK_LED_PORT].PORT_SEL0 &= ~(0xFFUL << (BLINK_LED_PIN * 8U));
    port->CFG = (port->CFG & ~(0xFUL << shift)) | (0xEUL << shift);

    SysTick_Config(SystemCoreClock / 1000U);

    while (1) {
        if (ms_ticks >= 500U) {
            ms_ticks = 0;
            port->OUT_INV = 1UL << BLINK_LED_PIN;
        }
    }
}
