/*
 * host test stub: comparator.h with the HPPASS CSG registers as plain
 * memory, field layout as in cyip_hppass.h
 */
#ifndef COMPARATOR_H_
#define COMPARATOR_H_

#include <stdint.h>

typedef struct {
    uint32_t CMP_CFG;
    uint32_t DAC_CFG;
    uint32_t DAC_VAL;
    uint32_t CMP_STATUS;
} HPPASS_CSG_SLICE_Type;

typedef struct {
    struct {
        HPPASS_CSG_SLICE_Type SLICE[5];
        uint32_t CSG_CTRL;
        uint32_t CMP_INTR;
        uint32_t CMP_INTR_MASK;
    } CSG;
} HPPASS_Type;

extern HPPASS_Type fake_hppass;
#define HPPASS (&fake_hppass)

#define HPPASS_CSG_SLICE_CMP_CFG_CMP_POS_SEL_Pos    0UL
#define HPPASS_CSG_SLICE_CMP_CFG_CMP_NEG_SEL_Pos    2UL
#define HPPASS_CSG_SLICE_CMP_CFG_CMP_EDGE_MODE_Pos  12UL
#define HPPASS_CSG_SLICE_CMP_STATUS_CMP_VAL_Msk     0x1UL
#define HPPASS_CSG_SLICE_DAC_VAL_VALUE_Msk          0x3FFUL

#define pass_interrupt_csg_cmps_IRQn 138
void NVIC_ClearPendingIRQ(int irq);
void NVIC_EnableIRQ(int irq);

#define CSG_POS_AN_A        0UL
#define CSG_POS_AN_B        1UL
#define CSG_NEG_DAC         0UL
#define CSG_NEG_AN_A        2UL
#define CSG_NEG_AN_B        3UL
#define COMP_REFERENCE_DEFAULT  512U

void initComparators(void);
uint32_t comparatorSliceMask(void);
uint8_t getCompOutputLevel(void);
void maskPhaseInterrupts(void);
void enableCompInterrupts(void);
void changeCompInput(void);
void setCompReference(uint16_t dac);
uint16_t compReferenceFromBus(uint16_t adc_raw_volts);

extern volatile char rising;
extern char step;

#endif
