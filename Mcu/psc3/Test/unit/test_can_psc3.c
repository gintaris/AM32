/*
 * Host test for the PSC3 CAN driver of DroneCAN (Src/DroneCAN/sys_can_psc3.c)
 * against the real M_TTCAN register layout over plain memory: init values
 * (bit timing for 1 Mbit/s from the 48 MHz SCB group clock, message RAM
 * sections, interrupts, channel stop request, transceiver standby pin),
 * the Tx FIFO element encoding, Rx FIFO decoding with acknowledge, bus off
 * recovery, the unique ID and CAN FD (bit rate switch, data phase timing,
 * transmitter delay compensation, DLC coding of 12..64 byte frames).
 */

#include <string.h>

#include "peripherals.h"
#include "sys_can.h"
#include "unit.h"

uint32_t fake_canfd[(0x10000 + 4096) / 4];
SFLASH_Type fake_sflash;
struct CANStats canstats;

static struct {
    uint32_t stb_level, stb_mode;
    uint32_t tx_hsiom, rx_hsiom;
    uint32_t peri_group, peri_slave;
    uint32_t div_group, div_index, div_value, clk_dst, clk_div;
    int irq_enabled, irq_prio;
    uint32_t term_level, term_mode, term_hsiom;
} hw;

void gpioWrite(uint32_t port, uint32_t pin, uint32_t level)
{
    if (port == 7 && pin == 6) {
        hw.stb_level = level;
    }
    if (port == 2 && pin == 3) {
        hw.term_level = level;
    }
}

void gpioSetMode(uint32_t port, uint32_t pin, uint32_t drive, uint32_t hsiom)
{
    if (port == 7 && pin == 6) {
        hw.stb_mode = drive;
    }
    if (port == 2 && pin == 3) {
        hw.term_mode = drive;
        hw.term_hsiom = hsiom;
    }
    if (port == 5 && pin == 3) {
        hw.tx_hsiom = hsiom;
    }
    if (port == 5 && pin == 2) {
        hw.rx_hsiom = hsiom;
    }
}

void enablePeriSlave(uint32_t group, uint32_t slave)
{
    hw.peri_group = group;
    hw.peri_slave = slave;
}

void pclkDivider16_5(uint32_t group, uint32_t div, uint32_t divide_by)
{
    hw.div_group = group;
    hw.div_index = div;
    hw.div_value = divide_by;
}

void connectPclk16_5(en_clk_dst_t dst, uint32_t div)
{
    hw.clk_dst = (uint32_t)dst;
    hw.clk_div = div;
}

void NVIC_SetPriority(int irq, uint32_t prio)
{
    if (irq == canfd_0_interrupts0_1_IRQn) {
        hw.irq_prio = (int)prio;
    }
}

void NVIC_ClearPendingIRQ(int irq)
{
    (void)irq;
}

void NVIC_EnableIRQ(int irq)
{
    if (irq == canfd_0_interrupts0_1_IRQn) {
        hw.irq_enabled = 1;
    }
}

void NVIC_DisableIRQ(int irq)
{
    if (irq == canfd_0_interrupts0_1_IRQn) {
        hw.irq_enabled = 0;
    }
}

void canfd_0_interrupts0_1_IRQHandler(void);

// ---------------------------------------------------------------- received frames
static CanardCANFrame got[8];
static int got_count;
static uint32_t rx_level, rx_get;  // fake Rx FIFO 0 state behind RXF0S

static CANFD_CH_M_TTCAN_Type* ch1(void)
{
    return &CANFD0->CH[1].M_TTCAN;
}

static void setRxStatus(void)
{
    *(volatile uint32_t*)&ch1()->RXF0S = (rx_level << CANFD_CH_M_TTCAN_RXF0S_F0FL_Pos)
        | (rx_get << CANFD_CH_M_TTCAN_RXF0S_F0GI_Pos);
}

void DroneCAN_handleFrame(const CanardCANFrame* rx_frame)
{
    CHECK(ch1()->RXF0A == rx_get, "frame acknowledged with its get index before it is handled");
    if (got_count < 8) {
        got[got_count++] = *rx_frame;
    }
    // the controller moves on after the acknowledge
    rx_level--;
    rx_get = (rx_get + 1U) % 32U;
    setRxStatus();
}

void DroneCAN_processTxQueue(void)
{
}

void DroneCAN_receiveFrame(void)
{
}

#define ELEM 72U    // message RAM element: 2 header words + 64 data bytes

static volatile uint32_t* mram(uint32_t byte_offset)
{
    return (volatile uint32_t*)((uint8_t*)fake_canfd + 0x10000 + byte_offset);
}

// ---------------------------------------------------------------- tests
static void testInit(void)
{
    memset(fake_canfd, 0xA5, sizeof(fake_canfd));      // message RAM garbage after reset
    CANFD0->CTL = CANFD_CTL_MRAM_OFF_Msk | 0x3UL;       // both channels stopped, RAM off
    *(volatile uint32_t*)&CANFD0->STATUS = 0;
    ch1()->CCCR = CANFD_CH_M_TTCAN_CCCR_FDOE_Msk | CANFD_CH_M_TTCAN_CCCR_BRSE_Msk;

    sys_can_init();

    CHECK(hw.stb_level == 0 && hw.stb_mode == GPIO_DM_STRONG, "TLE9251V STB driven low (normal mode)");
    CHECK(hw.tx_hsiom == 11 && hw.rx_hsiom == 11, "P5.2 / P5.3 routed to CANFD0 channel 1");
    CHECK(hw.peri_group == 3 && hw.peri_slave == 0, "CANFD0 released in PERI group 3");
    CHECK(hw.div_group == 4 && hw.div_value == 1 && hw.clk_dst == 0x0406 && hw.clk_div == hw.div_index,
        "CAN clock: SCB group 16.5 divider, undivided 48 MHz, to clock_can_en[1]");
    CHECK(CANFD0->CTL == 0x1UL, "MRAM on, only channel 1 out of its stop request (CTL %08X)", CANFD0->CTL);

    const uint32_t nbtp = ch1()->NBTP;
    const uint32_t brp = ((nbtp & CANFD_CH_M_TTCAN_NBTP_NBRP_Msk) >> CANFD_CH_M_TTCAN_NBTP_NBRP_Pos) + 1U;
    const uint32_t seg1 = ((nbtp & CANFD_CH_M_TTCAN_NBTP_NTSEG1_Msk) >> CANFD_CH_M_TTCAN_NBTP_NTSEG1_Pos) + 1U;
    const uint32_t seg2 = ((nbtp & CANFD_CH_M_TTCAN_NBTP_NTSEG2_Msk) >> CANFD_CH_M_TTCAN_NBTP_NTSEG2_Pos) + 1U;
    const uint32_t sjw = ((nbtp & CANFD_CH_M_TTCAN_NBTP_NSJW_Msk) >> CANFD_CH_M_TTCAN_NBTP_NSJW_Pos) + 1U;
    const uint32_t tq = 1U + seg1 + seg2;
    CHECK(48000000UL / (brp * tq) == 1000000UL && 48000000UL % (brp * tq) == 0, "exactly 1 Mbit/s from 48 MHz");
    CHECK((1U + seg1) * 1000U / tq == 875U, "sample point 87.5 %%");
    CHECK(sjw <= seg2, "SJW within phase segment 2");

    const uint32_t cccr = ch1()->CCCR;
    CHECK((cccr & CANFD_CH_M_TTCAN_CCCR_INIT_Msk) == 0, "running (INIT cleared)");
    CHECK((cccr & (CANFD_CH_M_TTCAN_CCCR_FDOE_Msk | CANFD_CH_M_TTCAN_CCCR_BRSE_Msk)) == 0, "classic CAN");
    CHECK((cccr & (CANFD_CH_M_TTCAN_CCCR_DAR_Msk | CANFD_CH_M_TTCAN_CCCR_MON__Msk)) == 0,
        "retransmission on, not bus monitoring");

    const uint32_t rxf0c = ch1()->RXF0C;
    const uint32_t txbc = ch1()->TXBC;
    const uint32_t rx_sa = rxf0c & CANFD_CH_M_TTCAN_RXF0C_F0SA_Msk;
    const uint32_t rx_n = (rxf0c & CANFD_CH_M_TTCAN_RXF0C_F0S_Msk) >> CANFD_CH_M_TTCAN_RXF0C_F0S_Pos;
    const uint32_t tx_sa = txbc & CANFD_CH_M_TTCAN_TXBC_TBSA_Msk;
    const uint32_t tx_n = (txbc & CANFD_CH_M_TTCAN_TXBC_TFQS_Msk) >> CANFD_CH_M_TTCAN_TXBC_TFQS_Pos;
    CHECK(rx_n == 32 && tx_n == 16 && (txbc & CANFD_CH_M_TTCAN_TXBC_NDTB_Msk) == 0
            && (txbc & CANFD_CH_M_TTCAN_TXBC_TFQM_Msk) == 0,
        "Rx FIFO 0 of 32, Tx FIFO of 16, no dedicated Tx buffers");
    CHECK(rx_sa + rx_n * ELEM <= tx_sa && tx_sa + tx_n * ELEM <= 4096U, "sections do not overlap and fit 4 kB");
    CHECK(ch1()->RXESC == 7U && ch1()->TXESC == 7U, "64 byte data fields (CAN FD)");
    CHECK(ch1()->SIDFC == 0 && ch1()->XIDFC == 0, "no ID filters");
    CHECK(ch1()->GFC == (CANFD_CH_M_TTCAN_GFC_RRFE_Msk | CANFD_CH_M_TTCAN_GFC_RRFS_Msk),
        "non-matching frames to FIFO 0, remote frames rejected");
    int clean = 1;
    for (uint32_t ofs = 0; ofs < tx_sa + tx_n * ELEM; ofs += 4U) {
        clean &= *mram(ofs) == 0;
    }
    CHECK(clean, "message RAM of the channel cleared");
    CHECK((ch1()->IE & CANFD_CH_M_TTCAN_IE_RF0NE_Msk) && (ch1()->IE & CANFD_CH_M_TTCAN_IE_BOE_Msk)
            && (ch1()->IE & CANFD_CH_M_TTCAN_IE_RF0LE_Msk) && ch1()->ILS == 0
            && ch1()->ILE == CANFD_CH_M_TTCAN_ILE_EINT0_Msk,
        "new frame, lost frame and bus off on interrupt line 0");
    CHECK(hw.irq_enabled && hw.irq_prio == 3, "NVIC line enabled at the lowest priority");
}

static void testTransmit(void)
{
    const uint32_t tx_sa = ch1()->TXBC & CANFD_CH_M_TTCAN_TXBC_TBSA_Msk;

    *(volatile uint32_t*)&ch1()->TXFQS = 3U << CANFD_CH_M_TTCAN_TXFQS_TFQPI_Pos;
    ch1()->TXBAR = 0;
    CanardCANFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x1234567U | CANARD_CAN_FRAME_EFF;
    for (uint8_t i = 0; i < 8; i++) {
        f.data[i] = (uint8_t)(0x10 + i);
    }
    f.data_len = 8;
    CHECK(sys_can_transmit(&f) == 1, "frame queued");
    volatile uint32_t* e = mram(tx_sa + 3U * ELEM);
    CHECK(e[0] == (0x1234567U | (1U << 30)), "extended ID with XTD in T0 (%08X)", e[0]);
    CHECK(e[1] == (8U << 16), "DLC 8, classic, no event (%08X)", e[1]);
    CHECK(e[2] == 0x13121110U && e[3] == 0x17161514U, "data little endian");
    CHECK(ch1()->TXBAR == (1U << 3), "add request for the put index");

    *(volatile uint32_t*)&ch1()->TXFQS = 5U << CANFD_CH_M_TTCAN_TXFQS_TFQPI_Pos;
    f.id = 0x123;
    f.data_len = 2;
    CHECK(sys_can_transmit(&f) == 1, "standard frame queued");
    e = mram(tx_sa + 5U * ELEM);
    CHECK(e[0] == (0x123U << 18) && e[1] == (2U << 16) && e[2] == 0x1110U && e[3] == 0,
        "standard ID in bits 28:18, unused data zero");

    *(volatile uint32_t*)&ch1()->TXFQS = CANFD_CH_M_TTCAN_TXFQS_TFQF_Msk;
    ch1()->TXBAR = 0;
    CHECK(sys_can_transmit(&f) == 0 && ch1()->TXBAR == 0, "full Tx FIFO: 0, nothing requested");
}

static void testReceive(void)
{
    const uint32_t rx_sa = ch1()->RXF0C & CANFD_CH_M_TTCAN_RXF0C_F0SA_Msk;

    // two frames at get index 31 and 0 (wrap)
    volatile uint32_t* e = mram(rx_sa + 31U * ELEM);
    e[0] = 0x0ABCDEFU | (1U << 30);
    e[1] = (5U << 16) | 0x1234U;   // DLC 5, time stamp
    e[2] = 0x44332211U;
    e[3] = 0x000000FFU;
    e = mram(rx_sa + 0U * ELEM);
    e[0] = 0x7FFU << 18;
    e[1] = 1U << 16;
    e[2] = 0xEEU;
    e[3] = 0;
    rx_level = 2;
    rx_get = 31;
    setRxStatus();
    got_count = 0;
    ch1()->IR = CANFD_CH_M_TTCAN_IR_RF0N_Msk;

    canfd_0_interrupts0_1_IRQHandler();

    CHECK(got_count == 2, "both frames handled in one interrupt (%d)", got_count);
    CHECK(got[0].id == (0x0ABCDEFU | CANARD_CAN_FRAME_EFF) && got[0].data_len == 5
            && memcmp(got[0].data, "\x11\x22\x33\x44\xFF", 5) == 0,
        "extended frame decoded");
    CHECK(got[1].id == 0x7FFU && got[1].data_len == 1 && got[1].data[0] == 0xEE, "standard frame decoded");
}

static void testBusOff(void)
{
    struct psc3_can_stats {
        uint32_t rx_overflow;
        uint32_t bus_off;
        uint32_t init_failed;
    };
    extern volatile struct psc3_can_stats can_stats;

    rx_level = 0;
    setRxStatus();
    ch1()->CCCR |= CANFD_CH_M_TTCAN_CCCR_INIT_Msk;     // the controller sets INIT at bus off
    ch1()->IR = CANFD_CH_M_TTCAN_IR_BO__Msk | CANFD_CH_M_TTCAN_IR_RF0L__Msk;
    canfd_0_interrupts0_1_IRQHandler();
    CHECK((ch1()->CCCR & CANFD_CH_M_TTCAN_CCCR_INIT_Msk) == 0 && can_stats.bus_off == 1,
        "bus off: INIT cleared to start the recovery");
    CHECK(can_stats.rx_overflow == 1, "lost frame counted");
    CHECK(can_stats.init_failed == 0, "init did not time out");
}

static void testUniqueId(void)
{
    uint8_t id[16];
    fake_sflash.DIE_LOT[0] = 1;
    fake_sflash.DIE_LOT[1] = 2;
    fake_sflash.DIE_LOT[2] = 3;
    fake_sflash.DIE_WAFER = 4;
    fake_sflash.DIE_X = 5;
    fake_sflash.DIE_Y = 6;
    fake_sflash.DIE_YEAR = 26;
    sys_can_getUniqueID(id);
    CHECK(id[0] == 1 && id[2] == 3 && id[3] == 4 && id[5] == 6 && id[10] == 26 && memcmp(&id[12], "PSC3", 4) == 0,
        "unique ID from the die ID");

    set_rtc_backup_register(0, 0x8c42c8);
    CHECK(get_rtc_backup_register(0) == 0x8c42c8 && get_rtc_backup_register(9) == 0, "backup registers in RAM");
}

// CAN_TERM_PIN as P2 defines it: GPIO_PORT_PIN(2, 3), high = termination on
static void testPortPin(void)
{
    hw.term_level = 7;
    setup_portpin((2U << 8) | 3U, true);
    CHECK(hw.term_level == 1 && hw.term_mode == GPIO_DM_STRONG && hw.term_hsiom == HSIOM_SEL_GPIO,
        "setup_portpin: P2.3 strong output, high");
    setup_portpin((2U << 8) | 3U, false);
    CHECK(hw.term_level == 0, "setup_portpin: P2.3 low");
}

static void testCanFd(void)
{
    CHECK(!sys_can_set_fd(5), "5 Mbit/s data phase refused (48 MHz clock)");
    CHECK(sys_can_set_fd(4), "4 Mbit/s data phase accepted");
    sys_can_init();

    const uint32_t cccr = ch1()->CCCR;
    CHECK((cccr & CANFD_CH_M_TTCAN_CCCR_FDOE_Msk) && (cccr & CANFD_CH_M_TTCAN_CCCR_BRSE_Msk)
            && (cccr & CANFD_CH_M_TTCAN_CCCR_INIT_Msk) == 0,
        "CAN FD with bit rate switch, running");
    const uint32_t dbtp = ch1()->DBTP;
    const uint32_t brp = ((dbtp & CANFD_CH_M_TTCAN_DBTP_DBRP_Msk) >> CANFD_CH_M_TTCAN_DBTP_DBRP_Pos) + 1U;
    const uint32_t seg1 = ((dbtp & CANFD_CH_M_TTCAN_DBTP_DTSEG1_Msk) >> CANFD_CH_M_TTCAN_DBTP_DTSEG1_Pos) + 1U;
    const uint32_t seg2 = ((dbtp & CANFD_CH_M_TTCAN_DBTP_DTSEG2_Msk) >> CANFD_CH_M_TTCAN_DBTP_DTSEG2_Pos) + 1U;
    const uint32_t sjw = ((dbtp & CANFD_CH_M_TTCAN_DBTP_DSJW_Msk) >> CANFD_CH_M_TTCAN_DBTP_DSJW_Pos) + 1U;
    const uint32_t tq = 1U + seg1 + seg2;
    CHECK(48000000UL / (brp * tq) == 4000000UL && 48000000UL % (brp * tq) == 0, "data phase exactly 4 Mbit/s");
    CHECK((1U + seg1) * 100U / tq == 75U && sjw <= seg2, "data sample point 75 %%, SJW within seg2");
    const uint32_t tdco = (ch1()->TDCR & CANFD_CH_M_TTCAN_TDCR_TDCO_Msk) >> CANFD_CH_M_TTCAN_TDCR_TDCO_Pos;
    CHECK((dbtp & CANFD_CH_M_TTCAN_DBTP_TDC_Msk) && tdco == brp * (1U + seg1),
        "transmitter delay compensation, offset at the sample point (%u)", tdco);
    const uint32_t nbtp = ch1()->NBTP;
    CHECK(((nbtp & CANFD_CH_M_TTCAN_NBTP_NBRP_Msk) >> CANFD_CH_M_TTCAN_NBTP_NBRP_Pos) == 2U,
        "nominal phase stays 1 Mbit/s");

    // 64 byte FD frame: DLC 15, FDF and BRS
    const uint32_t tx_sa = ch1()->TXBC & CANFD_CH_M_TTCAN_TXBC_TBSA_Msk;
    *(volatile uint32_t*)&ch1()->TXFQS = 2U << CANFD_CH_M_TTCAN_TXFQS_TFQPI_Pos;
    CanardCANFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x1000455U | CANARD_CAN_FRAME_EFF;
    for (uint8_t i = 0; i < 64; i++) {
        f.data[i] = i;
    }
    f.data_len = 64;
    f.canfd = true;
    CHECK(sys_can_transmit(&f) == 1, "FD frame queued");
    volatile uint32_t* e = mram(tx_sa + 2U * ELEM);
    CHECK(e[1] == ((15U << 16) | (1U << 21) | (1U << 20)), "DLC 15, FDF, BRS (%08X)", e[1]);
    CHECK(e[2] == 0x03020100U && e[17] == 0x3F3E3D3CU, "64 data bytes in 16 words");

    // 13 bytes go out as the next FD length, 16, padded with zeros
    for (uint32_t w = 0; w < ELEM / 4U; w++) {
        mram(tx_sa + 3U * ELEM)[w] = 0xA5A5A5A5U;     // old element contents
    }
    *(volatile uint32_t*)&ch1()->TXFQS = 3U << CANFD_CH_M_TTCAN_TXFQS_TFQPI_Pos;
    f.data_len = 13;
    CHECK(sys_can_transmit(&f) == 1, "13 byte FD frame queued");
    e = mram(tx_sa + 3U * ELEM);
    CHECK(((e[1] >> 16) & 0xFU) == 10U && e[5] == 0x0000000CU, "DLC 10 (16 bytes), padding zero (%08X)", e[5]);

    // a classic frame on the FD bus stays classic
    *(volatile uint32_t*)&ch1()->TXFQS = 4U << CANFD_CH_M_TTCAN_TXFQS_TFQPI_Pos;
    f.canfd = false;
    f.data_len = 8;
    CHECK(sys_can_transmit(&f) == 1, "classic frame queued");
    e = mram(tx_sa + 4U * ELEM);
    CHECK(e[1] == (8U << 16), "classic frame: no FDF, no BRS (%08X)", e[1]);

    // received 48 byte FD frame
    const uint32_t rx_sa = ch1()->RXF0C & CANFD_CH_M_TTCAN_RXF0C_F0SA_Msk;
    e = mram(rx_sa + 7U * ELEM);
    e[0] = 0x0155AA7U | (1U << 30);
    e[1] = (14U << 16) | (1U << 21) | (1U << 20);
    for (uint32_t w = 0; w < 12U; w++) {
        e[2U + w] = 0x01010101U * (w + 1U);
    }
    rx_level = 1;
    rx_get = 7;
    setRxStatus();
    got_count = 0;
    ch1()->IR = CANFD_CH_M_TTCAN_IR_RF0N_Msk;
    canfd_0_interrupts0_1_IRQHandler();
    CHECK(got_count == 1 && got[0].canfd && got[0].data_len == 48 && got[0].data[0] == 1 && got[0].data[47] == 12,
        "48 byte FD frame decoded (len %u)", got[0].data_len);

    // back to classic CAN
    CHECK(!sys_can_set_fd(0), "0: classic CAN");
    sys_can_init();
    CHECK((ch1()->CCCR & (CANFD_CH_M_TTCAN_CCCR_FDOE_Msk | CANFD_CH_M_TTCAN_CCCR_BRSE_Msk)) == 0, "classic again");
}

int main(void)
{
    testInit();
    testTransmit();
    testReceive();
    testBusOff();
    testUniqueId();
    testPortPin();
    testCanFd();
    return report("test_can_psc3");
}
