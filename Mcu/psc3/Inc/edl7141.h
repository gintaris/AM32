/*
 * edl7141.h
 *
 * Infineon 6EDL7141 three phase gate driver with buck, DVDD regulator and
 * charge pumps, configured over SPI (targets with USE_6EDL7141).
 *
 * Register addresses, fields and reset values from the 6EDL7141 datasheet
 * Rev 1.04 (2021-10-28), section 8. SPI frame (section 7.1.2): 24 bits, MSB
 * first, bit 23 command (1 write, 0 read), bits 22:16 address, bits 15:0
 * data. A read returns the register in the data bits of the same frame. SCLK
 * idles low and SDI is sampled on the falling edge (CPOL 0, CPHA 1).
 */

#ifndef EDL7141_H_
#define EDL7141_H_

#include <stdint.h>

// registers (table 19)
#define EDL_FAULT_ST            0x00U   // fault and warning status
#define EDL_TEMP_ST             0x01U   // temperature status
#define EDL_SUPPLY_ST           0x02U   // power supply status
#define EDL_FUNCT_ST            0x03U   // functional status
#define EDL_OTP_ST              0x04U   // OTP status
#define EDL_ADC_ST              0x05U   // ADC status
#define EDL_CP_ST               0x06U   // charge pumps status
#define EDL_DEVICE_ID           0x07U   // device ID
#define EDL_FAULTS_CLR          0x10U   // fault clear (write only)
#define EDL_SUPPLY_CFG          0x11U   // power supply configuration
#define EDL_ADC_CFG             0x12U   // ADC configuration
#define EDL_PWM_CFG             0x13U   // PWM configuration
#define EDL_SENSOR_CFG          0x14U   // sensor configuration
#define EDL_WD_CFG              0x15U   // watchdog configuration
#define EDL_WD_CFG2             0x16U   // watchdog configuration 2
#define EDL_IDRIVE_CFG          0x17U   // gate driver current
#define EDL_IDRIVE_PRE_CFG      0x18U   // pre-charge gate driver current
#define EDL_TDRIVE_SRC_CFG      0x19U   // gate driver sourcing timing
#define EDL_TDRIVE_SINK_CFG     0x1AU   // gate driver sinking timing
#define EDL_DT_CFG              0x1BU   // dead time
#define EDL_CP_CFG              0x1CU   // charge pump configuration
#define EDL_CSAMP_CFG           0x1DU   // current sense amplifier configuration
#define EDL_CSAMP_CFG2          0x1EU   // current sense amplifier configuration 2
#define EDL_OTP_PROG            0x1FU   // OTP program

// SPI frame
#define EDL_FRAME_WRITE         (1UL << 23)
#define EDL_FRAME_ADDR_Pos      16U
#define EDL_FRAME_ADDR_Msk      (0x7FUL << EDL_FRAME_ADDR_Pos)
#define EDL_FRAME_DATA_Msk      0xFFFFUL
#define EDL_FRAME(write, addr, data) \
    (((write) ? EDL_FRAME_WRITE : 0UL) | (((uint32_t)(addr) << EDL_FRAME_ADDR_Pos) & EDL_FRAME_ADDR_Msk) \
        | ((uint32_t)(data) & EDL_FRAME_DATA_Msk))

// DEVICE_ID: DEV_ID 3:0, reset value 0006H
#define EDL_DEV_ID_Msk          0x000FU
#define EDL_DEV_ID_VALUE        0x0006U

// FAULTS_CLR
#define EDL_CLR_FLTS            0x0001U // all faults except latched ones
#define EDL_CLR_LATCH           0x0002U // latched faults only

// SUPPLY_ST: '1' = above the UVLO threshold
#define EDL_VCCLS_UVST          0x0001U
#define EDL_VCCHS_UVST          0x0002U
#define EDL_DVDD_UVST           0x0004U
#define EDL_VDDB_UVST           0x0010U
#define EDL_SUPPLY_GATES_OK     (EDL_VCCLS_UVST | EDL_VCCHS_UVST)

// FAULT_ST bits
#define EDL_FLT_CS_OCP_Msk      0x0007U
#define EDL_FLT_CP              0x0008U
#define EDL_FLT_DVDD_OCP        0x0010U
#define EDL_FLT_DVDD_UV         0x0020U
#define EDL_FLT_DVDD_OV         0x0040U
#define EDL_FLT_BK_OCP          0x0080U
#define EDL_FLT_OTS             0x0100U
#define EDL_FLT_OTW             0x0200U
#define EDL_FLT_RLOCK           0x0400U
#define EDL_FLT_WD              0x0800U
#define EDL_FLT_OTP             0x1000U

/*
 * Configuration written at start-up while EN_DRV is low (most fields are
 * 'standby programmable', section 8.1), then read back. A target can
 * override any value before including this file.
 */
// SUPPLY_CFG: reset value 6000H, PVCC (gate drive) 12 V in bits 1:0 (b00),
// DVDD from the VSENSE resistor, buck 500 kHz
#ifndef EDL_CFG_SUPPLY
#define EDL_CFG_SUPPLY          0x6000U
#endif
// PWM_CFG: 6PWM mode (b000), low side braking: AM32 drives every switch
#ifndef EDL_CFG_PWM
#define EDL_CFG_PWM             0x0000U
#endif
// IDRIVE_CFG: reset BBBBH = 200 mA everywhere, too slow for two paralleled
// MOSFETs per switch. Source 400 mA (b1110), sink 500 mA (b1111) on both
// sides; to be tuned on the board (slew rate against ringing)
#ifndef EDL_CFG_IDRIVE
#define EDL_CFG_IDRIVE          0xFEFEU
#endif
// DT_CFG: reset 3131H = 4.04 us rise and fall, ~10 % of a 24 kHz period.
// The PSC3 PWM already inserts DEAD_TIME, so the minimum 120 ns (b0) here
#ifndef EDL_CFG_DT
#define EDL_CFG_DT              0x0000U
#endif
// CSAMP_CFG: reset 0028H. The current comes from the TLI4971: amplifiers
// off (CS_EN 6:4 = 0), digital gain (CS_GAIN_ANA 3 = 0), no OCP fault
// trigger (CS_OCPFLT_CFG 15:14 = b11)
#ifndef EDL_CFG_CSAMP
#define EDL_CFG_CSAMP           0xC000U
#endif

// edl7141Start() result, also in edl7141_status for the debugger
#define EDL_OK                  0U
#define EDL_ERR_ID              1U      // no answer or wrong DEVICE_ID: check SPI
#define EDL_ERR_VERIFY          2U      // a register read back differently
#define EDL_ERR_GATE_SUPPLY     3U      // charge pumps did not come up after EN_DRV
#define EDL_NOT_STARTED         0xFFU

// time for the charge pumps after EN_DRV, in 100 us polls
#ifndef EDL_GATE_SUPPLY_POLLS
#define EDL_GATE_SUPPLY_POLLS   50U
#endif

uint8_t edl7141Start(void);
void edl7141Service(void);
uint16_t edl7141Read(uint8_t reg);
void edl7141Write(uint8_t reg, uint16_t value);

extern volatile uint8_t edl7141_status;
extern volatile uint16_t edl7141_fault_st;     // FAULT_ST of the last fault
extern volatile uint16_t edl7141_fault_count;
extern volatile uint16_t edl7141_bad_reg;      // register that failed the read back

// hardware hooks, edl7141_hal.c on the PSC3 and stubs in the host tests
void edl7141HwInit(void);                      // SPI, nSCS high, EN_DRV low, nFAULT input
uint32_t edl7141SpiFrame(uint32_t frame);      // one 24 bit frame, returns the 24 bits read
void edl7141SetEnable(uint8_t on);             // EN_DRV pin
uint8_t edl7141FaultActive(void);              // nFAULT pin low
void edl7141DelayUs(uint32_t us);

#endif /* EDL7141_H_ */
