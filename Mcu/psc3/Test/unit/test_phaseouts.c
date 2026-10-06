/*
 * Host test for Mcu/psc3/Src/phaseouts.c with the KIT_2GO_PSC3 pin map.
 * Evaluates every gate pin for PWM '1' and '0' and checks the electrical
 * phase states of each commutation step.
 */

#include <stdio.h>
#include <string.h>

#include "peripherals.h"
#include "phaseouts.h"
#include "targets.h"
#include "unit.h"

TCPWM_GRP_CNT_Type fake_pwm_cnt[8];
volatile uint8_t temp_comp_pwm;
char prop_brake_active;

typedef struct {
    int cnt;
    int compl;
} pin_t;

static const pin_t high_pin[3] = {
    { PHASE_A_HIGH_CNT, PHASE_A_HIGH_COMPL },
    { PHASE_B_HIGH_CNT, PHASE_B_HIGH_COMPL },
    { PHASE_C_HIGH_CNT, PHASE_C_HIGH_COMPL },
};
static const pin_t low_pin[3] = {
    { PHASE_A_LOW_CNT, PHASE_A_LOW_COMPL },
    { PHASE_B_LOW_CNT, PHASE_B_LOW_COMPL },
    { PHASE_C_LOW_CNT, PHASE_C_LOW_COMPL },
};

// level of a gate pin while the PWM signal 'line' is pwm
static int pinLevel(pin_t p, int pwm)
{
    const uint32_t shift = p.compl ? TCPWM_GRP_CNT_LINE_SEL_COMPL_OUT_SEL_Pos : 0;
    const uint32_t sel = (fake_pwm_cnt[p.cnt].LINE_SEL >> shift) & 7U;
    switch (sel) {
    case TCPWM_LINE_L:
        return 0;
    case TCPWM_LINE_H:
        return 1;
    case TCPWM_LINE_PWM:
        return pwm;
    case TCPWM_LINE_PWM_INV:
        return !pwm;
    }
    return -1;
}

enum { P_FLOAT, P_LOW, P_HIGH, P_SHORT, P_BAD };

static int phaseState(int phase, int pwm)
{
    const int h = pinLevel(high_pin[phase], pwm);
    const int l = pinLevel(low_pin[phase], pwm);
    if (h < 0 || l < 0) {
        return P_BAD;
    }
    if (h && l) {
        return P_SHORT;
    }
    return h ? P_HIGH : (l ? P_LOW : P_FLOAT);
}

/*
 * expected per phase: 'P' PWM (high while pwm, else low with complementary
 * PWM or floating), 'L' low, 'F' floating, 'B' low side on the inverted PWM
 */
static void expectStep(const char* name, const char* expect, int comp)
{
    for (int phase = 0; phase < 3; phase++) {
        for (int pwm = 0; pwm <= 1; pwm++) {
            const int s = phaseState(phase, pwm);
            int want;
            CHECK(s != P_SHORT, "%s phase %c shoots through", name, 'A' + phase);
            switch (expect[phase]) {
            case 'P':
                want = pwm ? P_HIGH : (comp ? P_LOW : P_FLOAT);
                break;
            case 'L':
                want = P_LOW;
                break;
            case 'B':
                want = pwm ? P_FLOAT : P_LOW;
                break;
            default:
                want = P_FLOAT;
                break;
            }
            CHECK(s == want, "%s comp %d phase %c pwm %d: state %d, want %d", name, comp,
                'A' + phase, pwm, s, want);
        }
    }
}

static void testPinMapIsUnique(void)
{
    const pin_t all[6] = { high_pin[0], low_pin[0], high_pin[1], low_pin[1], high_pin[2], low_pin[2] };
    for (int i = 0; i < 6; i++) {
        CHECK(all[i].cnt >= PWM_CNT_FIRST && all[i].cnt <= PWM_CNT_LAST, "gate %d counter %d", i, all[i].cnt);
        for (int j = i + 1; j < 6; j++) {
            CHECK(all[i].cnt != all[j].cnt || all[i].compl != all[j].compl,
                "gates %d and %d share one output", i, j);
        }
    }
}

int main(void)
{
    static const char* const steps[7] = { "", "PLF", "FLP", "LFP", "LPF", "FPL", "PFL" };

    testPinMapIsUnique();

    for (int comp = 0; comp <= 1; comp++) {
        temp_comp_pwm = (uint8_t)comp;
        memset(fake_pwm_cnt, 0xff, sizeof(fake_pwm_cnt));
        allOff();
        expectStep("allOff", "FFF", comp);
        for (int step = 1; step <= 6; step++) {
            char name[16];
            snprintf(name, sizeof(name), "comStep %d", step);
            comStep(step);
            expectStep(name, steps[step], comp);
        }
        comStep(0); // an invalid step leaves the outputs alone
        expectStep("comStep 0", steps[6], comp);
        fullBrake();
        expectStep("fullBrake", "LLL", comp);
        allpwm();
        expectStep("allpwm", "PPP", comp);
        twoChannelForward();
        expectStep("twoChannelForward", "PLP", comp);
        twoChannelReverse();
        expectStep("twoChannelReverse", "LPL", comp);
        proportionalBrake();
        expectStep("proportionalBrake", "BBB", comp);
    }

    return report("test_phaseouts");
}
