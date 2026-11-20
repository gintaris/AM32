/*
  sys_can_psc3.c - MCU specific CAN code for the Infineon PSOC Control C3

  CANFD0 is a Bosch M_TTCAN (the M_CAN family of the STM32G4 FDCAN, see
  sys_can_stm32_CANFD.c), here at 1 Mbit/s for DroneCAN: classic CAN, or
  with CANARD_ENABLE_CANFD and sys_can_set_fd() CAN FD with bit rate switch
  (data phase 1, 2, 4 or 8 Mbit/s, transmitter delay compensation from
  2 Mbit/s). Register level through the device headers, no PDL code.

  Target settings (Inc/targets.h, EVK_PSC3_CAN):
    CAN_CHANNEL               M_TTCAN channel (0 or 1)
    CAN_RX_PORT/PIN, CAN_TX_PORT/PIN, CAN_HSIOM_SEL
    CAN_PCLK                  PCLK_CANFD0_CLOCK_CAN_ENx of the channel
    CAN_IRQ, CAN_IRQ_HANDLER  interrupt line 0 of the channel
    CAN_STB_PORT/PIN          optional: transceiver standby, driven low

  Message RAM (4 kB at CANFD0 + 0x10000, shared by both channels, element
  offsets in bytes from its start): no ID filters, everything that is not
  remote goes to Rx FIFO 0 (32 elements), Tx FIFO of 16 elements; 64 byte
  data fields (CAN FD), so 18 words per element, 3456 bytes in all.

  The CAN clock comes from a 16.5 divider of the SCB PCLK group
  (PSC3_SCB_CLOCK_HZ, 48 MHz on the EVK). Nominal bit timing: prescaler to
  16 MHz, 16 tq per bit (sync 1 + seg1 13 + seg2 2), sample point 87.5 %.
  The data phase uses the same 48 MHz, so 5 Mbit/s is not possible.
 */

#include "targets.h"

#if DRONECAN_SUPPORT && defined(MCU_PSC3)

#include "sys_can.h"
#include "functions.h"
#include "peripherals.h"
#include "psc3_timing.h"
#include <string.h>

#define CAN_BITRATE         1000000UL
#define CAN_TQ_PER_BIT      16UL
#define CAN_TSEG1           13UL    // prop + phase 1, tq
#define CAN_TSEG2           2UL
#define CAN_SJW             2UL
#define CAN_PRESCALER       (PSC3_SCB_CLOCK_HZ / (CAN_BITRATE * CAN_TQ_PER_BIT))

#if (CAN_PRESCALER * CAN_BITRATE * CAN_TQ_PER_BIT) != PSC3_SCB_CLOCK_HZ
#error "PSC3_SCB_CLOCK_HZ is not a multiple of 16 MHz, CAN bit timing would be off"
#endif

#define CAN_PCLK_GROUP      ((uint32_t)CAN_PCLK >> 8)
#define CAN_PCLK_DIV        0U      // 16.5 divider 0 of the SCB group (free)

#define MRAM_BASE           ((uintptr_t)CANFD0 + 0x10000UL)
#define MRAM_BYTES          4096UL
#define DATA_BYTES          64UL    // CAN FD data field
#define ELEMENT_BYTES       (8UL + DATA_BYTES)  // 2 header words + data
#define RX_FIFO_ELEMENTS    32UL
#define TX_FIFO_ELEMENTS    16UL
#define RX_FIFO_OFFSET      0UL
#define TX_FIFO_OFFSET      (RX_FIFO_OFFSET + RX_FIFO_ELEMENTS * ELEMENT_BYTES)

#define ID_XTD              (1UL << 30)
#define ID_RTR              (1UL << 29)
#define MASK_STD_ID         0x7FFUL
#define MASK_EXT_ID         0x1FFFFFFFUL
#define DLC_POS             16U
#define ELEM_BRS            (1UL << 20)
#define ELEM_FDF            (1UL << 21)
#define F_DS_64             7UL     // RXESC / TXESC data field size code: 64 bytes

#if (TX_FIFO_OFFSET + TX_FIFO_ELEMENTS * ELEMENT_BYTES) > MRAM_BYTES
#error "CAN message RAM sections do not fit"
#endif

#define GFC_REJECT_REMOTE   (CANFD_CH_M_TTCAN_GFC_RRFE_Msk | CANFD_CH_M_TTCAN_GFC_RRFS_Msk)
#define INIT_TIMEOUT        100000U

#define CH()                (&CANFD0->CH[CAN_CHANNEL].M_TTCAN)

struct psc3_can_stats {
    uint32_t rx_overflow;   // Rx FIFO 0 message lost
    uint32_t bus_off;
    uint32_t init_failed;
};
volatile struct psc3_can_stats can_stats;

// CAN FD data phase in Mbit/s, 0: classic CAN
static uint8_t fd_mbit;

/*
  data phase bit timing from the 48 MHz CAN clock: prescaler, tq of seg1 and
  seg2 (sync is 1 tq), sample point 75 % (66.7 % at 8 Mbit/s), SJW = seg2
*/
struct data_timing {
    uint8_t mbit, brp, tseg1, tseg2;
};
static const struct data_timing data_timings[] = {
    { 1, 3, 11, 4 },    // 16 MHz, 16 tq
    { 2, 2, 8, 3 },     // 24 MHz, 12 tq
    { 4, 1, 8, 3 },     // 48 MHz, 12 tq
    { 8, 1, 3, 2 },     // 48 MHz, 6 tq
};
#if PSC3_SCB_CLOCK_HZ != 48000000UL
#error "the CAN FD data timings are for a 48 MHz CAN clock"
#endif

static const struct data_timing* findDataTiming(uint8_t mbit)
{
    for (uint32_t i = 0; i < sizeof(data_timings) / sizeof(data_timings[0]); i++) {
        if (data_timings[i].mbit == mbit) {
            return &data_timings[i];
        }
    }
    return NULL;
}

static const uint8_t dlc_bytes[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };

static uint32_t lengthToDlc(uint32_t len)
{
    uint32_t dlc = 0;
    while (dlc < 15U && dlc_bytes[dlc] < len) {
        dlc++;
    }
    return dlc;
}

static volatile uint32_t* element(uint32_t offset, uint32_t index)
{
    return (volatile uint32_t*)(MRAM_BASE + offset + index * ELEMENT_BYTES);
}

static bool waitBit(volatile const uint32_t* reg, uint32_t mask, bool set)
{
    for (uint32_t i = 0; i < INIT_TIMEOUT; i++) {
        if (((*reg & mask) != 0U) == set) {
            return true;
        }
    }
    return false;
}

static bool can_send(const CanardCANFrame* frame)
{
    CANFD_CH_M_TTCAN_Type* can = CH();
    const uint32_t fqs = can->TXFQS;

    if (fqs & CANFD_CH_M_TTCAN_TXFQS_TFQF_Msk) {
        return false;
    }
    const uint32_t put = (fqs & CANFD_CH_M_TTCAN_TXFQS_TFQPI_Msk) >> CANFD_CH_M_TTCAN_TXFQS_TFQPI_Pos;
    volatile uint32_t* e = element(TX_FIFO_OFFSET, put);

    uint32_t id;
    if (frame->id & CANARD_CAN_FRAME_EFF) {
        id = (frame->id & MASK_EXT_ID) | ID_XTD;
    } else {
        id = (frame->id & MASK_STD_ID) << 18;
    }
    if (frame->id & CANARD_CAN_FRAME_RTR) {
        id |= ID_RTR;
    }
    uint32_t flags = 0;
    uint32_t max_len = 8U;
#if CANARD_ENABLE_CANFD
    if (frame->canfd && fd_mbit != 0U) {
        flags = ELEM_FDF | (fd_mbit > 1U ? ELEM_BRS : 0U);
        max_len = DATA_BYTES;
    }
#endif
    const uint32_t len = frame->data_len > max_len ? max_len : frame->data_len;
    const uint32_t dlc = lengthToDlc(len);
    uint32_t data[DATA_BYTES / 4U];
    memset(data, 0, sizeof(data));     // padding up to the DLC length
    memcpy(data, frame->data, len);

    e[0] = id;
    e[1] = (dlc << DLC_POS) | flags;   // no event FIFO
    for (uint32_t w = 0; w < (dlc_bytes[dlc] + 3U) / 4U; w++) {
        e[2U + w] = data[w];
    }
    can->TXBAR = 1UL << put;
    return true;
}

static void receiveFifo0(void)
{
    CANFD_CH_M_TTCAN_Type* can = CH();

    while (can->RXF0S & CANFD_CH_M_TTCAN_RXF0S_F0FL_Msk) {
        const uint32_t get = (can->RXF0S & CANFD_CH_M_TTCAN_RXF0S_F0GI_Msk) >> CANFD_CH_M_TTCAN_RXF0S_F0GI_Pos;
        volatile const uint32_t* e = element(RX_FIFO_OFFSET, get);
        CanardCANFrame frame;
        memset(&frame, 0, sizeof(frame));

        const uint32_t id = e[0];
        if (id & ID_XTD) {
            frame.id = (id & MASK_EXT_ID) | CANARD_CAN_FRAME_EFF;
        } else {
            frame.id = (id >> 18) & MASK_STD_ID;
        }
        if (id & ID_RTR) {
            frame.id |= CANARD_CAN_FRAME_RTR;
        }
        uint32_t len = dlc_bytes[(e[1] >> DLC_POS) & 0xFU];
#if CANARD_ENABLE_CANFD
        frame.canfd = (e[1] & ELEM_FDF) != 0U;
#endif
        if (len > sizeof(frame.data)) {
            len = sizeof(frame.data);   // without CANARD_ENABLE_CANFD: 8 bytes
        }
        frame.data_len = (uint8_t)len;
        uint32_t data[DATA_BYTES / 4U] = { 0 };
        for (uint32_t w = 0; w < (len + 3U) / 4U; w++) {
            data[w] = e[2U + w];
        }
        memcpy(frame.data, data, len);

        can->RXF0A = get;
        DroneCAN_handleFrame(&frame);
    }
}

/*
  interrupt line 0 of the channel: received frames, lost frames, bus off
*/
void CAN_IRQ_HANDLER(void)
{
    CANFD_CH_M_TTCAN_Type* can = CH();
    const uint32_t ir = can->IR;
    can->IR = ir;   // write 1 to clear

    if (ir & CANFD_CH_M_TTCAN_IR_RF0L__Msk) {
        can_stats.rx_overflow++;
    }
    if (ir & (CANFD_CH_M_TTCAN_IR_RF0N_Msk | CANFD_CH_M_TTCAN_IR_RF0F_Msk | CANFD_CH_M_TTCAN_IR_RF0L__Msk)) {
        receiveFifo0();
    }
    if (ir & CANFD_CH_M_TTCAN_IR_BO__Msk) {
        // bus off sets INIT; clearing it starts the recovery (128 x 11
        // recessive bits) done by the controller
        can_stats.bus_off++;
        can->CCCR &= ~CANFD_CH_M_TTCAN_CCCR_INIT_Msk;
    }
    canstats.esr = can->PSR;
}

/*
  16 byte unique ID: the SFLASH die ID (lot, wafer, X, Y, sort, revision,
  date), then "PSC3"
*/
void sys_can_getUniqueID(uint8_t id[16])
{
    const SFLASH_Type* sf = PSC3_SFLASH;
    memset(id, 0, 16);
    id[0] = sf->DIE_LOT[0];
    id[1] = sf->DIE_LOT[1];
    id[2] = sf->DIE_LOT[2];
    id[3] = sf->DIE_WAFER;
    id[4] = sf->DIE_X;
    id[5] = sf->DIE_Y;
    id[6] = sf->DIE_SORT;
    id[7] = sf->DIE_MINOR;
    id[8] = sf->DIE_DAY;
    id[9] = sf->DIE_MONTH;
    id[10] = sf->DIE_YEAR;
    memcpy(&id[12], "PSC3", 4);
}

int16_t sys_can_transmit(const CanardCANFrame* txf)
{
    return can_send(txf) ? 1 : 0;
}

// receive is interrupt driven
int16_t sys_can_receive(CanardCANFrame* rx_frame)
{
    (void)rx_frame;
    return -1;
}

void sys_can_disable_IRQ(void)
{
    NVIC_DisableIRQ(CAN_IRQ);
}

void sys_can_enable_IRQ(void)
{
    NVIC_EnableIRQ(CAN_IRQ);
}

static void can_init(void)
{
    CANFD_CH_M_TTCAN_Type* can = CH();

    enablePeriSlave(CY_MMIO_CANFD0_GROUP_NR, CY_MMIO_CANFD0_SLAVE_NR);
    pclkDivider16_5(CAN_PCLK_GROUP, CAN_PCLK_DIV, 1U);
    connectPclk16_5(CAN_PCLK, CAN_PCLK_DIV);

    // message RAM on, channel out of its stop request (Cy_CANFD_Init)
    CANFD0->CTL &= ~(CANFD_CTL_MRAM_OFF_Msk | (1UL << (CANFD_CTL_STOP_REQ_Pos + CAN_CHANNEL)));
    if (!waitBit(&CANFD0->STATUS, 1UL << (CANFD_STATUS_STOP_ACK_Pos + CAN_CHANNEL), false)) {
        can_stats.init_failed++;
        return;
    }

    can->CCCR |= CANFD_CH_M_TTCAN_CCCR_INIT_Msk;
    if (!waitBit(&can->CCCR, CANFD_CH_M_TTCAN_CCCR_INIT_Msk, true)) {
        can_stats.init_failed++;
        return;
    }
    can->CCCR |= CANFD_CH_M_TTCAN_CCCR_CCE_Msk;
    // classic CAN unless CAN FD was selected; retransmission on
    can->CCCR &= ~(CANFD_CH_M_TTCAN_CCCR_FDOE_Msk | CANFD_CH_M_TTCAN_CCCR_BRSE_Msk
        | CANFD_CH_M_TTCAN_CCCR_DAR_Msk | CANFD_CH_M_TTCAN_CCCR_ASM_Msk
        | CANFD_CH_M_TTCAN_CCCR_MON__Msk | CANFD_CH_M_TTCAN_CCCR_TEST_Msk);
    const struct data_timing* dt = findDataTiming(fd_mbit);
    if (dt != NULL) {
        can->CCCR |= CANFD_CH_M_TTCAN_CCCR_FDOE_Msk | (fd_mbit > 1U ? CANFD_CH_M_TTCAN_CCCR_BRSE_Msk : 0U);
        const bool tdc = fd_mbit >= 2U;
        can->DBTP = ((uint32_t)(dt->tseg2 - 1U) << CANFD_CH_M_TTCAN_DBTP_DSJW_Pos)
            | ((uint32_t)(dt->tseg2 - 1U) << CANFD_CH_M_TTCAN_DBTP_DTSEG2_Pos)
            | ((uint32_t)(dt->tseg1 - 1U) << CANFD_CH_M_TTCAN_DBTP_DTSEG1_Pos)
            | ((uint32_t)(dt->brp - 1U) << CANFD_CH_M_TTCAN_DBTP_DBRP_Pos)
            | (tdc ? CANFD_CH_M_TTCAN_DBTP_TDC_Msk : 0U);
        // secondary sample point at the data sample point, counted in CAN
        // clocks after the measured transmitter delay; no filter window
        can->TDCR = tdc ? ((uint32_t)dt->brp * (1U + dt->tseg1)) << CANFD_CH_M_TTCAN_TDCR_TDCO_Pos : 0U;
    }

    can->NBTP = ((CAN_SJW - 1UL) << CANFD_CH_M_TTCAN_NBTP_NSJW_Pos)
        | ((CAN_PRESCALER - 1UL) << CANFD_CH_M_TTCAN_NBTP_NBRP_Pos)
        | ((CAN_TSEG1 - 1UL) << CANFD_CH_M_TTCAN_NBTP_NTSEG1_Pos)
        | ((CAN_TSEG2 - 1UL) << CANFD_CH_M_TTCAN_NBTP_NTSEG2_Pos);

    // message RAM of this channel cleared, then the sections
    for (uint32_t ofs = 0; ofs < TX_FIFO_OFFSET + TX_FIFO_ELEMENTS * ELEMENT_BYTES; ofs += 4U) {
        *(volatile uint32_t*)(MRAM_BASE + ofs) = 0;
    }
    can->GFC = GFC_REJECT_REMOTE;   // ANFS = ANFE = 0: everything else to Rx FIFO 0
    can->SIDFC = 0;
    can->XIDFC = 0;
    can->XIDAM = MASK_EXT_ID;
    can->RXF0C = (RX_FIFO_OFFSET & CANFD_CH_M_TTCAN_RXF0C_F0SA_Msk)
        | (RX_FIFO_ELEMENTS << CANFD_CH_M_TTCAN_RXF0C_F0S_Pos);    // blocking mode
    can->RXF1C = 0;
    can->RXBC = 0;
    can->RXESC = F_DS_64 << CANFD_CH_M_TTCAN_RXESC_F0DS_Pos;  // 64 byte data fields
    can->TXESC = F_DS_64 << CANFD_CH_M_TTCAN_TXESC_TBDS_Pos;
    can->TXEFC = 0;
    can->TXBC = (TX_FIFO_OFFSET & CANFD_CH_M_TTCAN_TXBC_TBSA_Msk)
        | (TX_FIFO_ELEMENTS << CANFD_CH_M_TTCAN_TXBC_TFQS_Pos);    // FIFO, no dedicated buffers

    can->IR = 0x3FFFFFFFUL;
    can->IE = CANFD_CH_M_TTCAN_IE_RF0NE_Msk | CANFD_CH_M_TTCAN_IE_RF0FE_Msk
        | CANFD_CH_M_TTCAN_IE_RF0LE_Msk | CANFD_CH_M_TTCAN_IE_BOE_Msk;
    can->ILS = 0;                   // all on line 0
    can->ILE = CANFD_CH_M_TTCAN_ILE_EINT0_Msk;

    can->CCCR &= ~CANFD_CH_M_TTCAN_CCCR_INIT_Msk;
    if (!waitBit(&can->CCCR, CANFD_CH_M_TTCAN_CCCR_INIT_Msk, false)) {
        can_stats.init_failed++;
    }
}

#if CANARD_ENABLE_CANFD
bool sys_can_set_fd(uint8_t data_mbit)
{
    if (findDataTiming(data_mbit) == NULL) {
        fd_mbit = 0;
        return false;
    }
    fd_mbit = data_mbit;
    return true;
}
#endif

void sys_can_init(void)
{
#ifdef CAN_STB_PORT
    // transceiver out of standby (TLE9251V: STB high = standby)
    gpioWrite(CAN_STB_PORT, CAN_STB_PIN, 0);
    gpioSetMode(CAN_STB_PORT, CAN_STB_PIN, GPIO_DM_STRONG, HSIOM_SEL_GPIO);
#endif
    gpioWrite(CAN_TX_PORT, CAN_TX_PIN, 1);     // recessive until the controller drives it
    gpioSetMode(CAN_TX_PORT, CAN_TX_PIN, GPIO_DM_STRONG, CAN_HSIOM_SEL);
    gpioSetMode(CAN_RX_PORT, CAN_RX_PIN, GPIO_DM_HIGHZ, CAN_HSIOM_SEL);

    can_init();

    // below the motor control interrupts, like the ADC
    NVIC_SetPriority(CAN_IRQ, 3);
    NVIC_ClearPendingIRQ(CAN_IRQ);
    NVIC_EnableIRQ(CAN_IRQ);
}

/*
  static output pin, GPIO_PORT_PIN(port, pin) = port << 8 | pin: used by
  DroneCAN.c for CAN_TERM_PIN (switchable bus termination, CAN_TERM_ENABLE)
*/
void setup_portpin(uint16_t portpin, bool enable)
{
    const uint32_t port = (uint32_t)portpin >> 8;
    const uint32_t pin = (uint32_t)portpin & 0xFFU;
    gpioWrite(port, pin, enable ? 1U : 0U);
    gpioSetMode(port, pin, GPIO_DM_STRONG, HSIOM_SEL_GPIO);
}

/*
  The AM32 bootloader for the PSC3 does not exist yet, so the registers that
  carry the firmware update handshake to it only need to survive in RAM for
  now (DroneCAN.c reboots right after setting the update request, which
  then has no effect)
*/
static uint32_t backup_registers[4];

uint32_t get_rtc_backup_register(uint8_t idx)
{
    return idx < 4U ? backup_registers[idx] : 0U;
}

void set_rtc_backup_register(uint8_t idx, uint32_t value)
{
    if (idx < 4U) {
        backup_registers[idx] = value;
    }
}

#endif // DRONECAN_SUPPORT && defined(MCU_PSC3)
