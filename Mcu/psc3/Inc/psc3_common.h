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

static inline void modifyReg32(volatile uint32_t* reg, uint32_t clearbits, uint32_t setbits)
{
    *reg = (*reg & ~clearbits) | setbits;
}

#endif /* PSC3_COMMON_H_ */
