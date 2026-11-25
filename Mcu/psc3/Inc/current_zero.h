/*
 * current_zero.h - zero calibration of the current sensor (PSC3 layer)
 *
 * The current sensor zero (TLI4971 VREF on the DEV ESC) and the SAR reference
 * (VDDA) are not exactly the CURRENT_OFFSET / 3.3 V of the target: on the
 * DEV ESC Rev B the zero read 1.663 V instead of 1.650 V, about 1.3 A at
 * 10 mV/A. While the motor is stopped the raw current reading is averaged and
 * every reading is shifted by (zero - nominal), so the common code keeps
 * CURRENT_OFFSET as it is.
 *
 * - the zero is learned only while the motor is stopped, frozen while it runs
 * - readings outside nominal +- window are ignored (no sensor, broken wiring):
 *   then no correction is applied
 * - the correction is applied only after CURRENT_ZERO_MIN_SAMPLES readings
 */

#ifndef CURRENT_ZERO_H_
#define CURRENT_ZERO_H_

#include <stdint.h>

#define CURRENT_ZERO_SHIFT          6U      // IIR filter 1/64 (64 ms at 1 kHz)
#define CURRENT_ZERO_MIN_SAMPLES    256U

typedef struct {
    uint32_t acc;       // zero << CURRENT_ZERO_SHIFT
    uint16_t zero;      // learned zero, raw counts
    uint16_t samples;   // accepted readings, saturating
    uint8_t valid;      // 1 once samples >= CURRENT_ZERO_MIN_SAMPLES
} current_zero_t;

void currentZeroInit(current_zero_t* z, uint16_t nominal);

// one 1 kHz reading: learns while motor_off, returns the corrected reading
uint16_t currentZeroUpdate(current_zero_t* z, uint16_t raw, uint8_t motor_off,
    uint16_t nominal, uint16_t window);

#endif /* CURRENT_ZERO_H_ */
