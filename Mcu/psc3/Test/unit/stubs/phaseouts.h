/*
 * host test stub: phaseouts.h without the device headers
 */
#ifndef PHASEOUTS_H_
#define PHASEOUTS_H_

#include <stdint.h>

void allOff(void);
void comStep(int newStep);
void fullBrake(void);
void allpwm(void);
void proportionalBrake(void);
void twoChannelForward(void);
void twoChannelReverse(void);

#endif
