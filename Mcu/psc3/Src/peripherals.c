/*
 * peripherals.c
 *
 * PSC3 clocks, GPIO, watchdog and the PWM / timer TCPWM counters.
 */

#include "peripherals.h"

#include "ADC.h"
#include "IO.h"
#include "comparator.h"
#include "hppass.h"
#include "serial_telemetry.h"
#include "targets.h"

// PCLK divider type for CLOCK_CTL / DIV_CMD
#define PCLK_DIV_TYPE_8     0UL
#define PCLK_DIV_TYPE_16    1UL

// watchdog: the free running WDT resets on the third match interrupt that is
// not cleared, after about ILO period x 2 x 2^(IGNORE_BITS_ABOVE) = 2 s
#define WDT_IGNORE_BITS_ABOVE   15UL
#define WDT_LOCK_CLR0           (1UL << SRSS_WDT_CTL_WDT_LOCK_Pos)
#define WDT_LOCK_CLR1           (2UL << SRSS_WDT_CTL_WDT_LOCK_Pos)
#define WDT_LOCK_SET01          (3UL << SRSS_WDT_CTL_WDT_LOCK_Pos)

// bring-up diagnostics, readable with the debugger
volatile uint8_t hppass_ready;

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

// set an 8 bit divider of the TCPWM PCLK group (the CSG clock)
static void setPclkDivider8(uint32_t div8, uint32_t divide_by)
{
    PERI_PCLK_GR_Type* gr = &PERI_PCLK->GR[PCLK_TCPWM_GROUP];
    const uint32_t sel = (PCLK_DIV_TYPE_8 << PERI_PCLK_GR_DIV_CMD_TYPE_SEL_Pos) | div8;

    gr->DIV_CMD = PERI_PCLK_GR_DIV_CMD_DISABLE_Msk | sel;
    gr->DIV_8_CTL[div8] = ((divide_by - 1UL) << PERI_PCLK_GR_DIV_8_CTL_INT8_DIV_Pos)
        & PERI_PCLK_GR_DIV_8_CTL_INT8_DIV_Msk;
    gr->DIV_CMD = PERI_PCLK_GR_DIV_CMD_ENABLE_Msk | (3UL << PERI_PCLK_GR_DIV_CMD_PA_TYPE_SEL_Pos)
        | (0xFFUL << PERI_PCLK_GR_DIV_CMD_PA_DIV_SEL_Pos) | sel;
}

// connect a peripheral clock (en_clk_dst_t, group in the high byte) to a
// divider of its PCLK group
static void connectPclkType(en_clk_dst_t dst, uint32_t type, uint32_t div)
{
    PERI_PCLK->GR[(uint32_t)dst >> 8].CLOCK_CTL[(uint32_t)dst & 0xFFUL] =
        (type << PERI_PCLK_GR_CLOCK_CTL_TYPE_SEL_Pos) | div;
}

static void connectPclk(en_clk_dst_t dst, uint32_t div16)
{
    connectPclkType(dst, PCLK_DIV_TYPE_16, div16);
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
    setPclkDivider(PCLK_DIV_2MHZ, PCLK_DIVIDE_2MHZ);
    setPclkDivider(PCLK_DIV_1MHZ, PCLK_DIVIDE_1MHZ);

    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN260, PCLK_DIV_PWM);
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN261, PCLK_DIV_PWM);
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN262, PCLK_DIV_PWM);
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN516, PCLK_DIV_INPUT);
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN512, PCLK_DIV_2MHZ);   // INTERVAL_TIMER
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN513, PCLK_DIV_2MHZ);   // COM_TIMER
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN514, PCLK_DIV_1MHZ);   // UTILITY_TIMER
    connectPclk(PCLK_TCPWM0_CLOCK_COUNTER_EN515, PCLK_DIV_1MHZ);   // TEN_KHZ_TIMER

    // HPPASS CSG clock, 10..120 MHz
    setPclkDivider8(PCLK_DIV8_CSG, PCLK_DIVIDE_CSG);
    connectPclkType(PCLK_PASS_CLOCK_CSG, PCLK_DIV_TYPE_8, PCLK_DIV8_CSG);
}

void MX_GPIO_Init(void)
{
    // keep the IPM disabled until the gate outputs are configured
    gpioWrite(IPM_ENABLE_PORT, IPM_ENABLE_PIN, !IPM_ENABLE_ACTIVE);
    gpioSetMode(IPM_ENABLE_PORT, IPM_ENABLE_PIN, GPIO_DM_STRONG, HSIOM_SEL_GPIO);

#ifdef LED_PORT
#ifdef LED_ACTIVE_LOW
    gpioWrite(LED_PORT, LED_PIN, 1);    // off
#else
    gpioWrite(LED_PORT, LED_PIN, 0);
#endif
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

// TCPWM counter settings for the PWM counters
#define TCPWM_MODE_TIMER        0UL
#define TCPWM_MODE_PWM_DT       5UL
#define TCPWM_DISABLE_RETAIN    1UL     // outputs follow LINE_SEL while disabled / stopped
#define TCPWM_EDGE_RISING       0UL
#define TCPWM_EDGE_NONE         3UL     // level, no edge detection
#define TCPWM_TRIG_ZERO         0UL     // input trigger 0 is constant '0'
#define TCPWM_TRIG_ONE          1UL     // input trigger 1 is constant '1'
#define TCPWM_PWM_SET           0UL
#define TCPWM_PWM_CLEAR         1UL
#define TCPWM_PWM_NO_CHANGE     3UL

// dead time before activating line_out and line_compl_out, in counter cycles
static void writeDeadTime(TCPWM_GRP_CNT_Type* cnt, uint32_t deadtime)
{
    const uint32_t dt = (deadtime << TCPWM_GRP_CNT_DT_DT_LINE_OUT_L_Pos)
        | (deadtime << TCPWM_GRP_CNT_DT_DT_LINE_COMPL_OUT_Pos);
    cnt->DT = dt;
    cnt->DT_BUFF = dt;
}

void setPwmDeadTime(uint16_t deadtime)
{
    for (uint32_t n = PWM_CNT_FIRST; n <= PWM_CNT_LAST; n++) {
        writeDeadTime(PWM_TIMER(n), deadtime);
    }
}

/*
 * PWM_DT mode, counting up, line '1' from the terminal count (overflow) to the
 * CC0 match like the STM32 PWM mode 1. Line selection comes before the dead
 * time insertion (TRM, TCPWM output path), so every gate pin gets the dead
 * time on its rising edge whatever signal LINE_SEL picks for it.
 */
void initPwm(void)
{
    for (uint32_t n = PWM_CNT_FIRST; n <= PWM_CNT_LAST; n++) {
        TCPWM_GRP_CNT_Type* cnt = PWM_TIMER(n);

        cnt->CTRL = 0;
        cnt->CTRL = (TCPWM_MODE_PWM_DT << TCPWM_GRP_CNT_CTRL_MODE_Pos)
            | (TCPWM_DISABLE_RETAIN << TCPWM_GRP_CNT_CTRL_PWM_DISABLE_MODE_Pos)
            | TCPWM_GRP_CNT_CTRL_AUTO_RELOAD_CC0_Msk | TCPWM_GRP_CNT_CTRL_AUTO_RELOAD_PERIOD_Msk
            | TCPWM_GRP_CNT_CTRL_AUTO_RELOAD_CC1_Msk
            | TCPWM_GRP_CNT_CTRL_CC0_MATCH_UP_EN_Msk | TCPWM_GRP_CNT_CTRL_CC1_MATCH_UP_EN_Msk;
        cnt->LINE_SEL = (TCPWM_LINE_L << TCPWM_GRP_CNT_LINE_SEL_OUT_SEL_Pos)
            | (TCPWM_LINE_L << TCPWM_GRP_CNT_LINE_SEL_COMPL_OUT_SEL_Pos);
        cnt->LINE_SEL_BUFF = cnt->LINE_SEL;
        cnt->COUNTER = 0;
        cnt->PERIOD = TIM1_AUTORELOAD;
        cnt->PERIOD_BUFF = TIM1_AUTORELOAD;
        cnt->CC0 = 0;
        cnt->CC0_BUFF = 0;
        cnt->CC1 = 0;
        cnt->CC1_BUFF = 0;
        writeDeadTime(cnt, DEAD_TIME);
        cnt->TR_PWM_CTRL = (TCPWM_PWM_CLEAR << TCPWM_GRP_CNT_TR_PWM_CTRL_CC0_MATCH_MODE_Pos)
            | (TCPWM_PWM_SET << TCPWM_GRP_CNT_TR_PWM_CTRL_OVERFLOW_MODE_Pos)
            | (TCPWM_PWM_NO_CHANGE << TCPWM_GRP_CNT_TR_PWM_CTRL_UNDERFLOW_MODE_Pos)
            | (TCPWM_PWM_NO_CHANGE << TCPWM_GRP_CNT_TR_PWM_CTRL_CC1_MATCH_MODE_Pos);
        cnt->TR_IN_SEL0 = (PWM_TRIG_INPUT_SWITCH << TCPWM_GRP_CNT_TR_IN_SEL0_CAPTURE0_SEL_Pos)
            | (TCPWM_TRIG_ONE << TCPWM_GRP_CNT_TR_IN_SEL0_COUNT_SEL_Pos);
        cnt->TR_IN_SEL1 = PWM_TRIG_INPUT_START << TCPWM_GRP_CNT_TR_IN_SEL1_START_SEL_Pos;
        cnt->TR_IN_EDGE_SEL = (TCPWM_EDGE_RISING << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_CAPTURE0_EDGE_Pos)
            | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_COUNT_EDGE_Pos)
            | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_RELOAD_EDGE_Pos)
            | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_STOP_EDGE_Pos)
            | (TCPWM_EDGE_RISING << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_START_EDGE_Pos)
            | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_CAPTURE1_EDGE_Pos);
        cnt->INTR_MASK = 0;
        cnt->CTRL |= TCPWM_GRP_CNT_CTRL_ENABLED_Msk;   // enabled, waits for the start trigger
    }

    // gate pins: drive low, then hand them to the TCPWM (LINE_SEL is all 'L')
    static const uint8_t gate_pins[6] = { PHASE_A_HIGH_PIN, PHASE_A_LOW_PIN, PHASE_B_HIGH_PIN,
        PHASE_B_LOW_PIN, PHASE_C_HIGH_PIN, PHASE_C_LOW_PIN };
    for (uint32_t i = 0; i < 6U; i++) {
        gpioWrite(PWM_GPIO_PORT, gate_pins[i], 0);
        gpioSetMode(PWM_GPIO_PORT, gate_pins[i], GPIO_DM_STRONG, PWM_HSIOM_SEL);
    }
}

// start the three PWM counters at the same clock
void startPwm(void)
{
    pwmTrigger(PWM_TRIG_START);
}

/*
 * Per phase duty cycles (sine startup) are not possible on the 2GO kit: every
 * PWM counter drives gate pins of two phases (see targets.h, PSC3_2GO), so
 * the target sets NO_SINE_START and these are never called with different
 * values. A board with the high and low gate of each phase on the line /
 * line_compl pair of one counter can implement them.
 */
void setPWMCompare1(uint16_t compareone)
{
    (void)compareone;
}

void setPWMCompare2(uint16_t comparetwo)
{
    (void)comparetwo;
}

void setPWMCompare3(uint16_t comparethree)
{
    (void)comparethree;
}

// the STM32 update event loads the preload registers, here the buffered
// values are copied at the next terminal count
void generatePwmTimerEvent(void)
{
    pwmTrigger(PWM_TRIG_SWITCH);
}

// like the STM32 PSC = 0, CNT = 0 after a signal timeout
void resetInputCaptureTimer(void)
{
    setPclkDivider(PCLK_DIV_INPUT, 1);
    IC_TIMER_REGISTER->COUNTER = 0;
}

// free running up counter in timer mode, period + 1 clocks per terminal count
// like the STM32 ARR + 1
static void initTimerCounter(TCPWM_GRP_CNT_Type* cnt, uint32_t period)
{
    cnt->CTRL = 0;
    cnt->CTRL = TCPWM_MODE_TIMER << TCPWM_GRP_CNT_CTRL_MODE_Pos;
    cnt->COUNTER = 0;
    cnt->PERIOD = period;
    cnt->TR_IN_SEL0 = TCPWM_TRIG_ONE << TCPWM_GRP_CNT_TR_IN_SEL0_COUNT_SEL_Pos;
    cnt->TR_IN_SEL1 = 0;
    cnt->TR_IN_EDGE_SEL = (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_CAPTURE0_EDGE_Pos)
        | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_COUNT_EDGE_Pos)
        | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_RELOAD_EDGE_Pos)
        | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_STOP_EDGE_Pos)
        | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_START_EDGE_Pos)
        | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_CAPTURE1_EDGE_Pos);
    cnt->INTR_MASK = 0;
    cnt->INTR = TCPWM_INTR_TC | TCPWM_INTR_CC0;
    cnt->CTRL |= TCPWM_GRP_CNT_CTRL_ENABLED_Msk;
}

/*
 * INTERVAL_TIMER 2 MHz and UTILITY_TIMER 1 MHz free running over 16 bits,
 * COM_TIMER 2 MHz with the period set by SET_AND_ENABLE_COM_INT, TEN_KHZ_TIMER
 * 1 MHz with a terminal count at LOOP_FREQUENCY_HZ. The clocks come from the
 * PCLK dividers set in SystemClock_Config.
 */
void initTimers(void)
{
    initTimerCounter(INTERVAL_TIMER, 0xFFFF);
    initTimerCounter(COM_TIMER, 0xFFFF);
    initTimerCounter(UTILITY_TIMER, 0xFFFF);
    initTimerCounter(TEN_KHZ_TIMER, TEN_KHZ_TIMER_PERIOD);
}

void startTimers(void)
{
    INTERVAL_TIMER->TR_CMD = TCPWM_CMD_START;
    COM_TIMER->TR_CMD = TCPWM_CMD_START;
    UTILITY_TIMER->TR_CMD = TCPWM_CMD_START;
    TEN_KHZ_TIMER->TR_CMD = TCPWM_CMD_START;

    NVIC_ClearPendingIRQ(COM_TIMER_IRQ);
    NVIC_EnableIRQ(COM_TIMER_IRQ);

    TEN_KHZ_TIMER->INTR = TCPWM_INTR_TC;
    TEN_KHZ_TIMER->INTR_MASK = TCPWM_INTR_TC;
    NVIC_ClearPendingIRQ(TEN_KHZ_TIMER_IRQ);
    NVIC_EnableIRQ(TEN_KHZ_TIMER_IRQ);
}

void initCorePeripherals(void)
{
    SystemClock_Config();
    MX_GPIO_Init();
    initNvicPriorities();
    initPwm();
    initTimers();
    initComparators();
#ifdef USE_ADC
    ADC_Init();
#endif
    hppass_ready = hppassStart(comparatorSliceMask(), HPPASS_SAR_USED);
    initInput();
    // DP10 telemetry
#ifdef USE_SERIAL_TELEMETRY
    telem_UART_Init();
#endif
}

void enableCorePeripherals(void)
{
    startPwm();
    // the gate outputs are all off now, the IPM can be enabled
    gpioWrite(IPM_ENABLE_PORT, IPM_ENABLE_PIN, IPM_ENABLE_ACTIVE);
    startTimers();
    // DP7: start the ADC
#ifdef USE_ADC
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
