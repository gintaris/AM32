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
 * Off time skew: the ADC sees nothing below 0 V, so in the off time the
 * threshold sits above the zero cross: a rising BEMF passes it late and a
 * falling one early by the same angle. That made the steps with a falling
 * cross short and the ones with a rising cross long (55 / 65 degrees at
 * 1000 rpm in the SITL with BEMF_OFF_THRESHOLD 4, the short steps seen on
 * the kit). The samples between the raw crosses measure it: falling to
 * rising is 60 degrees plus twice the skew, rising to falling 60 minus
 * twice the skew. A falling cross is held back by twice the skew, so both
 * come late by the skew alone, which only takes that much timing advance.
 * In the on time the neutral is symmetric and the hold decays to 0.
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

// off time skew compensation (see the top of the file)
#ifndef BEMF_FALL_HOLD_MAX
#define BEMF_FALL_HOLD_MAX 32U  // samples, a quarter step at most anyway
#endif
static uint16_t sample_count;   // SAR conversions, wraps
static uint16_t last_cross_count;
static uint8_t last_cross_rising = 0xFFU;   // none yet
static uint16_t span_fr;        // samples from a falling to a rising cross
static uint16_t span_rf;        // and from a rising to a falling one
static int32_t fall_hold_x16;   // filtered hold of a falling cross, 1/16 samples
static uint8_t fall_pending;    // a falling cross is being held
static uint8_t fall_hold;       // samples left to hold it

static void measureSkew(uint8_t cross_rising)
{
    const uint16_t span = (uint16_t)(sample_count - last_cross_count);
    last_cross_count = sample_count;
    const uint8_t previous = last_cross_rising;
    last_cross_rising = cross_rising;
    if (previous != (cross_rising ^ 1U)) {
        return;     // the first cross, or two of a kind (a missed one)
    }
    if (cross_rising) {
        span_fr = span;
    } else {
        span_rf = span;
    }
    if (span_fr == 0U || span_rf == 0U) {
        return;
    }
    // only a steady run tells the skew: spans that differ by more than half
    // their mean come from forced commutations at a speed where the BEMF is
    // lost in the noise (seen on the kit at 190 rpm and 4.5 V), not from
    // the threshold
    const int32_t diff = (int32_t)span_fr - (int32_t)span_rf;
    const int32_t sum = (int32_t)span_fr + (int32_t)span_rf;
    if (diff > sum / 4 || -diff > sum / 4) {
        return;
    }
    // hold = twice the skew = (span_fr - span_rf) / 2, at most a quarter
    // step; x16 for the filter
    int32_t target = diff * 8;
    const int32_t limit = sum * 2;
    if (target < 0) {
        target = 0;
    }
    if (target > limit) {
        target = limit;
    }
    if (target > (int32_t)(BEMF_FALL_HOLD_MAX * 16U)) {
        target = (int32_t)(BEMF_FALL_HOLD_MAX * 16U);
    }
    fall_hold_x16 += (target - fall_hold_x16) / 8;
}

#ifdef PSC3_BEMF_TRACE
static uint8_t levelOf(uint16_t sample);

// loss of sync trace: every SAR conversion while the motor runs goes into a
// ring buffer (170 ms at 24 kHz), which freezes BEMF_TRACE_AFTER samples
// after the first back EMF timeout (no zero cross for 45000 interval timer
// ticks, main.c) after the motor has run (more than 300 zero crosses).
// Read with the debugger: bemf_trace_frozen is 1 once
// frozen, bemf_trace_pos is then the oldest entry; write 0 to
// bemf_trace_frozen to record the next one. Per entry:
//   bemf_trace     step << 13 | level << 12 | 12 bit floating phase sample
//   bemf_trace_ic  interval timer count
//   bemf_trace_fl  BEMF_TRACE_F_* flags
#define BEMF_TRACE_LEN 1024U
#define BEMF_TRACE_AFTER 64U
#define BEMF_TRACE_F_BLANK 0x01U    // blanked after a commutation
#define BEMF_TRACE_F_EDGE 0x02U     // the crossing edge of the step seen
#define BEMF_TRACE_F_ZC 0x04U       // interruptRoutine() called
#define BEMF_TRACE_F_POLL 0x08U     // old_routine: polling mode
#define BEMF_TRACE_F_HELD 0x10U     // falling cross held
#define BEMF_TRACE_F_OFF 0x20U      // off time sampling
#define BEMF_TRACE_F_SEEN 0x40U     // level before the cross seen
#define BEMF_TRACE_F_INT 0x80U      // comparator interrupt enabled
extern uint8_t running;
extern char old_routine;
volatile uint16_t bemf_trace[BEMF_TRACE_LEN];
volatile uint16_t bemf_trace_ic[BEMF_TRACE_LEN];
volatile uint8_t bemf_trace_fl[BEMF_TRACE_LEN];
volatile uint16_t bemf_trace_pos;
volatile uint8_t bemf_trace_frozen;
// per commutation: a few seconds before the trigger, frozen with the
// sample trace. bemf_comm_pos is the oldest entry once frozen
#define BEMF_COMM_LEN 1536U
#define BEMF_COMM_F_POLL 0x01U      // polling mode at the end of the step
#define BEMF_COMM_F_OFF 0x02U       // off time sampling
#define BEMF_COMM_F_ZC 0x04U        // interruptRoutine() called in the step
#define BEMF_COMM_F_SEEN 0x08U      // level before the cross seen
#define BEMF_COMM_F_HELD 0x10U      // a falling cross was held
typedef struct {
    uint16_t samples;               // SAR samples in the step
    uint16_t commutation_interval;  // at the end of the step, saturated
    uint16_t duty_cycle;
    uint16_t peak;                  // highest floating phase sample
    uint8_t step;
    uint8_t flags;
    uint8_t edges;                  // crossing edges seen (bounces)
    uint8_t hold_x16;               // fall_hold_x16, saturated
} bemf_comm_t;
extern volatile uint16_t duty_cycle;
volatile bemf_comm_t bemf_comm[BEMF_COMM_LEN];
volatile uint16_t bemf_comm_pos;
static uint16_t comm_start = 0;     // sample_count at the step start
static uint16_t comm_peak;
static uint8_t comm_edges;
static uint8_t comm_flags;

static void traceCommutation(void)
{
    if (running && !bemf_trace_frozen) {
        volatile bemf_comm_t* e = &bemf_comm[bemf_comm_pos];
        e->samples = (uint16_t)(sample_count - comm_start);
        e->commutation_interval = commutation_interval > 0xFFFFU ? 0xFFFFU : (uint16_t)commutation_interval;
        e->duty_cycle = duty_cycle;
        e->peak = comm_peak;
        e->step = (uint8_t)step;
        e->flags = (uint8_t)(comm_flags | (old_routine ? BEMF_COMM_F_POLL : 0U)
            | (bemf_sample_mode == BEMF_SAMPLE_OFF ? BEMF_COMM_F_OFF : 0U)
            | (seen_before_cross ? BEMF_COMM_F_SEEN : 0U));
        e->edges = comm_edges;
        e->hold_x16 = fall_hold_x16 > 255 ? 255U : (uint8_t)fall_hold_x16;
        bemf_comm_pos = (uint16_t)((bemf_comm_pos + 1U) % BEMF_COMM_LEN);
    }
    comm_start = sample_count;
    comm_peak = 0;
    comm_edges = 0;
    comm_flags = 0;
}

extern volatile uint32_t zero_crosses;
static uint8_t trace_armed;
static uint16_t trace_after;    // samples left to record after the trigger
static uint16_t trace_idx = BEMF_TRACE_LEN;     // entry of this sample, LEN = none

#define TRACE_FLAG(f)     do {         if (trace_idx < BEMF_TRACE_LEN) {             bemf_trace_fl[trace_idx] |= (f);         }     } while (0)

static void traceSample(const uint16_t samples[3], uint16_t interval_count)
{
    trace_idx = BEMF_TRACE_LEN;
    if (!running || bemf_trace_frozen) {
        return;
    }
    if (trace_after != 0U) {
        if (--trace_after == 0U) {
            bemf_trace_frozen = 1;
            return;
        }
    } else if (trace_armed && interval_count > 45000U) {
        trace_after = BEMF_TRACE_AFTER;
    } else if (zero_crosses > 300U) {
        // the motor has run: the next back EMF timeout is the loss of sync,
        // not a failed start
        trace_armed = 1;
    }
    const uint16_t sample = samples[active_phase];
    if (!blank_samples && sample > comm_peak) {
        comm_peak = sample;
    }
    trace_idx = bemf_trace_pos;
    bemf_trace[trace_idx] = (uint16_t)(((uint16_t)(step & 7) << 13) | ((uint16_t)levelOf(sample) << 12)
        | (sample & 0xFFFU));
    bemf_trace_ic[trace_idx] = interval_count;
    bemf_trace_fl[trace_idx] = (uint8_t)((blank_samples ? BEMF_TRACE_F_BLANK : 0U)
        | (old_routine ? BEMF_TRACE_F_POLL : 0U) | (fall_pending ? BEMF_TRACE_F_HELD : 0U)
        | (bemf_sample_mode == BEMF_SAMPLE_OFF ? BEMF_TRACE_F_OFF : 0U)
        | (seen_before_cross ? BEMF_TRACE_F_SEEN : 0U) | (comp_int_enabled ? BEMF_TRACE_F_INT : 0U));
    bemf_trace_pos = (uint16_t)((bemf_trace_pos + 1U) % BEMF_TRACE_LEN);
}
#else
#define TRACE_FLAG(f)     do {     } while (0)
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
#ifdef PSC3_BEMF_TRACE
    traceCommutation();
#endif
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
    fall_pending = 0;
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
    sample_count++;
#ifdef PSC3_BEMF_TRACE
    traceSample(samples, interval_count);
#endif

    if (blank_samples != 0U) {
        blank_samples--;
        return;
    }
    if (bemf_sample_mode != bemf_sample_mode_req) {
        bemf_sample_mode = bemf_sample_mode_req;
        skip_samples = 2;
    }
    const uint8_t level = levelOf(samples[active_phase]);
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
        fall_pending = 0;   // a held falling cross that went back
        return;
    }
    // the crossing edge of this step: rising BEMF falling level, falling
    // BEMF rising level
    const uint8_t crossing = (rising && level == 0) || (!rising && level == 1);
    if (crossing) {
        TRACE_FLAG(BEMF_TRACE_F_EDGE);
#ifdef PSC3_BEMF_TRACE
        if (comm_edges < 255U) {
            comm_edges++;
        }
#endif
    }
    if (crossing && !fall_pending && seen_before_cross && interval_count > (commutation_interval >> 1)) {
        measureSkew(rising ? 1U : 0U);
    }
    if (crossing && !rising) {
        if (!fall_pending) {
            fall_pending = 1;
            fall_hold = (uint8_t)((fall_hold_x16 + 8) >> 4);
#ifdef PSC3_BEMF_TRACE
            if (fall_hold != 0U) {
                comm_flags |= BEMF_COMM_F_HELD;
            }
#endif
        }
        if (fall_hold != 0U) {
            fall_hold--;
            return;
        }
    }
    fall_pending = 0;
    comp_level = level;
    if (!comp_int_enabled || !seen_before_cross) {
        return;
    }
    // rising BEMF: falling comparator edge, falling BEMF: rising edge
    if ((rising && level == 0) || (!rising && level == 1)) {
        if (interval_count > (commutation_interval >> 1)) {
            TRACE_FLAG(BEMF_TRACE_F_ZC);
#ifdef PSC3_BEMF_TRACE
            comm_flags |= BEMF_COMM_F_ZC;
#endif
            interruptRoutine();
        }
    }
}

#endif /* BEMF_ADC */
