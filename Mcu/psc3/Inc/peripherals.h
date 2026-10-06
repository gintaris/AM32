/*
 * peripherals.h
 *
 * PSC3 core peripherals and the timer macros used by the shared AM32 code.
 * The AM32 timers map to TCPWM counters (see targets.h, MCU_PSC3):
 *  - TIM1 (PWM)      : group 1 counters 260..262, PCLK 16 bit divider 0
 *  - input capture   : counter 516, PCLK 16 bit divider 1
 *  - INTERVAL / COM  : counters 512 / 513, 2 MHz, PCLK 16 bit divider 2
 *  - UTILITY / 10 kHz: counters 514 / 515, 1 MHz, PCLK 16 bit divider 3
 */

#ifndef PERIPHERALS_H_
#define PERIPHERALS_H_

#include "ADC.h"
#include "main.h"
#include "psc3_timing.h"

// PERI_PCLK group 5 serves the TCPWM counters
#define PCLK_TCPWM_GROUP    5
#define PCLK_DIV_PWM        0
#define PCLK_DIV_INPUT      1
#define PCLK_DIV_2MHZ       2
#define PCLK_DIV_1MHZ       3
#define PCLK_DIV8_CSG       0       // 8 bit divider 0

// PWM counters in TCPWM group 1
#define PWM_CNT_FIRST       4       // counter 260
#define PWM_CNT_LAST        6       // counter 262
#define PWM_TIMER(n)        TCPWM_CNT(1, (n))

#define INTERVAL_TIMER_COUNT (INTERVAL_TIMER->COUNTER)
#define SET_INTERVAL_TIMER_COUNT(intertime) (INTERVAL_TIMER->COUNTER = (intertime))

#define DISABLE_COM_TIMER_INT() (COM_TIMER->INTR_MASK = 0)
#define ENABLE_COM_TIMER_INT() (COM_TIMER->INTR_MASK = TCPWM_INTR_TC)
#define SET_AND_ENABLE_COM_INT(time)                            \
    (COM_TIMER->COUNTER = 0, COM_TIMER->PERIOD = (time),        \
        COM_TIMER->INTR = TCPWM_INTR_TC, COM_TIMER->INTR_MASK = TCPWM_INTR_TC)

/*
 * The three PWM counters share two TCPWM general purpose input triggers that
 * software pulses through PERI trigger group 9 (TRM 24.2.5), so they start
 * and take new buffer values at the same clock:
 *  - tr_all_cnt_in[0], counter input 5: start
 *  - tr_all_cnt_in[1], counter input 6: switch (capture0), the buffered
 *    PERIOD / CC0 are copied at the next terminal count
 * A high and a low gate of one phase come from different counters, so a
 * switch that reached only some counters would leave one phase with two
 * duty cycles for a PWM period.
 */
#define PWM_TRIG_GROUP          9UL     // PERI trigger group 9 -> tcpwm[0].tr_all_cnt_in[0..3]
#define PWM_TRIG_START          0UL
#define PWM_TRIG_SWITCH         1UL
#define PWM_TRIG_INPUT_START    5UL     // TR_IN_SEL value of tr_all_cnt_in[0]
#define PWM_TRIG_INPUT_SWITCH   6UL     // TR_IN_SEL value of tr_all_cnt_in[1]

// pulse a trigger to all PWM counters (two clock cycles, Cy_TrigMux_SwTrigger)
static inline void pwmTrigger(uint32_t line)
{
    for (uint32_t i = 0; (PERI->TR_CMD & PERI_TR_CMD_ACTIVATE_Msk) && i < 100U; i++) {
    }
    PERI->TR_CMD = PERI_TR_CMD_ACTIVATE_Msk | PERI_TR_CMD_OUT_SEL_Msk | PERI_TR_CMD_TR_EDGE_Msk
        | (PWM_TRIG_GROUP << PERI_TR_CMD_GROUP_SEL_Pos) | line;
}

// period and compare go to the buffer registers and are copied at the next
// terminal count, like the STM32 preload registers
static inline void setPwmPeriod(uint32_t relval)
{
    PWM_TIMER(4)->PERIOD_BUFF = relval;
    PWM_TIMER(5)->PERIOD_BUFF = relval;
    PWM_TIMER(6)->PERIOD_BUFF = relval;
    pwmTrigger(PWM_TRIG_SWITCH);
}

static inline void setPwmDutyAll(uint32_t newdc)
{
    PWM_TIMER(4)->CC0_BUFF = newdc;
    PWM_TIMER(5)->CC0_BUFF = newdc;
    PWM_TIMER(6)->CC0_BUFF = newdc;
    // CC1 of counter 261 triggers the SAR in the middle of the on time (ADC.c)
    PWM_TIMER(5)->CC1_BUFF = newdc >> 1;
    pwmTrigger(PWM_TRIG_SWITCH);
}

// the PWM prescaler is the PCLK divider of the PWM counters, presc + 1
// like the STM32 PSC register
#define SET_PRESCALER_PWM(presc) setPwmPrescaler(presc)
#define SET_AUTO_RELOAD_PWM(relval) setPwmPeriod(relval)
#define SET_DUTY_CYCLE_ALL(newdc) setPwmDutyAll(newdc)

#define RELOAD_WATCHDOG_COUNTER() reloadWatchDogCounter()

void initAfterJump(void);
void initCorePeripherals(void);
void enableCorePeripherals(void);
void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_IWDG_Init(void);
void reloadWatchDogCounter(void);
void resetInputCaptureTimer(void);
void setPWMCompare1(uint16_t compareone);
void setPWMCompare2(uint16_t comparetwo);
void setPWMCompare3(uint16_t comparethree);
void generatePwmTimerEvent(void);
void setPwmPrescaler(uint16_t presc);
void setPwmDeadTime(uint16_t deadtime);
void initPwm(void);
void startPwm(void);
void initTimers(void);
void startTimers(void);
void setPclkDivider(uint32_t div16, uint32_t divide_by);

#endif /* PERIPHERALS_H_ */
