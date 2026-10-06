/*
 * hppass.h
 *
 * Start-up of the PSC3 high performance analog subsystem (HPPASS), shared by
 * the CSG comparators (comparator.c) and the SAR ADC (ADC.c).
 */

#ifndef HPPASS_H_
#define HPPASS_H_

#include "main.h"

// configure the CSG slices and the SAR first, then start the blocks.
// Returns 1 when the blocks report ready
uint8_t hppassStart(uint32_t csg_mask, uint8_t sar_enable);
uint8_t hppassReady(void);

#endif /* HPPASS_H_ */
