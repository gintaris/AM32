/*
 * peripherals.c
 *
 * PSC3 clocks, GPIO, watchdog and the PWM / timer TCPWM counters.
 */

#include "peripherals.h"

#include "ADC.h"
#include "IO.h"
#include "comparator.h"
#include "serial_telemetry.h"
#include "targets.h"

void initAfterJump(void)
{
    __enable_irq();
}

void MX_GPIO_Init(void)
{
    // DP3
}

void MX_IWDG_Init(void)
{
    // DP3
}

void reloadWatchDogCounter(void)
{
    // DP3
}

void setPclkDivider(uint32_t div16, uint32_t divide_by)
{
    (void)div16;
    (void)divide_by;
    // DP3
}

void setPwmPrescaler(uint16_t presc)
{
    setPclkDivider(PCLK_DIV_PWM, (uint32_t)presc + 1);
}

void setPWMCompare1(uint16_t compareone)
{
    (void)compareone;
    // DP4
}

void setPWMCompare2(uint16_t comparetwo)
{
    (void)comparetwo;
    // DP4
}

void setPWMCompare3(uint16_t comparethree)
{
    (void)comparethree;
    // DP4
}

void generatePwmTimerEvent(void)
{
    // DP4
}

void resetInputCaptureTimer(void)
{
    // DP8
}

void initCorePeripherals(void)
{
    SystemClock_Config();
    MX_GPIO_Init();
    // DP4 PWM, DP5 timers, DP6 comparators, DP8 input, DP10 telemetry
#ifdef USE_SERIAL_TELEMETRY
    telem_UART_Init();
#endif
}

void enableCorePeripherals(void)
{
    // DP4 / DP5: start the counters, DP7: start the ADC
#ifdef USE_ADC
    ADC_Init();
    enableADC_DMA();
    activateADC();
#endif
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
