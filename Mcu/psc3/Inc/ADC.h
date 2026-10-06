/*
 * ADC.h
 *
 * HPPASS SAR ADC for voltage, current and temperature.
 */

#ifndef ADC_H_
#define ADC_H_

#include "main.h"

void ADC_Init(void);
void activateADC(void);
void enableADC_DMA(void);
void ADC_DMA_Callback(void);
void startADCConversion(void);
int16_t getConvertedDegrees(uint16_t adcrawtemp);

#endif /* ADC_H_ */
