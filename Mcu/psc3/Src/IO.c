/*
 * IO.c
 *
 * DShot / PWM signal input with TCPWM capture and DataWire DMA, DP8.
 */

#include "IO.h"

#include "common.h"
#include "targets.h"

uint32_t dma_buffer[64] = { 0 };

void changeToOutput(void)
{
}

void changeToInput(void)
{
}

void receiveDshotDma(void)
{
}

void sendDshotDma(void)
{
}

uint8_t getInputPinState(void)
{
    return 0;
}

void setInputPolarityRising(void)
{
}

void setInputPullDown(void)
{
}

void setInputPullUp(void)
{
}

void enableHalfTransferInt(void)
{
}

void setInputPullNone(void)
{
}
