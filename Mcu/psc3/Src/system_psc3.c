/*
 * system_psc3.c
 *
 * PSC3 clock setup, called from Reset_Handler before main(). Kept apart from
 * peripherals.c so the bring-up tests (Mcu/psc3/Test) can use it alone.
 *
 * Clock tree (PSOC Control C3 architecture TRM, clocking system chapter):
 *   IHO 48 MHz -> PATH1 -> DPLL0 -> 180 MHz -> CLK_HF0 (CPU, PERI, SRSS)
 *                                           -> CLK_HF1 (IOSS, GPIO)
 *                                           -> CLK_HF3 (TCPWM, HPPASS, SCB0..4)
 * All counters therefore run from CPU_FREQUENCY_MHZ, which the shared AM32
 * timing code assumes. 180 MHz needs the OD (1.2 V) core voltage.
 *
 * The register sequences follow the Infineon PDL (mtb-pdl-cat1, Apache-2.0):
 * Cy_SysPm_LdoSetVoltage, Cy_SysLib_SetWaitStates, Cy_SysClk_DpllLpConfigure
 * and Cy_SysClk_DpllLpEnable for the MXS40SSRSS v1.2 SRSS.
 */

#include "main.h"

#ifndef CPU_FREQUENCY_MHZ
#define CPU_FREQUENCY_MHZ 180
#endif

#if CPU_FREQUENCY_MHZ != 180
#error the DPLL0 settings below are for 180 MHz
#endif

#define IMO_HZ                  8000000UL

// DPLL0: 48 MHz IHO / Q 6 = 8 MHz PFD (4..8 MHz), x P 45 = 360 MHz DCO
// (160..480 MHz, DCO code multiplier above 310 MHz), / OUTPUT_DIV 2 = 180 MHz.
// The v1.2 SRSS runs the DCO at twice the output for a 50 % duty cycle.
#define DPLL0_FEEDBACK_DIV      45UL
#define DPLL0_REFERENCE_DIV     6UL
#define DPLL0_OUTPUT_DIV        2UL
#define DPLL0_DCO_CODE_MULT     1UL
#define DPLL_LOCK_TIMEOUT       100000UL    // loop count, > 10 ms at 8 MHz

// PDL defaults for the MXS40SSRSS DPLL_LP loop filter
#define DPLL_CONFIG4_VALUE  ((1UL << CLK_DPLL_LP_CONFIG4_ACC_MODE_Pos) | \
                             (1UL << CLK_DPLL_LP_CONFIG4_TDC_MODE_Pos))
#define DPLL_CONFIG5_VALUE  ((0x24UL << CLK_DPLL_LP_CONFIG5_KI_INT_Pos) |     \
                             (0x1CUL << CLK_DPLL_LP_CONFIG5_KP_INT_Pos) |     \
                             (0x23UL << CLK_DPLL_LP_CONFIG5_KI_ACC_INT_Pos) | \
                             (0x1AUL << CLK_DPLL_LP_CONFIG5_KP_ACC_INT_Pos))
#define DPLL_CONFIG6_VALUE  ((0x24UL << CLK_DPLL_LP_CONFIG6_KI_FRACT_Pos) |     \
                             (0x20UL << CLK_DPLL_LP_CONFIG6_KP_FRACT_Pos) |     \
                             (0x23UL << CLK_DPLL_LP_CONFIG6_KI_ACC_FRACT_Pos) | \
                             (0x1AUL << CLK_DPLL_LP_CONFIG6_KP_ACC_FRACT_Pos))
#define DPLL_CONFIG7_VALUE  ((0x18UL << CLK_DPLL_LP_CONFIG7_KI_SSCG_Pos) |     \
                             (0x18UL << CLK_DPLL_LP_CONFIG7_KP_SSCG_Pos) |     \
                             (0x16UL << CLK_DPLL_LP_CONFIG7_KI_ACC_SSCG_Pos) | \
                             (0x14UL << CLK_DPLL_LP_CONFIG7_KP_ACC_SSCG_Pos))

#define DPLL_BYPASS_AUTO        0UL     // output follows the input until locked
#define DPLL_BYPASS_INPUT       2UL     // output = input

#define PATH_MUX_IHO            6UL
#define CLK_PATH_DPLL0          1UL     // DPLL n sits on clock path n + 1

// core voltage, see Cy_SysPm_LdoSetVoltage. PWR_TRIM_WAKE_CTL has no field
// in the SRSS register structure
#define SRSS_PWR_TRIM_WAKE_CTL  (*(volatile uint32_t*)(SRSS_BASE + 0x20E8UL))
#define TRIM_RAM_CTL_RA_MASK    (0x3UL << 8)
#define TRIM_RAM_CTL_WC_MASK    (0x3UL << 10)
#define TRIM_RAM_CTL_OD         0x00004013UL    // CY_SRSS_TRIM_RAM_CTL_OD, same for HALF_OD
#define TRIM_ROM_CTL_OD         0x00000013UL

// flash wait states, Cy_SysLib_SetWaitStates: 60 * f[MHz] / 1000 + 1
#define FLASH_WAIT_STATES       ((60UL * CPU_FREQUENCY_MHZ) / 1000UL + 1UL)

uint32_t SystemCoreClock = IMO_HZ;

// bring-up diagnostics, readable with the debugger
volatile uint8_t psc3_core_voltage_od;
volatile uint8_t psc3_dpll_locked;

static void delayLoop(uint32_t count)
{
    while (count--) {
        __NOP();
    }
}

// the trims can only be changed on silicon that allows it, see
// IsVoltageChangePossible in the PDL
static uint8_t voltageChangePossible(void)
{
    volatile uint32_t* ram = &SRSS->RAM_TRIM_STRUCT.TRIM_RAM_CTL;
    const uint32_t check = *ram & TRIM_RAM_CTL_WC_MASK;
    *ram = (*ram & ~TRIM_RAM_CTL_WC_MASK) | (~check & TRIM_RAM_CTL_WC_MASK);
    return check != (*ram & TRIM_RAM_CTL_WC_MASK);
}

static void setMemoryTrimsOd(void)
{
    volatile uint32_t* ram = &SRSS->RAM_TRIM_STRUCT.TRIM_RAM_CTL;
    *ram = (TRIM_RAM_CTL_OD & ~TRIM_RAM_CTL_RA_MASK) | (*ram & TRIM_RAM_CTL_RA_MASK);
    SRSS->RAM_TRIM_STRUCT.TRIM_ROM_CTL = TRIM_ROM_CTL_OD;
}

// raise VCCD to 1.2 V (OD mode), runs from the 8 MHz IMO
static void setCoreVoltageOd(void)
{
    const uint32_t trim = PSC3_SFLASH->LDO_1P2V_TRIM & SRSS_PWR_TRIM_PWRSYS_CTL_ACT_REG_TRIM_Msk;

    if ((SRSS->PWR_TRIM_PWRSYS_CTL & SRSS_PWR_TRIM_PWRSYS_CTL_ACT_REG_TRIM_Msk) == trim) {
        psc3_core_voltage_od = 1;
        return;
    }
    if (!voltageChangePossible()) {
        return;
    }
    SRSS_PWR_TRIM_WAKE_CTL = (PSC3_SFLASH->PWR_TRIM_WAKE_CTL & SFLASH_PWR_TRIM_WAKE_CTL_WAKE_DELAY_1P2V_Msk)
        >> SFLASH_PWR_TRIM_WAKE_CTL_WAKE_DELAY_1P2V_Pos;
    delayLoop(100);         // >= 3 us
    setMemoryTrimsOd();     // write assist
    SRSS->PWR_TRIM_PWRSYS_CTL = (SRSS->PWR_TRIM_PWRSYS_CTL & ~SRSS_PWR_TRIM_PWRSYS_CTL_ACT_REG_TRIM_Msk) | trim;
    delayLoop(100);         // let VCCD settle at 1.2 V
    setMemoryTrimsOd();     // read margin
    psc3_core_voltage_od = 1;
}

static void setFlashWaitStates(uint32_t ws)
{
    FLASHC->FLASH_CTL = (FLASHC->FLASH_CTL & ~FLASHC_FLASH_CTL_RBUS_WS_Msk)
        | ((ws << FLASHC_FLASH_CTL_RBUS_WS_Pos) & FLASHC_FLASH_CTL_RBUS_WS_Msk);
}

static uint8_t startDpll0(void)
{
    CLK_DPLL_LP_Type* dpll = &SRSS->CLK_DPLL_LP[0];

    // the boot ROM may have left the DPLL running, it must be off to be configured
    if (dpll->CONFIG & CLK_DPLL_LP_CONFIG_ENABLE_Msk) {
        dpll->CONFIG = (dpll->CONFIG & ~CLK_DPLL_LP_CONFIG_BYPASS_SEL_Msk)
            | (DPLL_BYPASS_INPUT << CLK_DPLL_LP_CONFIG_BYPASS_SEL_Pos);
        delayLoop(10);
        dpll->CONFIG &= ~CLK_DPLL_LP_CONFIG_ENABLE_Msk;
    }

    SRSS->CLK_PATH_SELECT[CLK_PATH_DPLL0] = PATH_MUX_IHO;

    dpll->CONFIG = (DPLL0_FEEDBACK_DIV << CLK_DPLL_LP_CONFIG_FEEDBACK_DIV_Pos)
        | (DPLL0_REFERENCE_DIV << CLK_DPLL_LP_CONFIG_REFERENCE_DIV_Pos)
        | (DPLL0_OUTPUT_DIV << CLK_DPLL_LP_CONFIG_OUTPUT_DIV_Pos)
        | (DPLL0_DCO_CODE_MULT << CLK_DPLL_LP_CONFIG_PLL_DCO_CODE_MULT_Pos)
        | (DPLL_BYPASS_AUTO << CLK_DPLL_LP_CONFIG_BYPASS_SEL_Pos);
    dpll->CONFIG2 = CLK_DPLL_LP_CONFIG2_FRAC_EN_Msk;    // fraction 0
    dpll->CONFIG3 = 0;                                  // no spread spectrum
    dpll->CONFIG4 = DPLL_CONFIG4_VALUE;
    dpll->CONFIG5 = DPLL_CONFIG5_VALUE;
    dpll->CONFIG6 = DPLL_CONFIG6_VALUE;
    dpll->CONFIG7 = DPLL_CONFIG7_VALUE;

    dpll->CONFIG |= CLK_DPLL_LP_CONFIG_ENABLE_Msk;
    for (uint32_t i = 0; i < DPLL_LOCK_TIMEOUT; i++) {
        if (dpll->STATUS & CLK_DPLL_LP_STATUS_LOCKED_Msk) {
            return 1;
        }
    }
    // no lock: bypass and switch the DPLL off again
    dpll->CONFIG = (dpll->CONFIG & ~CLK_DPLL_LP_CONFIG_BYPASS_SEL_Msk)
        | (DPLL_BYPASS_INPUT << CLK_DPLL_LP_CONFIG_BYPASS_SEL_Pos);
    delayLoop(10);
    dpll->CONFIG &= ~CLK_DPLL_LP_CONFIG_ENABLE_Msk;
    return 0;
}

// feed CLK_HF<root> from a clock path, undivided
static void setClockRoot(uint32_t root, uint32_t path)
{
    SRSS->CLK_ROOT_SELECT[root] = (path << SRSS_CLK_ROOT_SELECT_ROOT_MUX_Pos) | SRSS_CLK_ROOT_SELECT_ENABLE_Msk;
    SRSS->CLK_DIRECT_SELECT[root] = SRSS_CLK_DIRECT_SELECT_DIRECT_MUX_Msk;  // ROOT_MUX, not IMO
}

void SystemInit(void)
{
    // run from the IMO while the voltage, flash and DPLL are changed. The
    // direct mux is glitch free
    SRSS->CLK_DIRECT_SELECT[0] = 0;
    SystemCoreClock = IMO_HZ;

    setCoreVoltageOd();
    setFlashWaitStates(FLASH_WAIT_STATES);
    ICACHE0->CTL |= ICACHE_CTL_CA_EN_Msk;

    psc3_dpll_locked = startDpll0();
    if (!psc3_dpll_locked) {
        // stay on the IMO, the firmware timing will be wrong but the
        // debugger can still read psc3_dpll_locked
        return;
    }

    setClockRoot(1, CLK_PATH_DPLL0);
    setClockRoot(3, CLK_PATH_DPLL0);
    setClockRoot(0, CLK_PATH_DPLL0);
    SystemCoreClock = CPU_FREQUENCY_MHZ * 1000000UL;
}

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = psc3_dpll_locked ? CPU_FREQUENCY_MHZ * 1000000UL : IMO_HZ;
}
