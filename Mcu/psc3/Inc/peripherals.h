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

// PERI_PCLK group 5 serves the TCPWM counters
#define PCLK_TCPWM_GROUP    5
#define PCLK_DIV_PWM        0
#define PCLK_DIV_INPUT      1
#define PCLK_DIV_2MHZ       2
#define PCLK_DIV_1MHZ       3

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

// the PWM prescaler is the PCLK divider of the PWM counters, presc + 1
// like the STM32 PSC register
#define SET_PRESCALER_PWM(presc) setPwmPrescaler(presc)
// period and compare go to the buffer registers, they are swapped in at the
// next terminal count like the STM32 preload registers
#define SET_AUTO_RELOAD_PWM(relval)                                           \
    (PWM_TIMER(4)->PERIOD_BUFF = (relval), PWM_TIMER(5)->PERIOD_BUFF = (relval), \
        PWM_TIMER(6)->PERIOD_BUFF = (relval))
#define SET_DUTY_CYCLE_ALL(newdc)                                       \
    (PWM_TIMER(4)->CC0_BUFF = (newdc), PWM_TIMER(5)->CC0_BUFF = (newdc), \
        PWM_TIMER(6)->CC0_BUFF = (newdc))

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
void setPclkDivider(uint32_t div16, uint32_t divide_by);

#endif /* PERIPHERALS_H_ */
