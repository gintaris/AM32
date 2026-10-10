/*
 * bemf_adc.h
 *
 * BEMF zero cross detection with the SAR ADC (BEMF_ADC targets,
 * Src/comparator_adc.c). No device headers, so the SITL can build the same
 * code (Mcu/SITL/Src/sitl_psc3_bemf.c).
 */

#ifndef BEMF_ADC_H_
#define BEMF_ADC_H_

#include <stdint.h>

// virtual neutral before the first bus voltage reading: VDDA / 2 as 12 bit
// ADC count
#define COMP_REFERENCE_ADC_DEFAULT 2048U

// BEMF_ADC sampling point, chosen with the duty cycle (setPwmDutyAll()):
//  - ON:  middle of the PWM on time, neutral = VBUS / 2. The floating phase
//         also carries the voltage the di/dt of the driven windings couples
//         into it, which only cancels over a whole PWM period: fine at high
//         duty, where the on time di/dt is small
//  - OFF: middle of the off time with complementary PWM: both driven phases
//         are low, the floating phase shows the BEMF against 0 V. Used at low
//         duty (found on the kit: at 7 % duty the on time samples sat at
//         60..80 % of VBUS with the rotor standing still)
#define BEMF_SAMPLE_ON          0U
#define BEMF_SAMPLE_OFF         1U
#ifndef BEMF_OFF_THRESHOLD
#define BEMF_OFF_THRESHOLD      4U      // ADC counts above 0 V in the off time (noise 0..3 on the kit)
#endif
extern volatile uint8_t bemf_sample_mode_req;

void bemfAdcProcess(const uint16_t samples[3], uint16_t interval_count);
void setCompReference(uint16_t reference);
uint16_t compReferenceFromBus(uint16_t adc_raw_volts);

#endif /* BEMF_ADC_H_ */
