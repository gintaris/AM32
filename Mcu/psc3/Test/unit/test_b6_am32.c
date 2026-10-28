/*
 * Host test for protocol B6 on the AM32 state (Src/b6_am32.c with
 * Src/b6_protocol.c): the main.c globals are plain variables here and the
 * MCU hooks (UART, input capture, faults) are fakes. Checks the rules that
 * involve the rest of the firmware:
 *  - B6 and DShot never drive the motor together: ARM refused while DShot is
 *    armed or its input is not zero, the signal input is off while B6 owns
 *    the throttle and back on after DISARM
 *  - the throttle reaches setInput() in the DShot scale, also repeated at
 *    1 kHz without new frames, and signaltimeout stays 0 while armed
 *  - the link watchdog zeroes the input in the control loop tick, the main
 *    loop then disarms with all phases off
 *  - an over-current latch sends EVENT, disarms and keeps the input off
 *    until a DISARM request finds the OCD pins high again
 *  - telemetry units
 */

#include <string.h>

#include "b6_am32.h"
#include "b6_protocol.h"
#include "common.h"
#include "unit.h"

#define TICK_HZ LOOP_FREQUENCY_HZ
#define MS(n)   ((n) * (TICK_HZ / 1000U))

// ---------------------------------------------------------------- main.c state
EEprom_t eepromBuffer;
uint16_t signaltimeout;
volatile uint16_t newinput;
uint16_t adjusted_input;
uint16_t input;
uint8_t running;
char forward = 1;
uint16_t battery_voltage;
int16_t actual_current;
uint16_t e_rpm;
volatile int16_t degrees_celsius;
volatile char armed;
volatile char inputSet;
char dshot;
volatile char servoPwm;
uint16_t duty_cycle_setpoint;
volatile uint16_t duty_cycle;
uint16_t duty_cycle_maximum = 2000;
int16_t use_current_limit_adjust = 2000;
char use_current_limit;
volatile uint32_t commutation_interval;
volatile uint32_t zero_crosses;
char old_routine;
char stepper_sine;
char prop_brake_active;
volatile uint8_t zero_throttle_brake_active;
char LOW_VOLTAGE_CUTOFF;
char cell_count;
int32_t consumed_current;
uint8_t temp_advance = 16;
uint8_t auto_advance_level;
uint8_t bemf_timeout_happened;
uint8_t desync_happened;
uint32_t fake_com_timer_int_mask;

static int set_inputs, all_offs, masks, tones;

void setInput(void)
{
    set_inputs++;
    // what matters here: AM32 takes newinput as the throttle while armed
    adjusted_input = newinput;
    input = armed ? adjusted_input : 0;
}

void allOff(void)
{
    all_offs++;
}

void maskPhaseInterrupts(void)
{
    masks++;
}

void playDefaultTone(void)
{
    tones++;
}

void NVIC_ClearPendingIRQ(int irq)
{
    (void)irq;
}

void __disable_irq(void)
{
}

void __enable_irq(void)
{
}

// ---------------------------------------------------------------- MCU hooks
static uint8_t txbuf[16384];
static uint32_t tx_len;
static uint8_t rxq[512];
static uint32_t rxq_len, rxq_pos;
static uint8_t capture_on = 1;
static int capture_switches;
static uint8_t fault, ocd_latched, ocd_pins_low, ocd_new;

void b6HwInit(void)
{
}

uint8_t b6HwRead(uint8_t* byte)
{
    if (rxq_pos >= rxq_len) {
        return 0;
    }
    *byte = rxq[rxq_pos++];
    return 1;
}

uint8_t b6HwWrite(const uint8_t* data, uint32_t len)
{
    if (tx_len + len > sizeof(txbuf)) {
        return 0;
    }
    memcpy(txbuf + tx_len, data, len);
    tx_len += len;
    return 1;
}

void b6HwInputCapture(uint8_t on)
{
    capture_on = on;
    capture_switches++;
}

uint32_t b6HwResetCause(void)
{
    return 0x10;    // SOFT
}

uint64_t b6HwMcuId(void)
{
    return 42;
}

uint16_t b6HwPowerStageId(void)
{
    return 1;
}

uint16_t b6HwPowerInputId(void)
{
    return 1;
}

uint16_t b6HwFlags(void)
{
    return (uint16_t)((ocd_latched & 1U) ? B6_FLAG_OCD1 : 0U) | B6_FLAG_DRIVER_ENABLED;
}

uint8_t b6HwFault(void)
{
    return fault || ocd_latched;
}

uint8_t b6HwOcdTripped(void)
{
    const uint8_t b = ocd_new;
    ocd_new = 0;
    return b;
}

void b6HwClearFaults(void)
{
    ocd_latched &= ocd_pins_low;
}

// ---------------------------------------------------------------- helpers
static b6_frame_t rx[512];
static int rx_count;

static void queue(uint8_t seq, uint8_t type, const uint8_t* payload, uint16_t len)
{
    rxq_len += b6Encode(rxq + rxq_len, sizeof(rxq) - rxq_len, seq, type, payload, len);
}

static void throttleReq(uint8_t seq, int16_t permille, uint16_t wd)
{
    const uint8_t p[4] = { (uint8_t)permille, (uint8_t)((uint16_t)permille >> 8), (uint8_t)wd, (uint8_t)(wd >> 8) };
    queue(seq, B6_THROTTLE, p, 4);
}

static void decode(void)
{
    b6_parser_t p;
    b6ParserReset(&p);
    rx_count = 0;
    for (uint32_t i = 0; i < tx_len; i++) {
        b6ParserPush(&p, txbuf[i]);
        while (rx_count < 512 && b6ParserNext(&p, &rx[rx_count])) {
            rx_count++;
        }
    }
    tx_len = 0;
}

// main loop pass: the queued requests and b6Service()
static void poll(void)
{
    b6Am32Poll();
    rxq_len = rxq_pos = 0;
    decode();
}

static void ticks(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        b6Am32Tick();
        signaltimeout++;    // as tenKhzRoutine() does after it
    }
}

static int responseCode(uint8_t seq)
{
    for (int i = 0; i < rx_count; i++) {
        if (rx[i].seq == seq && (rx[i].type == B6_ACK || rx[i].type == B6_NACK)) {
            return rx[i].type == B6_ACK ? 0 : rx[i].payload[1];
        }
    }
    return -1;
}

static int eventCount(uint16_t code)
{
    int n = 0;
    for (int i = 0; i < rx_count; i++) {
        n += rx[i].type == B6_EVENT && (uint16_t)(rx[i].payload[4] | (rx[i].payload[5] << 8)) == code;
    }
    return n;
}

static uint32_t u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void reset(void)
{
    memset(&eepromBuffer, 0, sizeof(eepromBuffer));
    armed = inputSet = dshot = servoPwm = 0;
    newinput = adjusted_input = input = 0;
    running = 0;
    duty_cycle_setpoint = 0;
    signaltimeout = 0;
    fault = ocd_latched = ocd_pins_low = ocd_new = 0;
    capture_on = 1;
    capture_switches = set_inputs = all_offs = masks = tones = 0;
    tx_len = rxq_len = rxq_pos = 0;
    b6Am32Init();
    decode();
}

// ---------------------------------------------------------------- tests
static void testResetEvent(void)
{
    reset();
    b6Am32Init();
    decode();
    CHECK(eventCount(B6_EVT_RESET) == 1, "EVENT reset at start");
    CHECK(rx_count == 1 && u32(rx[0].payload + 6) == 0x10, "reset cause as event data");
}

static void testExclusiveInputs(void)
{
    reset();
    armed = 1;      // armed by DShot
    dshot = 1;
    queue(1, B6_ARM, NULL, 0);
    poll();
    CHECK(responseCode(1) == B6_NACK_BUSY && capture_on, "ARM while DShot is armed: NACK 8, DShot keeps running");
    queue(2, B6_THROTTLE, (const uint8_t[]){ 0xF4, 0x01, 0xC8, 0x00 }, 4);
    poll();
    CHECK(responseCode(2) == B6_NACK_NOT_ARMED && newinput == 0 && set_inputs == 0,
        "B6 THROTTLE while DShot is armed: NACK 4, not applied");

    reset();
    newinput = 300;     // DShot sending throttle, not armed yet
    queue(3, B6_ARM, NULL, 0);
    poll();
    CHECK(responseCode(3) == B6_NACK_INPUT_NOT_ZERO && !armed, "ARM while the DShot input is not zero: NACK 5");

    reset();
    fault = 1;
    queue(4, B6_ARM, NULL, 0);
    poll();
    CHECK(responseCode(4) == B6_NACK_FAULT && !armed, "ARM with a driver fault: NACK 6");

    reset();
    LOW_VOLTAGE_CUTOFF = 1;
    queue(5, B6_ARM, NULL, 0);
    poll();
    CHECK(responseCode(5) == B6_NACK_FAULT && !armed, "ARM after low voltage cut-off: NACK 6");
    LOW_VOLTAGE_CUTOFF = 0;

    reset();
    queue(6, B6_ARM, NULL, 0);
    poll();
    CHECK(responseCode(6) == 0 && armed && inputSet && !capture_on, "ARM: AM32 armed, signal input off");
    CHECK(b6ThrottleActive(), "B6 owns the throttle");
    queue(7, B6_DISARM, NULL, 0);
    poll();
    CHECK(responseCode(7) == 0 && !armed && !inputSet && capture_on, "DISARM: AM32 disarmed, signal input on");
    CHECK(all_offs >= 1 && masks >= 1 && running == 0 && old_routine == 1, "DISARM: phases off, BEMF masked");
}

static void testThrottlePath(void)
{
    reset();
    queue(1, B6_ARM, NULL, 0);
    throttleReq(2, 500, 200);
    poll();
    CHECK(responseCode(2) == 0 && newinput == 1047 && input == 1047, "500 permille -> DShot 1047, input set");

    const int calls = set_inputs;
    ticks(MS(1));
    poll();
    CHECK(set_inputs == calls + 1 && newinput == 1047, "applied again every millisecond without new frames");
    ticks(MS(150));
    CHECK(signaltimeout <= 1, "signaltimeout held at 0 while B6 is armed (%u)", signaltimeout);

    eepromBuffer.bi_direction = 1;
    throttleReq(3, -1000, 200);
    poll();
    CHECK(responseCode(3) == 0 && newinput == 1047, "bidirectional -1000 -> 1047 (full reverse)");
    throttleReq(4, 1000, 200);
    poll();
    CHECK(newinput == 2047, "bidirectional +1000 -> 2047");
}

static void testWatchdogStopsMotor(void)
{
    reset();
    queue(1, B6_ARM, NULL, 0);
    throttleReq(2, 800, 200);
    poll();
    running = 1;
    duty_cycle_setpoint = 1600;

    ticks(MS(200));
    // no main loop pass yet: the tick alone has already removed the throttle
    CHECK(newinput == 0 && input == 0 && adjusted_input == 0 && duty_cycle_setpoint == 0,
        "watchdog: throttle zero in the control loop interrupt");
    CHECK(armed, "AM32 still armed until the main loop runs");
    const int calls = set_inputs;
    b6Am32Poll();       // b6Service() disarms first, no 1 kHz re-apply of the old throttle
    rxq_len = rxq_pos = 0;
    decode();
    CHECK(set_inputs == calls && newinput == 0, "old throttle not applied again after the trip");
    CHECK(!armed && !b6ThrottleActive() && running == 0 && all_offs >= 1, "main loop: disarmed, phases off");
    CHECK(eventCount(B6_EVT_LINK_WATCHDOG) == 1 && eventCount(B6_EVT_DISARMED) == 1, "events sent");
    CHECK(capture_on, "signal input back on after the watchdog disarm");

    // signaltimeout counts again: without frames the main loop resets the MCU as before
    ticks(MS(300));
    CHECK(signaltimeout >= MS(300), "signaltimeout counts once disarmed");
}

static void testOverCurrent(void)
{
    reset();
    queue(1, B6_ARM, NULL, 0);
    throttleReq(2, 400, 200);
    poll();

    // ocd.c trip: gates off and AM32 disarmed in the interrupt, input capture off
    armed = 0;
    capture_on = 0;
    ocd_latched = 1;
    ocd_new = 1;
    ocd_pins_low = 1;   // still in over-current
    throttleReq(3, 400, 200);
    poll();
    CHECK(eventCount(B6_EVT_CURRENT_LIMIT) == 1 && eventCount(B6_EVT_DISARMED) == 1, "EVENT current limit, disarmed");
    CHECK(!b6ThrottleActive() && !capture_on, "input stays off while the over-current is latched");
    CHECK(responseCode(3) == 0 || responseCode(3) == B6_NACK_NOT_ARMED, "THROTTLE in the same pass answered");

    queue(4, B6_ARM, NULL, 0);
    poll();
    CHECK(responseCode(4) == B6_NACK_FAULT, "ARM while latched: NACK 6");

    queue(5, B6_DISARM, NULL, 0);
    poll();
    CHECK(ocd_latched && !capture_on, "DISARM with the pin still low: latch stays");

    ocd_pins_low = 0;
    queue(6, B6_DISARM, NULL, 0);
    poll();
    CHECK(!ocd_latched && capture_on, "DISARM with the pin high: latch released, input on");
    queue(7, B6_ARM, NULL, 0);
    poll();
    CHECK(responseCode(7) == 0 && armed, "ARM possible again");
}

static void testTelemetryUnits(void)
{
    reset();
    queue(1, B6_ARM, NULL, 0);
    throttleReq(2, 250, 200);
    queue(3, B6_TELEM_CFG, (const uint8_t[]){ 100, 0 }, 2);
    poll();
    running = 1;
    battery_voltage = 2960;             // 10 mV units
    actual_current = 1234;              // 10 mA units
    e_rpm = 512;                        // 100 eRPM units
    commutation_interval = 162;         // 0.5 us units
    consumed_current = 25 << 16;        // mAh in 16.16
    degrees_celsius = 42;
    duty_cycle = 900;
    temp_advance = 16;
    ticks(MS(10));
    poll();

    const b6_frame_t* t = NULL;
    for (int i = 0; i < rx_count; i++) {
        if (rx[i].type == B6_TELEMETRY) {
            t = &rx[i];
        }
    }
    CHECK(t != NULL, "telemetry at 100 Hz");
    if (t) {
        const uint8_t* p = t->payload;
        const uint16_t flags = (uint16_t)(p[6] | (p[7] << 8));
        CHECK(p[5] == B6_SRC_B6, "source B6");
        CHECK((flags & (B6_FLAG_ARMED | B6_FLAG_RUNNING | B6_FLAG_DRIVER_ENABLED))
                == (B6_FLAG_ARMED | B6_FLAG_RUNNING | B6_FLAG_DRIVER_ENABLED),
            "flags armed, running, driver enabled");
        CHECK((int16_t)(p[8] | (p[9] << 8)) == 250 && (p[10] | (p[11] << 8)) == 450, "throttle 250, duty 45.0 %%");
        CHECK(u32(p + 14) == 51200U && (p[18] | (p[19] << 8)) == 81, "eRPM 51200, interval 81 us");
        CHECK((p[20] | (p[21] << 8)) == 29600 && (int32_t)u32(p + 22) == 12340, "29.6 V, 12.34 A");
        CHECK(u32(p + 26) == 25000U, "25 mAh = 25000 uAh");
        CHECK((int16_t)(p[30] | (p[31] << 8)) == 4200, "42 degC");
        CHECK(p[34] == 15, "advance 16/64 of 60 deg = 15 deg");
    }
}

int main(void)
{
    testResetEvent();
    testExclusiveInputs();
    testThrottlePath();
    testWatchdogStopsMotor();
    testOverCurrent();
    testTelemetryUnits();
    return report("test_b6_am32");
}
