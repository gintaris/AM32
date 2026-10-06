/*
 * IO.c
 *
 * DShot / servo PWM input and bidirectional DShot output on one pin with
 * TCPWM counter 516 (IC_TIMER_REGISTER) and DataWire 0 (INPUT_DMA_CHANNEL).
 *
 * Input: the pin goes through peri.tr_io_input[38] and trigger group 11 to the
 * one-to-one trigger 1 of the counter, which captures on both edges. Every
 * capture (cc0 event, tr_out0) triggers one DMA transfer of CC0 into
 * dma_buffer[], like the STM32 input capture DMA. The counter clock is
 * CPU_FREQUENCY_MHZ / (ic_timer_prescaler + 1) (PCLK divider), the STM32 PSC.
 *
 * Output (bidirectional DShot telemetry): the pin is driven by line[516]. The
 * counter runs in PWM mode with one GCR bit per period, and every terminal
 * count triggers one DMA transfer that writes the next level ('0' / '1') to
 * LINE_SEL, so each bit is a constant level without compare glitches.
 * gcr[] != 0 drives the line low, like the inverted STM32 output (CC1P).
 */

#include "IO.h"

#include "common.h"
#include "peripherals.h"
#include "targets.h"

uint32_t dma_buffer[64] = { 0 };
char ic_timer_prescaler = CPU_FREQUENCY_MHZ / 6;
uint8_t buffer_padding = 7;
volatile char out_put = 0;

// DataWire descriptor, 1D: ctl, src, dst, x_ctl, next (PDL cy_stc_dma_descriptor_t)
typedef struct {
    uint32_t ctl;
    uint32_t src;
    uint32_t dst;
    uint32_t x_ctl;
    uint32_t next;
} dw_descriptor_t;

#define DW_CTL_RETRIG_4CYC      (0UL << 0)
#define DW_CTL_INTR_DESCR       (2UL << 2)  // interrupt when the descriptor is done
#define DW_CTL_TR_IN_ELEMENT    (0UL << 6)  // one element per trigger
#define DW_CTL_CH_DISABLE       (1UL << 24) // disable the channel after the descriptor
#define DW_CTL_DATA_WORD        (2UL << 28)
#define DW_CTL_TYPE_1D          (1UL << 30)
#define DW_X_SRC_INCR(n)        ((uint32_t)(n) << 0)
#define DW_X_DST_INCR(n)        ((uint32_t)(n) << 12)
#define DW_X_COUNT(n)           ((uint32_t)((n) - 1U) << 24)

#define TCPWM_MODE_CAPTURE      2UL
#define TCPWM_MODE_PWM          4UL
#define TCPWM_TR_ONE_CNT_IN1    3UL     // TR_IN_SEL value of one-to-one trigger 1
#define TCPWM_EDGE_ANY          2UL
#define TCPWM_EDGE_NONE         3UL
#define TCPWM_OUT_TC            2UL
#define TCPWM_OUT_CC0_MATCH     3UL

#define TRIG_GROUP_DW0          0U      // PERI trigger group 0 -> cpuss.dw0_tr_in[]
#define TRIG_IN_TCPWM516_OUT0   0x15UL  // tcpwm[0].tr_out0[516]
#define TRIG_GROUP_TCPWM_ONE    11U     // PERI trigger group 11 -> tcpwm one-to-one inputs
#define TRIG_OUT_516_ONE1       0x10U   // tcpwm[0].tr_one_cnt_in[1549], counter 516 input 1
#define TRIG_IN_IO_INPUT(n)     (0x01UL + (n))  // ioss.peri_tr_io_input_in[n]

// one GCR bit of the telemetry answer per period: 750 kbit/s for DShot600
// (output_timer_prescaler 1), 375 kbit/s for DShot300 (prescaler 3)
#define DSHOT_OUT_BIT_CLOCKS    120UL

#define LINE_LEVEL(l)   (((l) << TCPWM_GRP_CNT_LINE_SEL_OUT_SEL_Pos) | ((l) << TCPWM_GRP_CNT_LINE_SEL_COMPL_OUT_SEL_Pos))

static dw_descriptor_t dw_descriptor;
static uint32_t line_levels[37];
static uint32_t input_drive_mode = GPIO_DM_PULLUP;

static DW_CH_STRUCT_Type* dmaChannel(void)
{
    return &DW0->CH_STRUCT[INPUT_DMA_CHANNEL];
}

static void stopDma(void)
{
    dmaChannel()->CH_CTL &= ~DW_CH_STRUCT_CH_CTL_ENABLED_Msk;
    dmaChannel()->INTR = DW_CH_STRUCT_INTR_CH_Msk;
}

static void startDma(void)
{
    DW_CH_STRUCT_Type* ch = dmaChannel();
    ch->CH_CURR_PTR = (uint32_t)&dw_descriptor;
    ch->CH_IDX = 0;
    ch->INTR = DW_CH_STRUCT_INTR_CH_Msk;
    ch->INTR_MASK = DW_CH_STRUCT_INTR_MASK_CH_Msk;
    ch->CH_CTL = DW_CH_STRUCT_CH_CTL_P_Msk | DW_CH_STRUCT_CH_CTL_ENABLED_Msk;   // secure, privileged
}

// routes and the DataWire block, once
void initInput(void)
{
    DW0->CTL = DW_CTL_ENABLED_Msk;
    PERI->TR_GR[TRIG_GROUP_DW0].TR_CTL[INPUT_DMA_CHANNEL] = TRIG_IN_TCPWM516_OUT0 | PERI_TR_GR_TR_CTL_TR_EDGE_Msk;
    PERI->TR_GR[TRIG_GROUP_TCPWM_ONE].TR_CTL[TRIG_OUT_516_ONE1] = TRIG_IN_IO_INPUT(INPUT_PIN_TR_IO_INPUT);

    NVIC_ClearPendingIRQ(IC_DMA_IRQ_NAME);
    NVIC_EnableIRQ(IC_DMA_IRQ_NAME);
    NVIC_ClearPendingIRQ(DSHOT_SW_IRQ);
    NVIC_EnableIRQ(DSHOT_SW_IRQ);

    changeToInput();
}

void changeToInput(void)
{
    gpioSetMode(INPUT_PIN_PORT, INPUT_PIN, input_drive_mode, HSIOM_SEL_TR_IO_INPUT);
}

void changeToOutput(void)
{
    // idle high, then hand the pin to line[516]
    IC_TIMER_REGISTER->LINE_SEL = LINE_LEVEL(TCPWM_LINE_H);
    gpioWrite(INPUT_PIN_PORT, INPUT_PIN, 1);
    gpioSetMode(INPUT_PIN_PORT, INPUT_PIN, GPIO_DM_STRONG, INPUT_PIN_HSIOM_SEL);
}

void receiveDshotDma(void)
{
    TCPWM_GRP_CNT_Type* cnt = IC_TIMER_REGISTER;

    stopDma();
    cnt->CTRL = 0;
    out_put = 0;
    changeToInput();
    setPclkDivider(PCLK_DIV_INPUT, (uint32_t)(uint8_t)ic_timer_prescaler + 1UL);

    cnt->CTRL = TCPWM_MODE_CAPTURE << TCPWM_GRP_CNT_CTRL_MODE_Pos;
    cnt->PERIOD = 0xFFFF;
    cnt->COUNTER = 0;
    cnt->TR_IN_SEL0 = (TCPWM_TR_ONE_CNT_IN1 << TCPWM_GRP_CNT_TR_IN_SEL0_CAPTURE0_SEL_Pos)
        | (1UL << TCPWM_GRP_CNT_TR_IN_SEL0_COUNT_SEL_Pos);
    cnt->TR_IN_SEL1 = 0;
    cnt->TR_IN_EDGE_SEL = (TCPWM_EDGE_ANY << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_CAPTURE0_EDGE_Pos)
        | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_COUNT_EDGE_Pos)
        | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_RELOAD_EDGE_Pos)
        | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_STOP_EDGE_Pos)
        | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_START_EDGE_Pos)
        | (TCPWM_EDGE_NONE << TCPWM_GRP_CNT_TR_IN_EDGE_SEL_CAPTURE1_EDGE_Pos);
    cnt->TR_OUT_SEL = (TCPWM_OUT_CC0_MATCH << TCPWM_GRP_CNT_TR_OUT_SEL_OUT0_Pos)
        | (7UL << TCPWM_GRP_CNT_TR_OUT_SEL_OUT1_Pos);
    cnt->INTR_MASK = 0;

    dw_descriptor.ctl = DW_CTL_RETRIG_4CYC | DW_CTL_INTR_DESCR | DW_CTL_TR_IN_ELEMENT | DW_CTL_CH_DISABLE
        | DW_CTL_DATA_WORD | DW_CTL_TYPE_1D;
    dw_descriptor.src = (uint32_t)&cnt->CC0;
    dw_descriptor.dst = (uint32_t)dma_buffer;
    dw_descriptor.x_ctl = DW_X_SRC_INCR(0) | DW_X_DST_INCR(1) | DW_X_COUNT(buffersize);
    dw_descriptor.next = 0;
    startDma();

    cnt->CTRL |= TCPWM_GRP_CNT_CTRL_ENABLED_Msk;
    cnt->TR_CMD = TCPWM_CMD_START;
}

void sendDshotDma(void)
{
    TCPWM_GRP_CNT_Type* cnt = IC_TIMER_REGISTER;
    const uint32_t bits = 23U + buffer_padding;

    stopDma();
    cnt->CTRL = 0;
    out_put = 1;
    for (uint32_t i = 0; i < bits; i++) {
        line_levels[i] = LINE_LEVEL(gcr[i] ? TCPWM_LINE_L : TCPWM_LINE_H);
    }
    setPclkDivider(PCLK_DIV_INPUT, (uint32_t)(uint8_t)output_timer_prescaler + 1UL);

    cnt->CTRL = (TCPWM_MODE_PWM << TCPWM_GRP_CNT_CTRL_MODE_Pos)
        | (1UL << TCPWM_GRP_CNT_CTRL_PWM_DISABLE_MODE_Pos);    // keep LINE_SEL while stopped
    cnt->PERIOD = DSHOT_OUT_BIT_CLOCKS - 1UL;
    cnt->COUNTER = 0;
    cnt->TR_IN_SEL0 = 1UL << TCPWM_GRP_CNT_TR_IN_SEL0_COUNT_SEL_Pos;
    cnt->TR_IN_SEL1 = 0;
    cnt->TR_OUT_SEL = (TCPWM_OUT_TC << TCPWM_GRP_CNT_TR_OUT_SEL_OUT0_Pos)
        | (7UL << TCPWM_GRP_CNT_TR_OUT_SEL_OUT1_Pos);
    changeToOutput();

    dw_descriptor.ctl = DW_CTL_RETRIG_4CYC | DW_CTL_INTR_DESCR | DW_CTL_TR_IN_ELEMENT | DW_CTL_CH_DISABLE
        | DW_CTL_DATA_WORD | DW_CTL_TYPE_1D;
    dw_descriptor.src = (uint32_t)line_levels;
    dw_descriptor.dst = (uint32_t)&cnt->LINE_SEL;
    dw_descriptor.x_ctl = DW_X_SRC_INCR(1) | DW_X_DST_INCR(0) | DW_X_COUNT(bits);
    dw_descriptor.next = 0;
    startDma();

    cnt->CTRL |= TCPWM_GRP_CNT_CTRL_ENABLED_Msk;
    cnt->TR_CMD = TCPWM_CMD_START;
}

uint8_t getInputPinState(void)
{
    return (uint8_t)gpioRead(INPUT_PIN_PORT, INPUT_PIN);
}

// the counter captures both edges; the shared code does not need a polarity
void setInputPolarityRising(void)
{
}

void enableHalfTransferInt(void)
{
}

static void setInputDriveMode(uint32_t mode)
{
    input_drive_mode = mode;
    if (!out_put) {
        changeToInput();
    }
}

void setInputPullDown(void)
{
    setInputDriveMode(GPIO_DM_PULLDOWN);
}

void setInputPullUp(void)
{
    setInputDriveMode(GPIO_DM_PULLUP);
}

void setInputPullNone(void)
{
    setInputDriveMode(GPIO_DM_HIGHZ);
}
