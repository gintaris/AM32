/*
 * phaseouts.c
 *
 * Gate output states for PSC3. The three PWM counters run the same waveform,
 * every gate pin picks its signal with the LINE_SEL field of its counter:
 *   PWM phase : high gate PWM, low gate inverted PWM (complementary) or '0'
 *   LOW phase : high gate '0', low gate '1'
 *   FLOAT     : both '0'
 * The dead time is inserted after the line selection on every rising edge,
 * so a change of LINE_SEL cannot turn on both gates of a phase even though
 * they belong to different counters. The IRSM836 inputs are active high.
 */

#include "phaseouts.h"

#include "peripherals.h"
#include "targets.h"

#if defined(USE_INVERTED_HIGH) || defined(USE_INVERTED_LOW)
#error inverted gate outputs would get the dead time on the wrong edge
#endif

extern char prop_brake_active;
extern volatile uint8_t temp_comp_pwm;

typedef struct {
    uint8_t cnt;    // counter in TCPWM group 1
    uint8_t shift;  // LINE_SEL.OUT_SEL or LINE_SEL.COMPL_OUT_SEL
} gate_t;

#define GATE(cnt, compl) { (cnt), (compl) ? TCPWM_GRP_CNT_LINE_SEL_COMPL_OUT_SEL_Pos : TCPWM_GRP_CNT_LINE_SEL_OUT_SEL_Pos }

static const gate_t gate_high[3] = {
    GATE(PHASE_A_HIGH_CNT, PHASE_A_HIGH_COMPL),
    GATE(PHASE_B_HIGH_CNT, PHASE_B_HIGH_COMPL),
    GATE(PHASE_C_HIGH_CNT, PHASE_C_HIGH_COMPL),
};

static const gate_t gate_low[3] = {
    GATE(PHASE_A_LOW_CNT, PHASE_A_LOW_COMPL),
    GATE(PHASE_B_LOW_CNT, PHASE_B_LOW_COMPL),
    GATE(PHASE_C_LOW_CNT, PHASE_C_LOW_COMPL),
};

enum { PHASE_A, PHASE_B, PHASE_C };

// shadow of LINE_SEL for counters 260..262, all gates off
static uint32_t line_sel[3];

static void setGate(const gate_t* gate, uint32_t sel)
{
    uint32_t* reg = &line_sel[gate->cnt - PWM_CNT_FIRST];
    *reg = (*reg & ~(TCPWM_GRP_CNT_LINE_SEL_OUT_SEL_Msk << gate->shift)) | (sel << gate->shift);
}

static void phasePwm(int phase)
{
    setGate(&gate_high[phase], TCPWM_LINE_PWM);
    setGate(&gate_low[phase], temp_comp_pwm ? TCPWM_LINE_PWM_INV : TCPWM_LINE_L);
}

static void phaseLow(int phase)
{
    setGate(&gate_high[phase], TCPWM_LINE_L);
    setGate(&gate_low[phase], TCPWM_LINE_H);
}

static void phaseFloat(int phase)
{
    setGate(&gate_high[phase], TCPWM_LINE_L);
    setGate(&gate_low[phase], TCPWM_LINE_L);
}

static void applyGates(void)
{
    for (uint32_t i = 0; i < 3U; i++) {
        PWM_TIMER(PWM_CNT_FIRST + i)->LINE_SEL = line_sel[i];
    }
}

void allOff(void)
{
    phaseFloat(PHASE_A);
    phaseFloat(PHASE_B);
    phaseFloat(PHASE_C);
    applyGates();
}

void comStep(int newStep)
{
    switch (newStep) {
    case 1: // A-B
        phasePwm(PHASE_A);
        phaseLow(PHASE_B);
        phaseFloat(PHASE_C);
        break;
    case 2: // C-B
        phaseFloat(PHASE_A);
        phaseLow(PHASE_B);
        phasePwm(PHASE_C);
        break;
    case 3: // C-A
        phaseLow(PHASE_A);
        phaseFloat(PHASE_B);
        phasePwm(PHASE_C);
        break;
    case 4: // B-A
        phaseLow(PHASE_A);
        phasePwm(PHASE_B);
        phaseFloat(PHASE_C);
        break;
    case 5: // B-C
        phaseFloat(PHASE_A);
        phasePwm(PHASE_B);
        phaseLow(PHASE_C);
        break;
    case 6: // A-C
        phasePwm(PHASE_A);
        phaseFloat(PHASE_B);
        phaseLow(PHASE_C);
        break;
    default:
        return;
    }
    applyGates();
}

// short all low sides
void fullBrake(void)
{
    phaseLow(PHASE_A);
    phaseLow(PHASE_B);
    phaseLow(PHASE_C);
    applyGates();
}

// for stepper_sine, needs per phase duty cycles (not on the 2GO kit)
void allpwm(void)
{
    phasePwm(PHASE_A);
    phasePwm(PHASE_B);
    phasePwm(PHASE_C);
    applyGates();
}

// high sides off, low sides on the complementary PWM: the duty cycle sets
// the share of coasting against braking
void proportionalBrake(void)
{
    for (int phase = PHASE_A; phase <= PHASE_C; phase++) {
        setGate(&gate_high[phase], TCPWM_LINE_L);
        setGate(&gate_low[phase], TCPWM_LINE_PWM_INV);
    }
    applyGates();
}

void twoChannelForward(void)
{
    phasePwm(PHASE_A);
    phaseLow(PHASE_B);
    phasePwm(PHASE_C);
    applyGates();
}

void twoChannelReverse(void)
{
    phaseLow(PHASE_A);
    phasePwm(PHASE_B);
    phaseLow(PHASE_C);
    applyGates();
}
