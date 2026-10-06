/*
 * comparator_adc.c
 *
 * BEMF zero cross detection with the SAR ADC instead of the CSG comparators
 * (targets with BEMF_ADC, the KIT_PSC3M5_2GO rework).
 *
 * SAR entry 0 samples the three phase dividers at the same time in the middle
 * of every PWM on time (TCPWM counter 261 CC1 match, see ADC.c). While the
 * PWM is on, the motor neutral sits at half the bus voltage, so the floating
 * phase is compared with VBUS / 2 from the DC link reading (phase dividers
 * with the ratio of the DC link divider). The result emulates the comparator
 * of the STM32 targets:
 *   getCompOutputLevel()  '1' when the floating phase is above the neutral
 *   interrupt             on the comparator edge selected by 'rising'; an
 *                         edge in the first half of the commutation interval
 *                         is ignored like in the STM32 comparator interrupts
 * The zero cross is found with the PWM period resolution (41.7 us at 24 kHz).
 *
 * This file has no register access so the host unit tests can run it.
 */

#include "comparator.h"

#include "targets.h"

#ifdef BEMF_ADC

extern volatile uint32_t commutation_interval;
extern void interruptRoutine(void);

static volatile uint8_t active_phase;
static volatile uint8_t comp_level;
static volatile uint8_t comp_int_enabled;
static volatile uint16_t neutral_raw = COMP_REFERENCE_DEFAULT;
static uint16_t last_sample[3];

void initComparators(void)
{
    active_phase = 0;
    comp_level = 0;
    comp_int_enabled = 0;
}

// no CSG slices are used
uint32_t comparatorSliceMask(void)
{
    return 0;
}

uint8_t getCompOutputLevel(void)
{
    return comp_level;
}

void maskPhaseInterrupts(void)
{
    comp_int_enabled = 0;
}

void enableCompInterrupts(void)
{
    comp_int_enabled = 1;
}

void changeCompInput(void)
{
    if (step == 1 || step == 4) { // C floating
        active_phase = 2;
    } else if (step == 2 || step == 5) { // A floating
        active_phase = 0;
    } else { // B floating
        active_phase = 1;
    }
    // the output follows the new phase at once without an edge, like a
    // comparator input switch with the interrupt masked
    comp_level = last_sample[active_phase] > neutral_raw;
}

// neutral in 12 bit ADC counts
void setCompReference(uint16_t reference)
{
    neutral_raw = reference;
}

uint16_t compReferenceFromBus(uint16_t adc_raw_volts)
{
    return (uint16_t)(adc_raw_volts >> 1);
}

// called by the SAR entry 0 interrupt with the phase A, B, C samples
void bemfAdcProcess(const uint16_t samples[3], uint16_t interval_count)
{
    last_sample[0] = samples[0];
    last_sample[1] = samples[1];
    last_sample[2] = samples[2];

    const uint8_t level = samples[active_phase] > neutral_raw;
    if (level == comp_level) {
        return;
    }
    comp_level = level;
    if (!comp_int_enabled) {
        return;
    }
    // rising BEMF: falling comparator edge, falling BEMF: rising edge
    if ((rising && level == 0) || (!rising && level == 1)) {
        if (interval_count > (commutation_interval >> 1)) {
            interruptRoutine();
        }
    }
}

#endif /* BEMF_ADC */
