/*
 * comparator.h
 *
 * BEMF zero cross detection with the HPPASS CSG comparators.
 */

#ifndef COMPARATOR_H_
#define COMPARATOR_H_

#include "main.h"

// HPPASS_CSG_SLICE CMP_CFG input selections
#define CSG_POS_AN_A        0UL     // AN_Ak
#define CSG_POS_AN_B        1UL     // AN_Bk
#define CSG_NEG_DAC         0UL     // DAC of the slice
#define CSG_NEG_AN_A        2UL     // AN_A(k+1)
#define CSG_NEG_AN_B        3UL     // AN_B(k+1)

// DAC code of the virtual neutral before the first bus voltage reading (VDDA / 2)
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

#endif /* COMPARATOR_H_ */
