/*
 * comparator.c
 *
 * BEMF comparators on the HPPASS CSG slices, one slice per phase:
 *   + input: the phase voltage divider (AN_Ak or AN_Bk)
 *   - input: the slice DAC as virtual neutral, or a neutral divider on
 *            AN_A(k+1) / AN_B(k+1)
 * The comparator output is '1' when the phase is above the neutral, like
 * the STM32 targets (phase on the + input, common on the - input).
 * changeCompInput() picks the slice of the floating phase and its interrupt
 * edge, the combined CSG comparator interrupt calls interruptRoutine()
 * (psc3_it.c).
 */

#include "comparator.h"

#include "targets.h"

static const uint8_t comp_slice[3] = { PHASE_A_COMP_CSG, PHASE_B_COMP_CSG, PHASE_C_COMP_CSG };
static const uint8_t comp_pos[3] = { PHASE_A_COMP_POS, PHASE_B_COMP_POS, PHASE_C_COMP_POS };
static const uint8_t comp_neg[3] = { PHASE_A_COMP_NEG, PHASE_B_COMP_NEG, PHASE_C_COMP_NEG };

#define CMP_EDGE_DISABLE    0UL
#define CMP_EDGE_RISING     1UL
#define CMP_EDGE_FALLING    2UL

static uint32_t active_slice = PHASE_A_COMP_CSG;

uint32_t comparatorSliceMask(void)
{
    return (1UL << PHASE_A_COMP_CSG) | (1UL << PHASE_B_COMP_CSG) | (1UL << PHASE_C_COMP_CSG);
}

static uint32_t cmpConfig(uint32_t phase, uint32_t edge)
{
    return (comp_pos[phase] << HPPASS_CSG_SLICE_CMP_CFG_CMP_POS_SEL_Pos)
        | (comp_neg[phase] << HPPASS_CSG_SLICE_CMP_CFG_CMP_NEG_SEL_Pos)
        | (edge << HPPASS_CSG_SLICE_CMP_CFG_CMP_EDGE_MODE_Pos);
}

// configure before hppassStart(), the AC enables the slices
void initComparators(void)
{
    HPPASS->CSG.CSG_CTRL = 0;   // no DAC to the SAR
    HPPASS->CSG.CMP_INTR_MASK = 0;
    for (uint32_t phase = 0; phase < 3U; phase++) {
        HPPASS_CSG_SLICE_Type* slice = &HPPASS->CSG.SLICE[comp_slice[phase]];
        slice->CMP_CFG = cmpConfig(phase, CMP_EDGE_DISABLE);
        slice->DAC_CFG = 0;     // buffered mode, the DAC follows DAC_VAL writes
        slice->DAC_VAL = COMP_REFERENCE_DEFAULT;
    }
    HPPASS->CSG.CMP_INTR = comparatorSliceMask();
    NVIC_ClearPendingIRQ(COMPARATOR_IRQ);
    NVIC_EnableIRQ(COMPARATOR_IRQ);
}

uint8_t getCompOutputLevel(void)
{
    return HPPASS->CSG.SLICE[active_slice].CMP_STATUS & HPPASS_CSG_SLICE_CMP_STATUS_CMP_VAL_Msk;
}

void maskPhaseInterrupts(void)
{
    HPPASS->CSG.CMP_INTR_MASK = 0;
    HPPASS->CSG.CMP_INTR = comparatorSliceMask();
}

void enableCompInterrupts(void)
{
    HPPASS->CSG.CMP_INTR_MASK = 1UL << active_slice;
}

void changeCompInput(void)
{
    uint32_t phase;

    if (step == 1 || step == 4) { // C floating
        phase = 2;
    } else if (step == 2 || step == 5) { // A floating
        phase = 0;
    } else { // B floating
        phase = 1;
    }
    active_slice = comp_slice[phase];
    // rising BEMF: interrupt on the falling comparator edge, like the STM32 targets
    HPPASS->CSG.SLICE[active_slice].CMP_CFG = cmpConfig(phase, rising ? CMP_EDGE_FALLING : CMP_EDGE_RISING);
}

// set the virtual neutral of the slices that compare against their DAC
void setCompReference(uint16_t dac)
{
    for (uint32_t phase = 0; phase < 3U; phase++) {
        if (comp_neg[phase] == CSG_NEG_DAC) {
            HPPASS->CSG.SLICE[comp_slice[phase]].DAC_VAL = dac & HPPASS_CSG_SLICE_DAC_VAL_VALUE_Msk;
        }
    }
}

// half the bus voltage as 10 bit DAC code from the 12 bit DC link reading,
// for phase dividers with the ratio of the DC link divider
uint16_t compReferenceFromBus(uint16_t adc_raw_volts)
{
    return (uint16_t)(adc_raw_volts >> 3);
}
