/*
 * Host test for Mcu/psc3/Src/edl7141.c: the 6EDL7141 start-up and fault
 * handling against a simulated device behind the SPI hooks.
 */

#include <string.h>

#include "edl7141.h"
#include "unit.h"

// ---------------------------------------------------------------- simulated 6EDL7141
static uint16_t regs[0x20];
static int enabled;             // EN_DRV pin
static int enabled_at_write;    // EN_DRV level of the last configuration write
static int fault_pin;           // nFAULT active
static int frames;
static int stuck_reg = -1;      // register that ignores writes
static int no_answer;           // SDO stays low (SPI not connected)
static int pumps_after_polls;   // SUPPLY_ST reads until the charge pumps are up
static int supply_reads;
static uint32_t last_frames[64];

static void deviceReset(void)
{
    memset(regs, 0, sizeof(regs));
    regs[EDL_DEVICE_ID] = 0x0006;   // reset values, datasheet section 8.2
    regs[EDL_SUPPLY_CFG] = 0x6000;
    regs[EDL_IDRIVE_CFG] = 0xBBBB;
    regs[EDL_DT_CFG] = 0x3131;
    regs[EDL_CSAMP_CFG] = 0x0028;
    regs[EDL_FAULT_ST] = 0x0020;    // DVDD UV from the power-up
    enabled = 0;
    enabled_at_write = -1;
    fault_pin = 0;
    frames = 0;
    stuck_reg = -1;
    no_answer = 0;
    pumps_after_polls = 2;
    supply_reads = 0;
}

void edl7141HwInit(void)
{
}

uint32_t edl7141SpiFrame(uint32_t frame)
{
    const int write = (frame & EDL_FRAME_WRITE) != 0;
    const uint8_t addr = (uint8_t)((frame & EDL_FRAME_ADDR_Msk) >> EDL_FRAME_ADDR_Pos);
    const uint16_t data = (uint16_t)(frame & EDL_FRAME_DATA_Msk);

    CHECK((frame >> 24) == 0, "frame fits in 24 bits: %06lx", (unsigned long)frame);
    if (frames < 64) {
        last_frames[frames] = frame;
    }
    frames++;
    if (no_answer) {
        return 0;
    }
    if (write) {
        if (addr == EDL_FAULTS_CLR) {
            if (data & EDL_CLR_FLTS) {
                regs[EDL_FAULT_ST] = 0;
                fault_pin = 0;
            }
        } else if (addr != stuck_reg) {
            regs[addr & 0x1F] = data;
            enabled_at_write = enabled;
        }
        return frame & 0xFF0000UL;  // SDO is high impedance during a write
    }
    uint16_t value = regs[addr & 0x1F];
    if (addr == EDL_SUPPLY_ST) {
        supply_reads++;
        // before both pumps are up only the low side one is (VCCLS_UVST)
        value = (enabled && supply_reads > pumps_after_polls) ? 0x003F : 0x0035;
    }
    return value;   // the read data comes back in the same frame
}

void edl7141SetEnable(uint8_t on)
{
    enabled = on;
}

uint8_t edl7141FaultActive(void)
{
    return (uint8_t)fault_pin;
}

void edl7141DelayUs(uint32_t us)
{
    (void)us;
}

// ---------------------------------------------------------------- tests
int main(void)
{
    // frame layout of the datasheet example (section 7.1.2.1): write FE01H to
    // TDRIVE_SRC_CFG (19H) is 99FE01H, the read of it 19xxxxH
    CHECK(EDL_FRAME(1, EDL_TDRIVE_SRC_CFG, 0xFE01) == 0x99FE01UL, "write frame %06lx",
        (unsigned long)EDL_FRAME(1, EDL_TDRIVE_SRC_CFG, 0xFE01));
    CHECK(EDL_FRAME(0, EDL_TDRIVE_SRC_CFG, 0) == 0x190000UL, "read frame");

    // normal start
    deviceReset();
    CHECK(edl7141Start() == EDL_OK, "start: %u", edl7141_status);
    CHECK(edl7141_status == EDL_OK, "status ok");
    CHECK(enabled == 1, "EN_DRV high after the start");
    CHECK(enabled_at_write == 0, "configuration written with EN_DRV low (standby programmable)");
    CHECK(regs[EDL_PWM_CFG] == 0x0000, "6PWM mode");
    CHECK(regs[EDL_DT_CFG] == EDL_CFG_DT, "dead time %04x", regs[EDL_DT_CFG]);
    CHECK(regs[EDL_DT_CFG] != 0x3131, "4 us reset dead time replaced");
    CHECK(regs[EDL_IDRIVE_CFG] == EDL_CFG_IDRIVE, "gate current");
    CHECK(regs[EDL_CSAMP_CFG] == EDL_CFG_CSAMP, "current sense amplifiers");
    CHECK((regs[EDL_CSAMP_CFG] & 0x0070) == 0, "all current sense amplifiers off");
    CHECK((regs[EDL_SUPPLY_CFG] & 0x0003) == 0, "PVCC 12 V");
    CHECK(regs[EDL_FAULT_ST] == 0, "power-up faults cleared");
    CHECK(last_frames[0] == EDL_FRAME(0, EDL_DEVICE_ID, 0), "first frame reads DEVICE_ID");
    // every configuration write is followed by a read of the same register
    for (int i = 1; i < 11; i += 2) {
        CHECK((last_frames[i] & EDL_FRAME_WRITE) && !(last_frames[i + 1] & EDL_FRAME_WRITE)
                && ((last_frames[i] ^ last_frames[i + 1]) & EDL_FRAME_ADDR_Msk) == 0,
            "frame %d: write then read back", i);
    }

    // SPI not connected: SDO reads 0, the driver stays disabled
    deviceReset();
    no_answer = 1;
    CHECK(edl7141Start() == EDL_ERR_ID, "no SPI answer: %u", edl7141_status);
    CHECK(enabled == 0, "EN_DRV low without SPI");

    // a register that does not take the value
    deviceReset();
    stuck_reg = EDL_DT_CFG;
    CHECK(edl7141Start() == EDL_ERR_VERIFY, "read back mismatch: %u", edl7141_status);
    CHECK(edl7141_bad_reg == EDL_DT_CFG, "bad register %02x", edl7141_bad_reg);
    CHECK(enabled == 0, "EN_DRV low after a failed read back");

    // charge pumps never come up
    deviceReset();
    pumps_after_polls = 1000;
    CHECK(edl7141Start() == EDL_ERR_GATE_SUPPLY, "charge pumps: %u", edl7141_status);
    CHECK(enabled == 0, "EN_DRV low again without gate supply");
    CHECK(supply_reads == EDL_GATE_SUPPLY_POLLS, "polled %d times", supply_reads);

    // fault service
    deviceReset();
    CHECK(edl7141Start() == EDL_OK, "restart");
    uint16_t count = edl7141_fault_count;
    int before = frames;
    edl7141Service();
    CHECK(frames == before, "no SPI traffic without nFAULT");
    regs[EDL_FAULT_ST] = EDL_FLT_OTW;
    fault_pin = 1;
    edl7141Service();
    CHECK(edl7141_fault_st == EDL_FLT_OTW, "fault stored: %04x", edl7141_fault_st);
    CHECK(edl7141_fault_count == (uint16_t)(count + 1), "fault counted");
    CHECK(fault_pin == 0 && regs[EDL_FAULT_ST] == 0, "fault cleared");

    // no service after a failed start
    deviceReset();
    no_answer = 1;
    (void)edl7141Start();
    fault_pin = 1;
    before = frames;
    edl7141Service();
    CHECK(frames == before, "no service while not started");

    return report("test_edl7141");
}
