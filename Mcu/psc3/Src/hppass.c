/*
 * hppass.c
 *
 * HPPASS start-up following the TRM (HPPASS chapter, Startup and Autonomous
 * controller initialization) and the order of Cy_HPPASS_Init in the PDL:
 *   1. factory trims from SFLASH (address / value table) and the SAR
 *      linearity calibration
 *   2. analog reference (AREF) enable
 *   3. start-up timer: AREF -> SAR / CSG channel enable 40 us, -> CSG slice
 *      power 10 us, -> CSG ready 400 ns, with a 200 ns timer resolution
 *   4. autonomous controller (AC) program: unlock and enable the blocks,
 *      wait for BLOCK_READY, stop
 * The CSG slices and the SAR must be configured before hppassStart().
 */

#include "hppass.h"

#include "psc3_timing.h"

// clk_infra is CLK_HF3 / 2
#define HPPASS_INFRA_HZ         (PSC3_COUNTER_CLOCK_HZ / 2UL)
#define STARTUP_TICK_NS         200UL
#define STARTUP_DIV_VAL         ((HPPASS_INFRA_HZ / 1000000UL) * STARTUP_TICK_NS / 1000UL - 1UL)
#define STARTUP_COUNT(us)       ((us) * 1000UL / STARTUP_TICK_NS - 1UL)

#define AC_ACTION_STOP          0UL
#define AC_ACTION_WAIT_FOR      2UL
#define AC_COND_BLOCK_READY     2UL

#define HPPASS_TRIM_ENTRIES     7UL
#define HPPASS_READY_TIMEOUT    2000000UL   // loop count, about 50 ms at 180 MHz

// SFLASH SAR_INFRA_TRIM_TABLE: number of entries, then address / value pairs
typedef struct {
    uint32_t num_entries;
    struct {
        uint32_t address;
        uint32_t value;
    } entry[HPPASS_TRIM_ENTRIES];
} hppass_trims_t;

static uint8_t applyTrims(void)
{
    const hppass_trims_t* trims = (const hppass_trims_t*)SFLASH->SAR_INFRA_TRIM_TABLE;

    if (trims->num_entries != HPPASS_TRIM_ENTRIES) {
        return 0;
    }
    for (uint32_t i = 0; i < HPPASS_TRIM_ENTRIES; i++) {
        // the table holds non-secure addresses, use the secure alias of
        // the same HPPASS register (bit 28), and only HPPASS registers
        const uint32_t addr = trims->entry[i].address | 0x10000000UL;
        if ((addr & 0xFFF00000UL) != HPPASS_BASE) {
            return 0;
        }
        *(volatile uint32_t*)addr = trims->entry[i].value;
    }
    for (uint32_t i = 0; i < 16U; i++) {
        HPPASS->SARADC.CALLIN[i] = SFLASH->SAR_CAL_LIN_TABLE[i];
    }
    return 1;
}

uint8_t hppassReady(void)
{
    return (HPPASS->ACTRLR.BLOCK_STATUS & HPPASS_ACTRLR_BLOCK_STATUS_READY_Msk) != 0;
}

uint8_t hppassStart(uint32_t csg_mask, uint8_t sar_enable)
{
    HPPASS->ACTRLR.CTRL = HPPASS_ACTRLR_CTRL_ENABLED_Msk;

    if (!applyTrims()) {
        return 0;
    }

    HPPASS->INFRA.AREFV2.AREF_CTRL = (1UL << HPPASS_INFRA_AREFV2_AREF_CTRL_IZTAT_SEL_Pos)
        | (1UL << HPPASS_INFRA_AREFV2_AREF_CTRL_VREF_SEL_Pos)
        | (1UL << HPPASS_INFRA_AREFV2_AREF_CTRL_AREF_BIAS_SCALE_Pos)
        | HPPASS_INFRA_AREFV2_AREF_CTRL_ENABLED_Msk;

    HPPASS->INFRA.CLOCK_STARTUP_DIV = STARTUP_DIV_VAL;
    HPPASS->INFRA.STARTUP_CFG[0] = STARTUP_COUNT(40UL)
        | HPPASS_INFRA_STARTUP_CFG_SAR_EN_Msk | HPPASS_INFRA_STARTUP_CFG_CSG_CH_EN_Msk;
    HPPASS->INFRA.STARTUP_CFG[1] = STARTUP_COUNT(10UL) | HPPASS_INFRA_STARTUP_CFG_CSG_PWR_EN_SLICE_Msk;
    HPPASS->INFRA.STARTUP_CFG[2] = 1UL | HPPASS_INFRA_STARTUP_CFG_CSG_READY_Msk;  // 400 ns > 32 CSG clocks
    HPPASS->INFRA.STARTUP_CFG[3] = 0;

    // AC state 0: enable the blocks and wait until they are ready
    HPPASS_ACTRLR_TTCFG_Type* tt = &HPPASS->ACTRLR.TTCFG[0];
    tt->TT_CFG0 = (sar_enable ? HPPASS_ACTRLR_TTCFG_TT_CFG0_SAR_UNLOCK_Msk : 0UL)
        | (csg_mask << HPPASS_ACTRLR_TTCFG_TT_CFG0_CSG_UNLOCK_Pos);
    tt->TT_CFG1 = (AC_COND_BLOCK_READY << HPPASS_ACTRLR_TTCFG_TT_CFG1_COND_Pos)
        | (AC_ACTION_WAIT_FOR << HPPASS_ACTRLR_TTCFG_TT_CFG1_ACTION_Pos);
    tt->TT_CFG2 = csg_mask << HPPASS_ACTRLR_TTCFG_TT_CFG2_CSG_EN_Pos;
    tt->TT_CFG3 = sar_enable ? HPPASS_ACTRLR_TTCFG_TT_CFG3_SAR_EN_Msk : 0UL;
    // AC state 1: stop, keeping the enables (all four words must be written)
    tt = &HPPASS->ACTRLR.TTCFG[1];
    tt->TT_CFG0 = 0;
    tt->TT_CFG1 = AC_ACTION_STOP << HPPASS_ACTRLR_TTCFG_TT_CFG1_ACTION_Pos;
    tt->TT_CFG2 = csg_mask << HPPASS_ACTRLR_TTCFG_TT_CFG2_CSG_EN_Pos;
    tt->TT_CFG3 = sar_enable ? HPPASS_ACTRLR_TTCFG_TT_CFG3_SAR_EN_Msk : 0UL;

    HPPASS->ACTRLR.CMD_STATE = 0;
    HPPASS->ACTRLR.CMD_RUN = HPPASS_ACTRLR_CMD_RUN_RUN_Msk;

    for (uint32_t i = 0; i < HPPASS_READY_TIMEOUT; i++) {
        if (hppassReady()) {
            return 1;
        }
    }
    return 0;
}
