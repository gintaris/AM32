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
extern volatile uint32_t commutation_interval;

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

// combined HPPASS CSG comparator interrupt, BEMF zero cross. Edges in the
// first half of the commutation interval are ignored like on the STM32 targets
void pass_interrupt_csg_cmps_IRQHandler(void)
{
    HPPASS->CSG.CMP_INTR = HPPASS->CSG.CMP_INTR;
    (void)HPPASS->CSG.CMP_INTR;
    if (INTERVAL_TIMER_COUNT > (commutation_interval >> 1)) {
        interruptRoutine();
    }
}
