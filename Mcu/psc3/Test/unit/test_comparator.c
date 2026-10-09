/*
 * Host test for Mcu/psc3/Src/comparator.c with the KIT_2GO_PSC3 slice map:
 * floating phase per commutation step, interrupt edge, interrupt mask and
 * the DAC virtual neutral.
 */

#include <string.h>

#include "comparator.h"
#include "targets.h"
#include "unit.h"

HPPASS_Type fake_hppass;
volatile char rising;
char step;
static int nvic_enabled;

void NVIC_ClearPendingIRQ(int irq)
{
    (void)irq;
}

void NVIC_EnableIRQ(int irq)
{
    nvic_enabled = (irq == pass_interrupt_csg_cmps_IRQn);
}

static uint32_t edgeOf(int slice)
{
    return (fake_hppass.CSG.SLICE[slice].CMP_CFG >> HPPASS_CSG_SLICE_CMP_CFG_CMP_EDGE_MODE_Pos) & 3U;
}

int main(void)
{
    // commutation step -> floating phase -> slice
    static const int floating_slice[7] = { -1, PHASE_C_COMP_CSG, PHASE_A_COMP_CSG, PHASE_B_COMP_CSG,
        PHASE_C_COMP_CSG, PHASE_A_COMP_CSG, PHASE_B_COMP_CSG };

    memset(&fake_hppass, 0xff, sizeof(fake_hppass));
    initComparators();
    CHECK(nvic_enabled, "comparator interrupt enabled in the NVIC");
    CHECK(fake_hppass.CSG.CMP_INTR_MASK == 0, "interrupts masked after init");
    CHECK(comparatorSliceMask() == ((1U << 2) | (1U << 3) | (1U << 4)), "slice mask %x",
        (unsigned)comparatorSliceMask());
    for (int s = 2; s <= 4; s++) {
        CHECK(fake_hppass.CSG.SLICE[s].CMP_CFG == CSG_POS_AN_B, "slice %d: AN_B on +, DAC on -, no edge", s);
        CHECK(fake_hppass.CSG.SLICE[s].DAC_VAL == COMP_REFERENCE_DEFAULT, "slice %d DAC", s);
    }

    for (int r = 0; r <= 1; r++) {
        for (int st = 1; st <= 6; st++) {
            rising = (char)r;
            step = (char)st;
            changeCompInput();
            enableCompInterrupts();
            const int slice = floating_slice[st];
            CHECK(fake_hppass.CSG.CMP_INTR_MASK == (1U << slice), "step %d: mask %x", st,
                (unsigned)fake_hppass.CSG.CMP_INTR_MASK);
            // phase on the CSG + input: rising BEMF -> rising CSG edge (1), falling -> falling (2)
            CHECK(edgeOf(slice) == (r ? 1U : 2U), "step %d rising %d: edge %u", st, r, (unsigned)edgeOf(slice));
            // AM32 level as on the STM32: 1 while the phase is below the neutral
            fake_hppass.CSG.SLICE[slice].CMP_STATUS = 1;
            CHECK(getCompOutputLevel() == 0, "step %d: phase above the neutral reads 0", st);
            fake_hppass.CSG.SLICE[slice].CMP_STATUS = 0;
            CHECK(getCompOutputLevel() == 1, "step %d: phase below the neutral reads 1", st);
            maskPhaseInterrupts();
            CHECK(fake_hppass.CSG.CMP_INTR_MASK == 0, "masked");
        }
    }

    // virtual neutral: half of the 12 bit DC link reading as 10 bit DAC code
    CHECK(compReferenceFromBus(4095) == 511, "full scale bus -> %u", compReferenceFromBus(4095));
    CHECK(compReferenceFromBus(2000) == 250, "2000 -> %u", compReferenceFromBus(2000));
    setCompReference(300);
    for (int s = 2; s <= 4; s++) {
        CHECK(fake_hppass.CSG.SLICE[s].DAC_VAL == 300, "slice %d DAC follows the bus", s);
    }
    setCompReference(0xFFFF);
    CHECK(fake_hppass.CSG.SLICE[2].DAC_VAL == 0x3FF, "DAC code limited to 10 bits");

    return report("test_comparator");
}
