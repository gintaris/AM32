/*
 * edl7141.c
 *
 * Infineon 6EDL7141 gate driver: start-up configuration over SPI and fault
 * handling (targets with USE_6EDL7141).
 *
 * The 6EDL7141 powers the MCU itself (DVDD), so it is already in STANDBY
 * (CE high, EN_DRV low) when the firmware starts. edl7141Start():
 *  1. checks DEVICE_ID, so a broken SPI link does not go unnoticed
 *  2. writes the configuration (edl7141.h) while EN_DRV is low, as most
 *     fields only take effect in STANDBY, and reads every register back as
 *     the datasheet recommends (section 7)
 *  3. clears the faults raised during the power-up
 *  4. sets EN_DRV and waits for both charge pumps (SUPPLY_ST)
 * On any failure EN_DRV stays low and edl7141_status tells why. The reset
 * values would also drive the bridge (6PWM, 12 V), but with 4 us dead time
 * and 200 mA gate current, so the firmware does not run the motor on them.
 *
 * edl7141Service() runs in the 1 kHz part of the main loop: on nFAULT it
 * stores FAULT_ST and clears the non latched faults.
 *
 * This file has no register access, the hooks are in edl7141_hal.c, so the
 * host unit tests can run it.
 */

#include "edl7141.h"

#include "targets.h"

#ifdef USE_6EDL7141

volatile uint8_t edl7141_status = EDL_NOT_STARTED;
volatile uint16_t edl7141_fault_st;
volatile uint16_t edl7141_fault_count;
volatile uint16_t edl7141_bad_reg;

typedef struct {
    uint8_t reg;
    uint16_t value;
} edl_cfg_t;

static const edl_cfg_t edl_config[] = {
    { EDL_SUPPLY_CFG, EDL_CFG_SUPPLY },
    { EDL_PWM_CFG, EDL_CFG_PWM },
    { EDL_IDRIVE_CFG, EDL_CFG_IDRIVE },
    { EDL_DT_CFG, EDL_CFG_DT },
    { EDL_CSAMP_CFG, EDL_CFG_CSAMP },
};

uint16_t edl7141Read(uint8_t reg)
{
    return (uint16_t)(edl7141SpiFrame(EDL_FRAME(0, reg, 0)) & EDL_FRAME_DATA_Msk);
}

void edl7141Write(uint8_t reg, uint16_t value)
{
    (void)edl7141SpiFrame(EDL_FRAME(1, reg, value));
}

static uint8_t fail(uint8_t status)
{
    edl7141SetEnable(0);
    edl7141SetBrake(1);
    edl7141_status = status;
    return status;
}

uint8_t edl7141Start(void)
{
    edl7141HwInit();
    edl7141SetEnable(0);
    edl7141SetBrake(1);

    if ((edl7141Read(EDL_DEVICE_ID) & EDL_DEV_ID_Msk) != EDL_DEV_ID_VALUE) {
        return fail(EDL_ERR_ID);
    }

    for (uint32_t i = 0; i < sizeof(edl_config) / sizeof(edl_config[0]); i++) {
        edl7141Write(edl_config[i].reg, edl_config[i].value);
        if (edl7141Read(edl_config[i].reg) != edl_config[i].value) {
            edl7141_bad_reg = edl_config[i].reg;
            return fail(EDL_ERR_VERIFY);
        }
    }

    edl7141Write(EDL_FAULTS_CLR, EDL_CLR_FLTS | EDL_CLR_LATCH);

    // after the start-up the VSENSE pin is nBRAKE, held low by R2 until the
    // MCU drives it high through D5
    edl7141SetBrake(0);
    edl7141SetEnable(1);
    for (uint32_t i = 0; i < EDL_GATE_SUPPLY_POLLS; i++) {
        edl7141DelayUs(100);
        if ((edl7141Read(EDL_SUPPLY_ST) & EDL_SUPPLY_GATES_OK) == EDL_SUPPLY_GATES_OK) {
            edl7141_status = EDL_OK;
            return EDL_OK;
        }
    }
    return fail(EDL_ERR_GATE_SUPPLY);
}

void edl7141Service(void)
{
    if (edl7141_status != EDL_OK || !edl7141FaultActive()) {
        return;
    }
    edl7141_fault_st = edl7141Read(EDL_FAULT_ST);
    edl7141_fault_count++;
    edl7141Write(EDL_FAULTS_CLR, EDL_CLR_FLTS);
}

#endif /* USE_6EDL7141 */
