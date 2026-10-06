/*
 * host test stub: the PWM counter registers used by phaseouts.c are plain
 * memory, the field layout is the one of cyip_tcpwm.h
 */
#ifndef PERIPHERALS_H_
#define PERIPHERALS_H_

#include <stdint.h>

typedef struct {
    uint32_t LINE_SEL;
} TCPWM_GRP_CNT_Type;

extern TCPWM_GRP_CNT_Type fake_pwm_cnt[8];

#define PWM_CNT_FIRST       4
#define PWM_CNT_LAST        6
#define PWM_TIMER(n)        (&fake_pwm_cnt[(n)])

#define TCPWM_GRP_CNT_LINE_SEL_OUT_SEL_Pos          0UL
#define TCPWM_GRP_CNT_LINE_SEL_OUT_SEL_Msk          0x7UL
#define TCPWM_GRP_CNT_LINE_SEL_COMPL_OUT_SEL_Pos    4UL

#define TCPWM_LINE_L        0UL
#define TCPWM_LINE_H        1UL
#define TCPWM_LINE_PWM      2UL
#define TCPWM_LINE_PWM_INV  3UL

#endif
