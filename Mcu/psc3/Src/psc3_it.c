/*
 * psc3_it.c
 *
 * Interrupt handlers, the names come from the IRQn list of the device header.
 */

#include "main.h"

#include "peripherals.h"
#include "targets.h"

extern void transfercomplete(void);
extern void PeriodElapsedCallback(void);
extern void interruptRoutine(void);
extern void tenKhzRoutine(void);
extern void processDshot(void);

void HardFault_Handler(void)
{
    while (1) {
    }
}

// COM_TIMER, counter 513
void tcpwm_0_interrupts_513_IRQHandler(void)
{
    COM_TIMER->INTR = TCPWM_INTR_TC;
    PeriodElapsedCallback();
}

// TEN_KHZ_TIMER, counter 515
void tcpwm_0_interrupts_515_IRQHandler(void)
{
    TEN_KHZ_TIMER->INTR = TCPWM_INTR_TC;
    tenKhzRoutine();
}
