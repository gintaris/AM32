/*
 * serial_telemetry.c
 *
 * Telemetry UART on SCB0 with DataWire DMA, DP10.
 */

#include "serial_telemetry.h"

#include "kiss_telemetry.h"
#include "targets.h"

void telem_UART_Init(void)
{
}

void send_telem_DMA(uint8_t bytes)
{
    (void)bytes;
}
