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
uint8_t running;                // BEMF trace switch of comparator_adc.c
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

// changeCompInput() ignores the next two samples (commutation spike)
static void blanking(void)
{
    uint16_t s[3] = { 0, 0, 0 };
    int before = zero_crosses;
    bemfAdcProcess(s, 0xFFFF);
    bemfAdcProcess(s, 0xFFFF);
    CHECK(zero_crosses == before, "blanked samples give no zero cross");
}

int main(void)
{
    const uint16_t bus = 3000;              // 12 bit DC link reading
    const uint16_t neutral = compReferenceFromBus(bus);
    CHECK(neutral == 1500, "neutral %u", neutral);

    initComparators();
    setCompReference(neutral);
    commutation_interval = 1000;            // 0.5 us ticks

    // level as on the STM32 comparators: 1 while the floating phase is below
    // the neutral. Falling BEMF ('rising' = 0): the level rises (0 -> 1)
    step = 2;
    rising = 0;
    sample(0, 1600, 0);
    changeCompInput();
    CHECK(getCompOutputLevel() == 0, "A above the neutral reads 0");
    blanking();
    enableCompInterrupts();
    sample(0, 1550, 600);
    CHECK(zero_crosses == 0, "no edge, no interrupt");
    sample(0, 1400, 300);
    CHECK(getCompOutputLevel() == 1, "A below the neutral reads 1");
    CHECK(zero_crosses == 0, "an edge in the first half of the interval is ignored");
    sample(0, 1600, 700);
    CHECK(zero_crosses == 0, "the opposite edge does not count");
    sample(0, 1400, 700);
    CHECK(zero_crosses == 1, "falling BEMF crossing in the second half: zero cross");

    // masked: the level still follows, no interrupt
    maskPhaseInterrupts();
    sample(0, 1600, 900);
    sample(0, 1400, 900);
    CHECK(zero_crosses == 1, "masked");
    CHECK(getCompOutputLevel() == 1, "level follows while masked");

    // step 3: phase B floats, rising BEMF: the level falls (1 -> 0)
    step = 3;
    rising = 1;
    sample(1, 1200, 0);
    changeCompInput();
    CHECK(getCompOutputLevel() == 1, "switching the input takes the level of B without an edge");
    CHECK(zero_crosses == 1, "no interrupt from the input switch");
    blanking();
    enableCompInterrupts();
    sample(1, 1100, 600);
    sample(1, 1800, 600);
    CHECK(zero_crosses == 2, "rising BEMF crossing");

    // steps 1 / 4 select phase C, 2 / 5 phase A, 3 / 6 phase B
    static const int floating[7] = { -1, 2, 0, 1, 2, 0, 1 };
    for (int st = 1; st <= 6; st++) {
        uint16_t s[3] = { 4000, 4000, 4000 };
        s[floating[st]] = 0;
        bemfAdcProcess(s, 0);
        step = (char)st;
        changeCompInput();
        CHECK(getCompOutputLevel() == 1, "step %d reads phase %c", st, 'A' + floating[st]);
    }

    // off time sampling (low duty): threshold above 0 V instead of the neutral
    bemf_sample_mode_req = BEMF_SAMPLE_OFF;
    step = 2;
    rising = 1;
    commutation_interval = 1000;
    changeCompInput();
    blanking();
    enableCompInterrupts();
    int before = zero_crosses;
    sample(0, 900, 700);                    // first two samples after the switch: no edge
    sample(0, 0, 700);
    CHECK(zero_crosses == before, "mode switch skips two samples");
    CHECK(getCompOutputLevel() == 1, "0 V in the off time is below the threshold");
    sample(0, BEMF_OFF_THRESHOLD + 40, 700);
    CHECK(zero_crosses == before + 1, "rising BEMF above 0 V in the off time: zero cross");
    sample(0, 0, 700);
    CHECK(zero_crosses == before + 1, "falling back does not count with rising BEMF");
    sample(0, 1400, 700);
    CHECK(getCompOutputLevel() == 0, "1400 is far above the off time threshold");

    // demagnetization: a falling BEMF step that starts at 0 V must not count
    // as crossed until the phase has been seen above the threshold
    commutation_interval = 2000;           // middle of the interval at 1000
    step = 4;
    rising = 0;
    sample(2, 0, 0);
    changeCompInput();
    blanking();
    enableCompInterrupts();
    before = zero_crosses;
    sample(2, 0, 700);
    sample(2, 0, 800);
    CHECK(getCompOutputLevel() == 0, "clamped to 0 V: reads the level before the cross");
    CHECK(zero_crosses == before, "no zero cross during the demagnetization");
    sample(2, BEMF_OFF_THRESHOLD + 20, 900);
    CHECK(getCompOutputLevel() == 0, "falling BEMF above 0 V");
    sample(2, 0, 1100);
    CHECK(zero_crosses == before + 1, "falling BEMF reaches 0 V: zero cross");
    CHECK(getCompOutputLevel() == 1, "after the cross");

    // demagnetization hides the whole positive half: the cross is taken in
    // the middle of the commutation interval
    commutation_interval = 1000;
    step = 6;
    rising = 0;
    sample(1, 0, 0);
    changeCompInput();
    blanking();
    enableCompInterrupts();
    before = zero_crosses;
    sample(1, 0, 300);
    CHECK(getCompOutputLevel() == 0, "first half: the level before the cross");
    sample(1, 0, 400);
    CHECK(zero_crosses == before, "no zero cross in the first half");
    sample(1, 0, 501);
    CHECK(getCompOutputLevel() == 1, "middle of the interval: taken as crossed");

    // off time skew: rising to falling cross 8 samples (2 blanked, 5 before
    // the cross, the cross), falling to rising 12 (4 after the falling cross,
    // 2 blanked, 5, the cross); the threshold above 0 V delays the rising and
    // advances the falling cross. The falling cross is learned to be held
    // (12 - 8) / 2 samples
    commutation_interval = 1000;
    int hold = -1;
    for (int cycle = 0; cycle < 60; cycle++) {
        step = 1;                           // C floats, rising BEMF
        rising = 1;
        changeCompInput();
        blanking();
        enableCompInterrupts();
        for (int i = 0; i < 5; i++) {
            sample(2, 0, 900);
        }
        before = zero_crosses;
        sample(2, 100, 900);
        CHECK(zero_crosses == before + 1, "rising cross at once, cycle %d", cycle);

        step = 2;                           // A floats, falling BEMF
        rising = 0;
        changeCompInput();
        blanking();
        enableCompInterrupts();
        for (int i = 0; i < 5; i++) {
            sample(0, 100, 900);
        }
        before = zero_crosses;
        hold = -1;
        for (int i = 0; i < 5; i++) {
            sample(0, 0, 900);
            if (hold < 0 && zero_crosses != before) {
                hold = i;
            }
        }
    }
    CHECK(hold == 2, "falling cross held 2 samples, got %d", hold);

    // back to on time sampling: the neutral applies again
    bemf_sample_mode_req = BEMF_SAMPLE_ON;
    sample(0, 1400, 700);
    sample(0, 1400, 700);
    CHECK(getCompOutputLevel() == 1, "1400 is below the on time neutral");

    CHECK(comparatorSliceMask() == 0, "no CSG slices in the ADC variant");

    return report("test_comparator_adc");
}
