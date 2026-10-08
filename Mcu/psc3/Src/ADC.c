/*
 * ADC.c
 *
 * HPPASS SAR ADC, 12 bit, VDDA reference. Two sequencer entries:
 *  - entry 1, firmware trigger HPPASS TR0: bus voltage, current and
 *    temperature, started by startADCConversion() from the 1 kHz part of the
 *    main loop; ADC_DMA_Callback() picks up the previous results
 *  - entry 0 (BEMF_ADC targets): the three phase dividers sampled at the same
 *    time, triggered in the middle of every PWM on time by the CC1 match of
 *    PWM counter 261 (tcpwm tr_out1[261] -> 1:1 trigger -> pass.tr_b_in[2] ->
 *    HPPASS TR2); its interrupt runs the comparator emulation
 *    (comparator_adc.c)
 * The results are read from the channel result registers, no DMA is needed.
 * The SAR is configured before hppassStart() and calibrates offset and gain
 * when the AC enables it.
 */

#include "ADC.h"

#include "comparator.h"
#include "peripherals.h"
#include "targets.h"

extern uint16_t ADC_raw_temp;
extern uint16_t ADC_raw_volts;
extern uint16_t ADC_raw_current;

#define ENTRY_BEMF          0U
#define ENTRY_SLOW          1U
#define HPPASS_TR_FW        0U      // HPPASS trigger 0: firmware pulse
#define HPPASS_TR_PWM       2U      // HPPASS trigger 2: pass.tr_b_in[2]
#define TR_SEL_FW_PULSE     3UL     // INFRA TR_IN_SEL values
#define TR_SEL_HW_B         2UL
#define TR_MODE_PULSE_POS   0UL     // pulse on the rising edge, double synced
#define ENTRY_TR(n)         ((uint32_t)(n) + 1UL)   // SEQ_ENTRY TR_SEL: 1 = TR0
#define SAMPLE_TIME_CLOCKS  64UL    // clk_hf3 / 2 = 90 MHz -> 711 ns for the dividers
#define TRIG_1TO1_GROUP_PASS 2U     // PERI 1:1 trigger group 2, tcpwm tr_out1 -> pass.tr_b_in
#define PWM_CNT_ADC_TRIGGER 5U      // counter 261, its tr_out1 reaches pass.tr_b_in[2]

// sampler enable bits of a channel for SEQ_ENTRY / SAMP_EN: channels 0..11 are
// direct samplers, channels 12..15 use muxed sampler 12 (MUX0)
static uint32_t samplerBits(uint32_t channel)
{
    if (channel < 12U) {
        return 1UL << channel;
    }
    return (1UL << 12) | ((channel - 12U) << HPPASS_SAR_SEQ_ENTRY_MUX0_SEL_Pos);
}

static uint32_t sampEnBits(uint32_t channel)
{
    return (channel < 12U) ? (1UL << channel) : (1UL << HPPASS_SAR_CFG_SAMP_EN_MUXED_SAMP_EN_Pos);
}

void ADC_Init(void)
{
    uint32_t slow = samplerBits(VOLTAGE_ADC_CHANNEL) | samplerBits(CURRENT_ADC_CHANNEL)
        | samplerBits(TEMP_ADC_CHANNEL);
    uint32_t samp_en = sampEnBits(VOLTAGE_ADC_CHANNEL) | sampEnBits(CURRENT_ADC_CHANNEL)
        | sampEnBits(TEMP_ADC_CHANNEL);

    HPPASS->SAR.CFG.CTRL = HPPASS_SAR_CFG_CTRL_STARTUP_CAL_OFFSET_EN_Msk
        | HPPASS_SAR_CFG_CTRL_STARTUP_CAL_GAIN_EN_Msk;     // VDDA reference
    HPPASS->SAR.CFG.SAMPLE_TIME[0] = SAMPLE_TIME_CLOCKS - 1UL;
    for (uint32_t ch = 0; ch < 28U; ch++) {
        HPPASS->SAR.CFG.CHAN_CFG[ch] = 0;   // single ended, unsigned, no averaging, no FIFO
    }

    HPPASS->SAR.SEQ_ENTRY[ENTRY_SLOW] = slow
        | (ENTRY_TR(HPPASS_TR_FW) << HPPASS_SAR_SEQ_ENTRY_TR_SEL_Pos)
        | (1UL << HPPASS_SAR_SEQ_ENTRY_SAMP_TIME_SEL_Pos);

    uint32_t tr_in_sel = TR_SEL_FW_PULSE << HPPASS_INFRA_TR_IN_SEL_TR0_SEL_Pos;

#ifdef BEMF_ADC
    const uint32_t bemf = samplerBits(PHASE_A_ADC_CHANNEL) | samplerBits(PHASE_B_ADC_CHANNEL)
        | samplerBits(PHASE_C_ADC_CHANNEL);
    samp_en |= sampEnBits(PHASE_A_ADC_CHANNEL) | sampEnBits(PHASE_B_ADC_CHANNEL)
        | sampEnBits(PHASE_C_ADC_CHANNEL);
    HPPASS->SAR.SEQ_ENTRY[ENTRY_BEMF] = bemf
        | (ENTRY_TR(HPPASS_TR_PWM) << HPPASS_SAR_SEQ_ENTRY_TR_SEL_Pos)
        | (1UL << HPPASS_SAR_SEQ_ENTRY_SAMP_TIME_SEL_Pos)
        | HPPASS_SAR_SEQ_ENTRY_PRIORITY_Msk;

    tr_in_sel |= TR_SEL_HW_B << HPPASS_INFRA_TR_IN_SEL_TR2_SEL_Pos;
    HPPASS->INFRA.HW_TR_MODE = TR_MODE_PULSE_POS << HPPASS_INFRA_HW_TR_MODE_HW_TR2_MODE_Pos;

    // PWM counter 261: tr_out1 on the CC1 match (CC1 = duty / 2, peripherals.h)
    PWM_TIMER(PWM_CNT_ADC_TRIGGER)->TR_OUT_SEL = (2UL << TCPWM_GRP_CNT_TR_OUT_SEL_OUT0_Pos)
        | (4UL << TCPWM_GRP_CNT_TR_OUT_SEL_OUT1_Pos);
    PERI->TR_1TO1_GR[TRIG_1TO1_GROUP_PASS].TR_CTL[HPPASS_TR_PWM] = PERI_TR_1TO1_GR_TR_CTL_TR_SEL_Msk;

    HPPASS->SAR.CFG.SAR_RESULT_INTR = 1UL << ENTRY_BEMF;
    HPPASS->SAR.CFG.SAR_RESULT_INTR_MASK = 1UL << ENTRY_BEMF;
#endif

    HPPASS->SAR.CFG.SAMP_EN = samp_en;
    HPPASS->INFRA.TR_IN_SEL = tr_in_sel;
}

// the triggers run once the HPPASS reports BLOCK_READY
void activateADC(void)
{
#ifdef BEMF_ADC
    NVIC_ClearPendingIRQ(COMPARATOR_IRQ);
    NVIC_EnableIRQ(COMPARATOR_IRQ);
#endif
    startADCConversion();
}

// the results are read from the channel result registers
void enableADC_DMA(void)
{
}

void startADCConversion(void)
{
    HPPASS->INFRA.FW_TR_PULSE = 1UL << HPPASS_TR_FW;
}

void ADC_DMA_Callback(void)
{
    ADC_raw_volts = (uint16_t)HPPASS->SAR.CFG.CHAN_RESULT[VOLTAGE_ADC_CHANNEL];
    ADC_raw_current = (uint16_t)HPPASS->SAR.CFG.CHAN_RESULT[CURRENT_ADC_CHANNEL];
    ADC_raw_temp = (uint16_t)HPPASS->SAR.CFG.CHAN_RESULT[TEMP_ADC_CHANNEL];
    setCompReference(compReferenceFromBus(ADC_raw_volts));
}

#ifdef BEMF_ADC
// SAR entry 0 done: the phase samples of this PWM period
void pass_interrupt_sar_entry_done_0_IRQHandler(void)
{
    HPPASS->SAR.CFG.SAR_RESULT_INTR = 1UL << ENTRY_BEMF;
    (void)HPPASS->SAR.CFG.SAR_RESULT_INTR;

    const uint16_t samples[3] = {
        (uint16_t)HPPASS->SAR.CFG.CHAN_RESULT[PHASE_A_ADC_CHANNEL],
        (uint16_t)HPPASS->SAR.CFG.CHAN_RESULT[PHASE_B_ADC_CHANNEL],
        (uint16_t)HPPASS->SAR.CFG.CHAN_RESULT[PHASE_C_ADC_CHANNEL],
    };
    bemfAdcProcess(samples, (uint16_t)INTERVAL_TIMER_COUNT);
}
#endif

/*
 * Thermistor on AN_A3 to degrees C: NTC to ground with a pull-up of the same
 * resistance to VDDA (kit: 100 k TH1, 100 k R59; the B constant of TH1 is
 * not in the schematic, the table uses B 3380). The table holds the
 * temperature at every 64th ADC count.
 */
static const int16_t ntc_table[65] = {
    150, 150, 150, 133, 119, 108, 100, 93, 87, 82, 77, 73, 69, 66, 63, 60, 57,
    54, 52, 50, 47, 45, 43, 41, 39, 37, 35, 34, 32, 30, 28, 27, 25, 23, 22, 20,
    19, 17, 15, 14, 12, 11, 9, 7, 6, 4, 2, 0, -1, -3, -5, -7, -9, -11, -14, -16,
    -19, -22, -25, -28, -33, -38, -40, -40, -40
};

int16_t getConvertedDegrees(uint16_t adcrawtemp)
{
    const uint32_t raw = adcrawtemp > 4095U ? 4095U : adcrawtemp;
    const uint32_t i = raw >> 6;
    const int32_t frac = (int32_t)(raw & 63U);
    const int32_t t0 = ntc_table[i];
    const int32_t t1 = ntc_table[i < 64U ? i + 1U : 64U];
    return (int16_t)(t0 + ((t1 - t0) * frac) / 64);
}
