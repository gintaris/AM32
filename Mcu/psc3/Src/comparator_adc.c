/*
 * comparator_adc.c
 *
 * BEMF zero cross detection with the SAR ADC instead of the CSG comparators
 * (targets with BEMF_ADC, the KIT_PSC3M5_2GO rework).
 *
 * SAR entry 0 samples the three phase dividers at the same time once every
 * PWM period (TCPWM counter 261 CC1 match, see ADC.c), in the middle of the
 * off time at low duty and in the middle of the on time at high duty (see
 * BEMF_SAMPLE_ON / BEMF_SAMPLE_OFF in comparator.h). In the on time the
 * floating phase is compared with VBUS / 2 from the DC link reading (phase
 * dividers with the ratio of the DC link divider), in the off time with
 * BEMF_OFF_THRESHOLD above 0 V. The result emulates the comparator of the
 * STM32 targets:
 *   getCompOutputLevel()  '1' when the floating phase is below the neutral
 *                         (STM32 comparators have the phase on the minus
 *                         input; found on the kit: with the opposite level
 *                         every second step commutated at once and the
 *                         others never)
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
// a zero cross needs the level before the cross first: after a commutation
// the new floating phase is clamped to 0 V while its current decays through
// a body diode (demagnetization), which reads as "already crossed" for a
// falling BEMF. A comparator gives no edge for that (and the STM32 one
// toggles on the off time noise); the emulation holds the level before the
// cross until it has been seen in this step. Found on the kit at 5 V, where
// the demagnetization lasted longer than the first half of the step and hid
// the whole positive half of a falling BEMF: if the level before the cross
// has not shown up by the middle of the commutation interval, the cross is
// taken there (the timing of a symmetric zero cross)
static volatile uint8_t seen_before_cross;
static volatile uint16_t neutral_raw = COMP_REFERENCE_DEFAULT;
static uint16_t last_sample[3];

// requested by setPwmDutyAll() together with the new CC1; the SAR samples of
// the period still running were taken at the old point, so two samples are
// skipped after a change
volatile uint8_t bemf_sample_mode_req = BEMF_SAMPLE_ON;
static uint8_t bemf_sample_mode = BEMF_SAMPLE_ON;
static uint8_t skip_samples;
static uint8_t blank_samples;   // ignored completely after a commutation

#ifdef PSC3_BEMF_TRACE
// bring-up trace: floating phase sample, level and step of every PWM period
// from the start of the motor (running set) until the buffer is full, about
// 340 ms at 24 kHz. Read bemf_trace with the debugger, write 0 to
// bemf_trace_pos to record the next start. Entry: step << 13 | level << 12 |
// 12 bit sample
#define BEMF_TRACE_LEN 8192U
extern uint8_t running;
volatile uint16_t bemf_trace[BEMF_TRACE_LEN];
volatile uint16_t bemf_trace_pos;
#endif

// level as on the STM32 comparators (phase on the minus input): '1' while
// the floating phase is below the neutral
static uint8_t levelOf(uint16_t sample)
{
    if (bemf_sample_mode == BEMF_SAMPLE_OFF) {
        return sample <= BEMF_OFF_THRESHOLD;
    }
    return sample <= neutral_raw;
}

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

// level before the zero cross: 1 (below the neutral) for a rising BEMF
static uint8_t levelBeforeCross(void)
{
    return rising ? 1U : 0U;
}

uint8_t getCompOutputLevel(void)
{
    return seen_before_cross ? comp_level : levelBeforeCross();
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
    comp_level = levelOf(last_sample[active_phase]);
    seen_before_cross = (comp_level == levelBeforeCross());
    // the first samples after a commutation can carry the switching spike
    blank_samples = 2;
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

    if (blank_samples != 0U) {
        blank_samples--;
        return;
    }
    if (bemf_sample_mode != bemf_sample_mode_req) {
        bemf_sample_mode = bemf_sample_mode_req;
        skip_samples = 2;
    }
    const uint8_t level = levelOf(samples[active_phase]);
#ifdef PSC3_BEMF_TRACE
    if (running && bemf_trace_pos < BEMF_TRACE_LEN) {
        bemf_trace[bemf_trace_pos++] = (uint16_t)(((uint16_t)(step & 7) << 13) | ((uint16_t)level << 12)
            | (samples[active_phase] & 0xFFFU));
    }
#endif
    if (level == levelBeforeCross() || interval_count > (commutation_interval >> 1)) {
        seen_before_cross = 1;
    }
    if (skip_samples != 0U) {
        // follow the level without an edge, like changeCompInput()
        skip_samples--;
        comp_level = level;
        return;
    }
    if (level == comp_level) {
        return;
    }
    comp_level = level;
    if (!comp_int_enabled || !seen_before_cross) {
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
