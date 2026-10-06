/*
 * Host test for the PSC3 PWM timing: period, dead time and the sound
 * prescaler emulation (PCLK divider = presc + 1).
 */

#include <stdint.h>
#include <stdio.h>

#include "targets.h"
#include "unit.h"

int main(void)
{
    const double f_cnt = CPU_FREQUENCY_MHZ * 1e6;

    // TCPWM up counting: PERIOD + 1 clocks per PWM period
    const double f_pwm = f_cnt / (TIM1_AUTORELOAD + 1);
    CHECK(TIM1_AUTORELOAD == 7499, "TIM1_AUTORELOAD %d", (int)TIM1_AUTORELOAD);
    CHECK(f_pwm > 23999.0 && f_pwm < 24001.0, "PWM frequency %.1f Hz", f_pwm);
    CHECK(TIM1_AUTORELOAD <= 0xFFFF, "the period must fit the 16 bit group 1 counters");

    // dead time in counter clocks
    const double dt_ns = DEAD_TIME * 1e9 / f_cnt;
    printf("dead time %d clocks = %.1f ns, PWM %.1f Hz\n", DEAD_TIME, dt_ns, f_pwm);
    CHECK(dt_ns > 250.0 && dt_ns < 1000.0, "dead time %.1f ns", dt_ns);
    CHECK(DEAD_TIME < TIM1_AUTORELOAD, "the dead time must not exceed the period");

    // sounds.c: SET_PRESCALER_PWM(9), reload = CPU_FREQUENCY_MHZ * 100000 / freq
    for (int freq = 400; freq <= 8000; freq *= 2) {
        const uint32_t reload = (uint16_t)(CPU_FREQUENCY_MHZ * 100000 / freq);
        const double f_out = f_cnt / (9 + 1) / (reload + 1);
        CHECK(f_out > freq * 0.99 && f_out < freq * 1.01, "%d Hz tone is %.1f Hz", freq, f_out);
    }

    // signal.c: ic_timer_prescaler = CPU_FREQUENCY_MHZ - 1 gives 1 us capture ticks
    CHECK(f_cnt / ((CPU_FREQUENCY_MHZ - 1) + 1) == 1e6, "servo capture clock");

    return report("test_pwm_math");
}
