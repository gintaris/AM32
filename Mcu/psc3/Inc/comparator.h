/*
 * comparator.h
 *
 * BEMF zero cross detection with the HPPASS CSG comparators.
 */

#ifndef COMPARATOR_H_
#define COMPARATOR_H_

#include "main.h"

void initComparators(void);
uint8_t getCompOutputLevel(void);
void maskPhaseInterrupts(void);
void enableCompInterrupts(void);
void changeCompInput(void);

extern volatile char rising;
extern char step;

#endif /* COMPARATOR_H_ */
