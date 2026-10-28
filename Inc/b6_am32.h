/*
 * b6_am32.h
 *
 * Protocol B6 in the AM32 firmware (targets with USE_B6_PROTOCOL): the glue
 * between b6_protocol.c and main.c, and the hooks the MCU layer provides
 * (UART, input capture, identity, fault inputs).
 */

#ifndef B6_AM32_H_
#define B6_AM32_H_

#include <stdint.h>

void b6Am32Init(void);      // after the peripherals run
void b6Am32Poll(void);      // main loop
void b6Am32Tick(void);      // tenKhzRoutine(), LOOP_FREQUENCY_HZ

extern volatile uint32_t b6_commutations;   // counted in PeriodElapsedCallback()

// MCU layer
void b6HwInit(void);
uint8_t b6HwRead(uint8_t* byte);                        // 1 if a received byte was taken
uint8_t b6HwWrite(const uint8_t* data, uint32_t len);   // queues all bytes or none
void b6HwInputCapture(uint8_t on);                      // DShot / servo input on or off
uint32_t b6HwResetCause(void);
uint64_t b6HwMcuId(void);
uint16_t b6HwPowerStageId(void);
uint16_t b6HwPowerInputId(void);
uint16_t b6HwFlags(void);           // B6_FLAG_OCD1 / OCD2 / DRIVER_FAULT / DRIVER_ENABLED / NBRAKE
uint8_t b6HwFault(void);            // a fault that refuses ARM
uint8_t b6HwOcdTripped(void);       // OCD bits latched since the last call (bit 0 OCD1, bit 1 OCD2)
void b6HwClearFaults(void);         // DISARM request: release the over-current latch if the inputs are high again

#endif /* B6_AM32_H_ */
