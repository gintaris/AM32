/*
 * phaseouts.c
 *
 * Gate output states for PSC3, DP4.
 */

#include "phaseouts.h"

#include "targets.h"

extern char prop_brake_active;

void allOff(void)
{
}

void comStep(int newStep)
{
    (void)newStep;
}

void fullBrake(void)
{
}

void allpwm(void)
{
}

void proportionalBrake(void)
{
}

void twoChannelForward(void)
{
}

void twoChannelReverse(void)
{
}
