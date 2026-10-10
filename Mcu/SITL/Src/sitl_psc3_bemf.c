/*
  sitl_psc3_bemf.c - PSC3 BEMF sensing in the SITL (make SITL_PSC3_BEMF=1)

  Runs the BEMF zero cross detection of the PSC3 port
  (Mcu/psc3/Src/comparator_adc.c, built here unchanged) against the SITL
  motor model instead of the STM32 comparator emulation of comparator.c:
  once per PWM period motor.c samples the three phase divider nodes at the
  point the PSC3 SAR would (middle of the off time at low duty, middle of
  the on time at high duty, bemfSamplePoint() in Mcu/psc3/Inc/peripherals.h)
  as 12 bit ADC counts and pends the SITL comparator interrupt. Its handler
  (sitl_it.c) runs bemfAdcProcess() with the firmware parked, as the PSC3
  SAR interrupt does, and a zero cross calls interruptRoutine() directly.

  The divider ratio and the ADC full scale follow the ESC60A_PSC3 board:
  11:1 dividers, 3.3 V VDDA reference.
 */

#ifdef SITL_PSC3_BEMF

#include <stdbool.h>
#include <stdint.h>

#include "sitl.h"

// comparator_adc.c provides getCompOutputLevel(), wrapped below for the
// SITL scheduler; its interruptRoutine() calls go to main.c as on the PSC3
#define BEMF_ADC
#define getCompOutputLevel psc3_getCompOutputLevel
// comparator_adc.c includes "comparator.h", which is the SITL one here; the
// BEMF_ADC part of the PSC3 comparator.h is in bemf_adc.h
#include "comparator.h"
#include "../../psc3/Inc/bemf_adc.h"
#define COMP_REFERENCE_DEFAULT COMP_REFERENCE_ADC_DEFAULT
#include "../../psc3/Src/comparator_adc.c"
#undef getCompOutputLevel

#ifndef SITL_PSC3_DIVIDER
#define SITL_PSC3_DIVIDER 11.0      // phase and VBAT dividers 20k / 2k
#endif
#define SITL_PSC3_VREF 3.3
#define SITL_PSC3_FULL_SCALE 4095.0

extern volatile uint8_t temp_comp_pwm;

static uint16_t to_counts(double volts)
{
    double c = volts / SITL_PSC3_DIVIDER / SITL_PSC3_VREF * SITL_PSC3_FULL_SCALE;
    if (c < 0) {
        c = 0;
    }
    if (c > SITL_PSC3_FULL_SCALE) {
        c = SITL_PSC3_FULL_SCALE;
    }
    return (uint16_t)(c + 0.5);
}

uint8_t getCompOutputLevel(void)
{
    // same scheduler accounting as the comparator emulation
    if (sitl_in_sim_thread()) {
        sitl_isr_read_tick();
    } else {
        sitl_fw_progress();
    }
    return psc3_getCompOutputLevel();
}

/*
  sample point of the PSC3 port for a new duty (setPwmDutyAll()): middle
  of the off time below 45 % with complementary PWM, middle of the on time
  above 55 %, hysteresis in between. Returns the TIM1 count to sample at
 */
uint32_t sitl_psc3_sample_point(uint32_t duty, uint32_t arr)
{
    const uint32_t period = arr + 1U;
    uint8_t mode = bemf_sample_mode_req;

    if (!temp_comp_pwm || duty * 100U > period * 55U) {
        mode = BEMF_SAMPLE_ON;
    } else if (duty * 100U < period * 45U) {
        mode = BEMF_SAMPLE_OFF;
    }
    bemf_sample_mode_req = mode;
    return (mode == BEMF_SAMPLE_OFF) ? duty + ((period - duty) >> 1) : duty >> 1;
}

// TIM1 count of the PSC3 SAR trigger for the active duty (CC1 of counter 261)
uint32_t sitl_psc3_sample_count(uint32_t ccr, uint32_t arr)
{
    const uint32_t period = arr + 1U;
    return (bemf_sample_mode_req == BEMF_SAMPLE_OFF) ? ccr + ((period - ccr) >> 1) : ccr >> 1;
}

// the latest SAR conversion, handed from the sim step to the interrupt
static struct {
    bool pending;
    uint16_t samples[3];
    uint16_t bus;
} sar;

// one SAR conversion (sim thread, motor step): phase nodes and bus voltage
// in volts. Only the latest one is kept, like the SAR result registers.
// Nothing here may read a firmware timer: from the sim thread a register
// read is an ISR read tick, which would run pending handlers inside the
// motor step while the firmware thread is not parked
void sitl_psc3_bemf_sample(const double v_phase[3], double vbus)
{
    sar.samples[0] = to_counts(v_phase[0]);
    sar.samples[1] = to_counts(v_phase[1]);
    sar.samples[2] = to_counts(v_phase[2]);
    sar.bus = to_counts(vbus);
    sar.pending = true;
    // the firmware boots with interrupts disabled for most of a second.
    // An interrupt pended all that time is reported as blocked by the
    // dispatcher, and that report can deadlock on the stdio lock of a
    // firmware thread parked inside its own boot messages
    static bool irqs_enabled_once;
    if (!irqs_enabled_once) {
        irqs_enabled_once = (sitl_primask_get() == 0);
    }
    if (irqs_enabled_once) {
        sitl_irq_pend(SITL_IRQ_COMP);
    }
}

// SAR interrupt (sitl_it.c comparator handler), firmware parked
void sitl_psc3_bemf_irq(void)
{
    if (!sar.pending) {
        return;
    }
    sar.pending = false;
    // the 1 kHz ADC callback updates the neutral from the DC link reading
    setCompReference(compReferenceFromBus(sar.bus));
    bemfAdcProcess(sar.samples, (uint16_t)sitl_interval_timer_count());
}

#endif /* SITL_PSC3_BEMF */
