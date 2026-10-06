/*
 * psc3_it.c
 *
 * Interrupt handlers, the names come from the IRQn list of the device header.
 */

#include "main.h"

#include "IO.h"
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

extern volatile char armed;
extern volatile char dshot_telemetry;
extern volatile char out_put;
extern volatile uint8_t compute_dshot_flag;

// DataWire 0 channel of the signal input / bidirectional DShot output
void cpuss_interrupts_dw0_0_IRQHandler(void)
{
    DW0->CH_STRUCT[INPUT_DMA_CHANNEL].INTR = DW_CH_STRUCT_INTR_CH_Msk;
    (void)DW0->CH_STRUCT[INPUT_DMA_CHANNEL].INTR;

    if (armed && dshot_telemetry) {
        if (out_put) {
            receiveDshotDma();
            compute_dshot_flag = 2;
        } else {
            sendDshotDma();
            compute_dshot_flag = 1;
        }
    } else {
        transfercomplete();
    }
    // processDshot() at a lower priority, like the STM32 software EXTI
    NVIC_SetPendingIRQ(DSHOT_SW_IRQ);
}

// software interrupt for processDshot(), TCPWM counter 3 is otherwise unused
void tcpwm_0_interrupts_3_IRQHandler(void)
{
    processDshot();
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

#ifndef BEMF_ADC
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
#endif
