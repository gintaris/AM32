/*
 * ocd.c
 *
 * TLI4971 fast over-current outputs OCD1 / OCD2 (open drain, active low,
 * pull-up on the power stage, 1 nF to ground) on two pins of one port
 * (targets with USE_OCD_INPUTS, EVK_PSC3: P2.0 J24-14, P2.1 J24-11).
 *
 * A falling edge interrupts at priority 0, like the commutation, so the
 * gates go off about 0.5 us after the pin edge (not measured yet): all phases float,
 * the BEMF and commutation interrupts are masked (including one that is
 * already pending), the motor is disarmed and the signal input switched off,
 * so DShot cannot arm again while the over-current is latched. Without a
 * signal the main loop resets the MCU after 2 s, which also releases the
 * latch; with protocol B6 a DISARM request releases it (ocdClear()) once the
 * pins are high again.
 *
 * P2.0 is also USER BTN2 of the KIT_PSC3M5_EVK, which tests the whole path.
 */

#include "ocd.h"

#include "IO.h"
#include "comparator.h"
#include "peripherals.h"
#include "phaseouts.h"
#include "targets.h"

#ifdef USE_OCD_INPUTS

#define GPIO_EDGE_FALLING   2UL     // INTR_CFG EDGEx_SEL

extern volatile char armed;
extern volatile char inputSet;
extern uint8_t running;
extern char old_routine;
extern char stepper_sine;
extern volatile uint32_t zero_crosses;
extern uint16_t input;
extern volatile uint16_t newinput;
extern uint16_t adjusted_input;
extern uint16_t duty_cycle_setpoint;

volatile uint16_t ocd_trips;
static volatile uint8_t latched;
static volatile uint8_t fresh;

static uint8_t pinsLow(void)
{
    uint8_t bits = 0;
    if (gpioRead(OCD_PORT, OCD1_PIN) == 0U) {
        bits |= OCD1_BIT;
    }
    if (gpioRead(OCD_PORT, OCD2_PIN) == 0U) {
        bits |= OCD2_BIT;
    }
    return bits;
}

// priority 0 or interrupts off: nothing else commutates meanwhile
static void trip(uint8_t bits)
{
    allOff();
    maskPhaseInterrupts();
    DISABLE_COM_TIMER_INT();
    NVIC_ClearPendingIRQ(COM_TIMER_IRQ);
    NVIC_ClearPendingIRQ(COMPARATOR_IRQ);
    armed = 0;
    inputSet = 0;
    running = 0;
    old_routine = 1;
    stepper_sine = 0;
    zero_crosses = 0;
    newinput = 0;
    adjusted_input = 0;
    input = 0;
    duty_cycle_setpoint = 0;
    inputCaptureEnable(0);
    latched |= bits;
    fresh |= bits;
    ocd_trips++;
}

void ocdInit(void)
{
    GPIO_PRT_Type* prt = &GPIO->PRT[OCD_PORT];

    // the internal pull-up keeps the pins high without a power stage; the
    // pull-up drive mode needs the output register high (low drives strong)
    gpioWrite(OCD_PORT, OCD1_PIN, 1);
    gpioWrite(OCD_PORT, OCD2_PIN, 1);
    gpioSetMode(OCD_PORT, OCD1_PIN, GPIO_DM_PULLUP, HSIOM_SEL_GPIO);
    gpioSetMode(OCD_PORT, OCD2_PIN, GPIO_DM_PULLUP, HSIOM_SEL_GPIO);

    prt->INTR_CFG = (prt->INTR_CFG & ~((3UL << (OCD1_PIN * 2U)) | (3UL << (OCD2_PIN * 2U))))
        | (GPIO_EDGE_FALLING << (OCD1_PIN * 2U)) | (GPIO_EDGE_FALLING << (OCD2_PIN * 2U));
    prt->INTR = (1UL << OCD1_PIN) | (1UL << OCD2_PIN);
    prt->INTR_MASK |= (1UL << OCD1_PIN) | (1UL << OCD2_PIN);
    NVIC_SetPriority(OCD_IRQ, 0);
    NVIC_ClearPendingIRQ(OCD_IRQ);
    NVIC_EnableIRQ(OCD_IRQ);

    // already low (no edge): latch it now
    const uint8_t low = pinsLow();
    if (low) {
        __disable_irq();
        trip(low);
        __enable_irq();
    }
}

uint8_t ocdLatched(void)
{
    return latched;
}

uint8_t ocdTakeNew(void)
{
    __disable_irq();
    const uint8_t bits = fresh;
    fresh = 0;
    __enable_irq();
    return bits;
}

void ocdClear(void)
{
    __disable_irq();
    latched &= pinsLow();
    __enable_irq();
}

void OCD_IRQ_HANDLER(void)
{
    GPIO_PRT_Type* prt = &GPIO->PRT[OCD_PORT];
    const uint32_t pending = prt->INTR & ((1UL << OCD1_PIN) | (1UL << OCD2_PIN));
    prt->INTR = pending;    // write 1 to clear
    (void)prt->INTR;

    uint8_t bits = 0;
    if (pending & (1UL << OCD1_PIN)) {
        bits |= OCD1_BIT;
    }
    if (pending & (1UL << OCD2_PIN)) {
        bits |= OCD2_BIT;
    }
    if (bits) {
        trip(bits);
    }
}

#endif // USE_OCD_INPUTS
