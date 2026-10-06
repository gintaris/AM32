/*
 * Host test for Mcu/psc3/Src/comparator_adc.c: the comparator emulation of
 * the BEMF_ADC targets, fed with synthetic SAR samples.
 */

#include "comparator.h"
#include "targets.h"
#include "unit.h"

volatile char rising;
char step;
volatile uint32_t commutation_interval;
static int zero_crosses;

void interruptRoutine(void)
{
    zero_crosses++;
}

// phase A, B, C samples with the floating phase at level
static void sample(int floating, uint16_t level, uint16_t interval_count)
{
    uint16_t s[3] = { 1000, 1000, 1000 };
    s[floating] = level;
    bemfAdcProcess(s, interval_count);
}

int main(void)
{
    const uint16_t bus = 3000;              // 12 bit DC link reading
    const uint16_t neutral = compReferenceFromBus(bus);
    CHECK(neutral == 1500, "neutral %u", neutral);

    initComparators();
    setCompReference(neutral);
    commutation_interval = 1000;            // 0.5 us ticks

    // step 2: phase A floats. Falling BEMF ('rising' = 0): rising comparator edge
    step = 2;
    rising = 0;
    sample(0, 1400, 0);
    changeCompInput();
    CHECK(getCompOutputLevel() == 0, "A below the neutral");
    enableCompInterrupts();
    sample(0, 1450, 600);
    CHECK(zero_crosses == 0, "no edge, no interrupt");
    sample(0, 1600, 300);
    CHECK(getCompOutputLevel() == 1, "A above the neutral");
    CHECK(zero_crosses == 0, "an edge in the first half of the interval is ignored");
    sample(0, 1400, 700);
    CHECK(zero_crosses == 0, "the opposite edge does not count");
    sample(0, 1600, 700);
    CHECK(zero_crosses == 1, "rising edge in the second half: zero cross");

    // masked: the level still follows, no interrupt
    maskPhaseInterrupts();
    sample(0, 1400, 900);
    sample(0, 1600, 900);
    CHECK(zero_crosses == 1, "masked");
    CHECK(getCompOutputLevel() == 1, "level follows while masked");

    // step 3: phase B floats, rising BEMF: falling comparator edge
    step = 3;
    rising = 1;
    sample(1, 1700, 0);
    changeCompInput();
    CHECK(getCompOutputLevel() == 1, "switching the input takes the level of B without an edge");
    CHECK(zero_crosses == 1, "no interrupt from the input switch");
    enableCompInterrupts();
    sample(1, 1800, 600);
    sample(1, 1200, 600);
    CHECK(zero_crosses == 2, "falling edge with rising BEMF");

    // steps 1 / 4 select phase C, 2 / 5 phase A, 3 / 6 phase B
    static const int floating[7] = { -1, 2, 0, 1, 2, 0, 1 };
    for (int st = 1; st <= 6; st++) {
        uint16_t s[3] = { 0, 0, 0 };
        s[floating[st]] = 4000;
        bemfAdcProcess(s, 0);
        step = (char)st;
        changeCompInput();
        CHECK(getCompOutputLevel() == 1, "step %d reads phase %c", st, 'A' + floating[st]);
    }

    CHECK(comparatorSliceMask() == 0, "no CSG slices in the ADC variant");

    return report("test_comparator_adc");
}
