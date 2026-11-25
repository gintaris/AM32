/*
 * current_zero.c - zero calibration of the current sensor (see current_zero.h)
 */

#include "current_zero.h"

void currentZeroInit(current_zero_t* z, uint16_t nominal)
{
    z->acc = (uint32_t)nominal << CURRENT_ZERO_SHIFT;
    z->zero = nominal;
    z->samples = 0;
    z->valid = 0;
}

uint16_t currentZeroUpdate(current_zero_t* z, uint16_t raw, uint8_t motor_off,
    uint16_t nominal, uint16_t window)
{
    const int32_t dev = (int32_t)raw - (int32_t)nominal;
    if (motor_off && dev <= (int32_t)window && dev >= -(int32_t)window) {
        if (z->samples == 0) {
            z->acc = (uint32_t)raw << CURRENT_ZERO_SHIFT;   // start from the first reading
        } else {
            z->acc = z->acc - (z->acc >> CURRENT_ZERO_SHIFT) + raw;
        }
        z->zero = (uint16_t)(z->acc >> CURRENT_ZERO_SHIFT);
        if (z->samples < 0xFFFFU) {
            z->samples++;
        }
        if (z->samples >= CURRENT_ZERO_MIN_SAMPLES) {
            z->valid = 1;
        }
    }
    if (!z->valid) {
        return raw;
    }
    int32_t out = (int32_t)raw - ((int32_t)z->zero - (int32_t)nominal);
    if (out < 0) {
        out = 0;
    } else if (out > 4095) {
        out = 4095;
    }
    return (uint16_t)out;
}
