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
 * stores FAULT_ST and clears the non latched faults. When the start failed
 * it retries every EDL_RETRY_MS while the motor is stopped: on a modular
 * board (KIT_PSC3M5_EVK on USB) the power stage, and so the 6EDL7141, often
 * gets its supply after the MCU has started.
 *
 * The same board can lose the battery while the MCU keeps running. nFAULT
 * then reads low (its pull-up goes to DVDD) and DEVICE_ID no longer answers:
 * the driver is marked EDL_ERR_LOST, EN_DRV and nBRAKE go low and the retry
 * starts it again. nBRAKE must be low before the supply returns: the same
 * pin is VSENSE, read once at power-up, and a high level there selects
 * DVDD 5 V instead of 3.3 V (datasheet Rev 1.20, section 3.9.2). A supply
 * dip too short to see on nFAULT resets the registers; the check every
 * EDL_CHECK_MS finds that and restarts the driver as well.
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
volatile uint16_t edl7141_retries;

extern uint8_t running;
static uint16_t retry_ms;
static uint16_t check_ms;
static uint8_t check_index;

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

// a second read before giving up: one disturbed frame must not switch the
// bridge off while the motor runs
static uint8_t readsAs(uint8_t reg, uint16_t mask, uint16_t value)
{
    return (edl7141Read(reg) & mask) == value || (edl7141Read(reg) & mask) == value;
}

void edl7141Service(void)
{
    if (edl7141_status != EDL_OK) {
        // the bridge stays off until a start succeeds; never while running
        if (!running && ++retry_ms >= EDL_RETRY_MS) {
            retry_ms = 0;
            edl7141_retries++;
            (void)edl7141Start();
        }
        return;
    }
    retry_ms = 0;
    const uint8_t fault = edl7141FaultActive();
    if (fault || ++check_ms >= EDL_CHECK_MS) {
        // still there, and with our configuration (one register per check)?
        check_ms = 0;
        if (!readsAs(EDL_DEVICE_ID, EDL_DEV_ID_Msk, EDL_DEV_ID_VALUE)) {
            (void)fail(EDL_ERR_LOST);   // supply gone: nBRAKE low before it returns
            return;
        }
        const edl_cfg_t* cfg = &edl_config[check_index];
        check_index = (uint8_t)((check_index + 1U) % (sizeof(edl_config) / sizeof(edl_config[0])));
        if (!readsAs(cfg->reg, 0xFFFFU, cfg->value)) {
            edl7141_bad_reg = cfg->reg;
            (void)fail(EDL_ERR_LOST);   // reset by a supply dip: configure it again
            return;
        }
    }
    if (!fault) {
        return;
    }
    edl7141_fault_st = edl7141Read(EDL_FAULT_ST);
    edl7141_fault_count++;
    edl7141Write(EDL_FAULTS_CLR, EDL_CLR_FLTS);
}

#endif /* USE_6EDL7141 */
