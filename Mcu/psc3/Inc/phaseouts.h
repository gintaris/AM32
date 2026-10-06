/*
 * phaseouts.h
 *
 * Gate output control for PSC3, the phase states are set with TCPWM LINE_SEL.
 */

#ifndef PHASEOUTS_H_
#define PHASEOUTS_H_

#include "main.h"

void allOff(void);
void comStep(int newStep);
void fullBrake(void);
void allpwm(void);
void proportionalBrake(void);
void twoChannelForward(void);
void twoChannelReverse(void);

#endif /* PHASEOUTS_H_ */
