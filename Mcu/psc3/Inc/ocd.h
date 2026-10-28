/*
 * ocd.h
 *
 * Fast over-current inputs (targets with USE_OCD_INPUTS): the open drain,
 * active low OCD1 / OCD2 outputs of a TLI4971 current sensor.
 */

#ifndef OCD_H_
#define OCD_H_

#include <stdint.h>

#define OCD1_BIT    0x01U
#define OCD2_BIT    0x02U

void ocdInit(void);
uint8_t ocdLatched(void);       // OCD1_BIT / OCD2_BIT latched since the last ocdClear()
uint8_t ocdTakeNew(void);       // bits latched since the last call
void ocdClear(void);            // releases the latch of the inputs that are high again

extern volatile uint16_t ocd_trips;

#endif /* OCD_H_ */
