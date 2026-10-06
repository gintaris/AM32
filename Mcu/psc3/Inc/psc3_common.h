/*
 * psc3_common.h
 *
 * Register helpers shared by the PSC3 MCU layer.
 */

#ifndef PSC3_COMMON_H_
#define PSC3_COMMON_H_

#include <stdint.h>

// TCPWM counter registers, group 0 has 32 bit counters 0..3, group 1 has
// 16 bit counters 256..263 with motor control features, group 2 has
// 16 bit counters 512..519
#define TCPWM_CNT(grp, cnt) (&TCPWM0->GRP[grp].CNT[cnt])

#define TCPWM_CMD_CAPTURE0  (1UL << TCPWM_GRP_CNT_TR_CMD_CAPTURE0_Pos)
#define TCPWM_CMD_RELOAD    (1UL << TCPWM_GRP_CNT_TR_CMD_RELOAD_Pos)
#define TCPWM_CMD_STOP      (1UL << TCPWM_GRP_CNT_TR_CMD_STOP_Pos)
#define TCPWM_CMD_START     (1UL << TCPWM_GRP_CNT_TR_CMD_START_Pos)

#define TCPWM_INTR_TC       (1UL << TCPWM_GRP_CNT_INTR_TC_Pos)
#define TCPWM_INTR_CC0      (1UL << TCPWM_GRP_CNT_INTR_CC0_MATCH_Pos)

// LINE_SEL.OUT_SEL / COMPL_OUT_SEL values
#define TCPWM_LINE_L        0UL     // fixed '0'
#define TCPWM_LINE_H        1UL     // fixed '1'
#define TCPWM_LINE_PWM      2UL     // PWM signal 'line'
#define TCPWM_LINE_PWM_INV  3UL     // inverted PWM signal 'line'
#define TCPWM_LINE_Z        4UL     // not driven by the TCPWM

// GPIO drive modes (GPIO_PRT CFG.DRIVE_MODEx), the input buffer is always on
#define GPIO_DM_HIGHZ       0UL
#define GPIO_DM_PULLUP      2UL
#define GPIO_DM_PULLDOWN    3UL
#define GPIO_DM_STRONG      6UL
#define GPIO_IN_EN          8UL

// HSIOM selection for software controlled GPIO
#define HSIOM_SEL_GPIO      0UL

// route a pin (HSIOM) and set its drive mode
static inline void gpioSetMode(uint32_t port, uint32_t pin, uint32_t drive, uint32_t hsiom)
{
    volatile uint32_t* sel = (pin < 4U) ? &HSIOM->PRT[port].PORT_SEL0 : &HSIOM->PRT[port].PORT_SEL1;
    const uint32_t sel_shift = (pin & 3U) * 8U;
    const uint32_t cfg_shift = pin * 4U;

    *sel = (*sel & ~(0x1FUL << sel_shift)) | (hsiom << sel_shift);
    GPIO->PRT[port].CFG = (GPIO->PRT[port].CFG & ~(0xFUL << cfg_shift))
        | ((drive | GPIO_IN_EN) << cfg_shift);
}

static inline void gpioWrite(uint32_t port, uint32_t pin, uint32_t level)
{
    if (level) {
        GPIO->PRT[port].OUT_SET = 1UL << pin;
    } else {
        GPIO->PRT[port].OUT_CLR = 1UL << pin;
    }
}

static inline uint32_t gpioRead(uint32_t port, uint32_t pin)
{
    return (GPIO->PRT[port].IN >> pin) & 1UL;
}

static inline void modifyReg32(volatile uint32_t* reg, uint32_t clearbits, uint32_t setbits)
{
    *reg = (*reg & ~clearbits) | setbits;
}

#endif /* PSC3_COMMON_H_ */
