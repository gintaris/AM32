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

// PCLK divider type for CLOCK_CTL / DIV_CMD
#define PCLK_DIV_TYPE_16    1UL

// watchdog: the free running WDT resets on the third match interrupt that is
// not cleared, after about ILO period x 2 x 2^(IGNORE_BITS_ABOVE) = 2 s
#define WDT_IGNORE_BITS_ABOVE   15UL
#define WDT_LOCK_CLR0           (1UL << SRSS_WDT_CTL_WDT_LOCK_Pos)
#define WDT_LOCK_CLR1           (2UL << SRSS_WDT_CTL_WDT_LOCK_Pos)
#define WDT_LOCK_SET01          (3UL << SRSS_WDT_CTL_WDT_LOCK_Pos)

void initAfterJump(void)
{
    __enable_irq();
}

// release an MMIO peripheral from reset and enable its clock (PERI group
// slave control, Cy_SysClk_PeriGroupSlaveInit)
static void enablePeriSlave(uint32_t group, uint32_t slave)
{
    PERI->GR[group].SL_CTL2 &= ~(1UL << slave);
    PERI->GR[group].SL_CTL |= 1UL << slave;
}

// set a 16 bit divider of the TCPWM PCLK group to divide by divide_by
void setPclkDivider(uint32_t div16, uint32_t divide_by)
{
    PERI_PCLK_GR_Type* gr = &PERI_PCLK->GR[PCLK_TCPWM_GROUP];
    const uint32_t sel = (PCLK_DIV_TYPE_16 << PERI_PCLK_GR_DIV_CMD_TYPE_SEL_Pos) | div16;

    gr->DIV_CMD = PERI_PCLK_GR_DIV_CMD_DISABLE_Msk | sel;
    gr->DIV_16_CTL[div16] = ((divide_by - 1UL) << PERI_PCLK_GR_DIV_16_CTL_INT16_DIV_Pos)
        & PERI_PCLK_GR_DIV_16_CTL_INT16_DIV_Msk;
    // no phase alignment: PA_TYPE_SEL = 3, PA_DIV_SEL = 255
    gr->DIV_CMD = PERI_PCLK_GR_DIV_CMD_ENABLE_Msk | (3UL << PERI_PCLK_GR_DIV_CMD_PA_TYPE_SEL_Pos)
        | (0xFFUL << PERI_PCLK_GR_DIV_CMD_PA_DIV_SEL_Pos) | sel;
}

// connect a peripheral clock (en_clk_dst_t, group in the high byte) to a
// 16 bit divider of its PCLK group
static void connectPclk(en_clk_dst_t dst, uint32_t div16)
{
    PERI_PCLK->GR[(uint32_t)dst >> 8].CLOCK_CTL[(uint32_t)dst & 0xFFUL] =
        (PCLK_DIV_TYPE_16 << PERI_PCLK_GR_CLOCK_CTL_TYPE_SEL_Pos) | div16;
}

/*
 * Peripheral enables and clocks. The CPU clock tree itself is set up in
 * SystemInit() (system_psc3.c), CLK_HF3 feeds the TCPWM and HPPASS at
 * CPU_FREQUENCY_MHZ.
 */
void SystemClock_Config(void)
{
    enablePeriSlave(CY_MMIO_HSIOM_GROUP_NR, CY_MMIO_HSIOM_SLAVE_NR);
    enablePeriSlave(CY_MMIO_GPIO_GROUP_NR, CY_MMIO_GPIO_SLAVE_NR);
    enablePeriSlave(CY_MMIO_TCPWM0_GROUP_NR, CY_MMIO_TCPWM0_SLAVE_NR);
    enablePeriSlave(CY_MMIO_PASS_GROUP_NR, CY_MMIO_PASS_SLAVE_NR);
    enablePeriSlave(CY_MMIO_SCB0_GROUP_NR, CY_MMIO_SCB0_SLAVE_NR);

    setPclkDivider(PCLK_DIV_PWM, 1);
    setPclkDivider(PCLK_DIV_INPUT, 1);
    setPclkDivider(PCLK_DIV_2MHZ, CPU_FREQUENCY_MHZ / 2);
    setPclkDivider(PCLK_DIV_1MHZ, CPU_FREQUENCY_MHZ);

    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN260, PCLK_DIV_PWM);
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN261, PCLK_DIV_PWM);
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN262, PCLK_DIV_PWM);
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN516, PCLK_DIV_INPUT);
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN512, PCLK_DIV_2MHZ);   // INTERVAL_TIMER
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN513, PCLK_DIV_2MHZ);   // COM_TIMER
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN514, PCLK_DIV_1MHZ);   // UTILITY_TIMER
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN515, PCLK_DIV_1MHZ);   // TEN_KHZ_TIMER
}

void MX_GPIO_Init(void)
{
    // keep the IPM disabled until the gate outputs are configured
    gpioWrite(IPM_ENABLE_PORT, IPM_ENABLE_PIN, !IPM_ENABLE_ACTIVE);
    gpioSetMode(IPM_ENABLE_PORT, IPM_ENABLE_PIN, GPIO_DM_STRONG, HSIOM_SEL_GPIO);

#ifdef LED_PORT
    gpioWrite(LED_PORT, LED_PIN, 0);
    gpioSetMode(LED_PORT, LED_PIN, GPIO_DM_STRONG, HSIOM_SEL_GPIO);
#endif
}

/*
 * Priorities, 3 bits: commutation and comparator first, the DShot DMA
 * second (main.c swaps these two at high rpm with bidirectional DShot),
 * then the DShot processing and the control loop, telemetry and ADC last.
 */
static void initNvicPriorities(void)
{
    NVIC_SetPriority(COM_TIMER_IRQ, 0);
    NVIC_SetPriority(COMPARATOR_IRQ, 0);
    NVIC_SetPriority(IC_DMA_IRQ_NAME, 1);
    NVIC_SetPriority(IC_TIMER_IRQ, 1);
    NVIC_SetPriority(DSHOT_SW_IRQ, 2);
    NVIC_SetPriority(TEN_KHZ_TIMER_IRQ, 2);
    NVIC_SetPriority(TELEM_DMA_IRQ, 3);
    NVIC_SetPriority(ADC_DMA_IRQ, 3);
}

void MX_IWDG_Init(void)
{
    // the WDT runs locked from reset with all 32 bits compared (about 37 h)
    SRSS->WDT_CTL = (SRSS->WDT_CTL & ~SRSS_WDT_CTL_WDT_LOCK_Msk) | WDT_LOCK_CLR0;
    SRSS->WDT_CTL = (SRSS->WDT_CTL & ~SRSS_WDT_CTL_WDT_LOCK_Msk) | WDT_LOCK_CLR1;
    SRSS->WDT_CTL &= ~SRSS_WDT_CTL_WDT_EN_Msk;

    SRSS->CLK_ILO_CONFIG |= SRSS_CLK_ILO_CONFIG_ENABLE_Msk;
    SRSS->WDT_MATCH2 = WDT_IGNORE_BITS_ABOVE << SRSS_WDT_MATCH2_IGNORE_BITS_ABOVE_Pos;
    SRSS->WDT_MATCH = 0;
    SRSS->WDT_CNT = 0;
    SRSS->SRSS_INTR = SRSS_SRSS_INTR_WDT_MATCH_Msk;

    SRSS->WDT_CTL = SRSS_WDT_CTL_WDT_EN_Msk;     // ILO clock, enabled
    SRSS->WDT_CTL |= WDT_LOCK_SET01;
}

// feed the watchdog by clearing its match interrupt
void reloadWatchDogCounter(void)
{
    SRSS->SRSS_INTR = SRSS_SRSS_INTR_WDT_MATCH_Msk;
    (void)SRSS->SRSS_INTR;
}

void setPwmPrescaler(uint16_t presc)
{
    setPclkDivider(PCLK_DIV_PWM, (uint32_t)presc + 1);
}

void setPwmDeadTime(uint16_t deadtime)
{
    (void)deadtime;
    // DP4
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
    initNvicPriorities();
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
