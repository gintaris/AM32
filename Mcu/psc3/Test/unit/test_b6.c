/*
 * Host test for protocol B6 (Src/b6_protocol.c): CRC, framing and resync,
 * the requests of v0.1 (PARAM_* included), the link watchdog, STOP latch, the armed-only
 * refusals and the telemetry record layout. The b6Port* hooks are fakes that
 * record what the firmware glue would have been asked to do; transmitted
 * frames go through the same parser as a PC would use.
 */

#include <stdlib.h>
#include <string.h>

#include "b6_protocol.h"
#include "unit.h"

#define TICK_HZ 20000U
#define MS(n)   ((n) * (TICK_HZ / 1000U))

// ---------------------------------------------------------------- fakes
static uint8_t tx[8192];
static uint32_t tx_len;
static uint32_t tx_room = sizeof(tx);
static uint8_t arm_refuse;
static uint8_t bidir;
static int arms, disarms, disarm_requests, cuts, links, throttle_calls;
static int16_t last_throttle;

uint8_t b6PortSend(const uint8_t* frame, uint32_t len)
{
    if (tx_len + len > tx_room) {
        return 0;
    }
    memcpy(tx + tx_len, frame, len);
    tx_len += len;
    return 1;
}

void b6PortLinkActive(void)
{
    links++;
}

uint8_t b6PortArmCheck(void)
{
    return arm_refuse;
}

void b6PortArm(void)
{
    arms++;
}

void b6PortDisarm(void)
{
    disarms++;
}

void b6PortDisarmRequest(void)
{
    disarm_requests++;
}

void b6PortThrottle(int16_t permille)
{
    throttle_calls++;
    last_throttle = permille;
}

void b6PortThrottleCut(void)
{
    cuts++;
}

uint8_t b6PortBidirectional(void)
{
    return bidir;
}

uint8_t b6PortBeep(void)
{
    return 0;
}

void b6PortHello(b6_hello_t* h)
{
    memcpy(h->firmware, "AM32 2.21", 9);
    memcpy(h->target, "EVK_PSC3", 8);
    h->build_hash = 0x16e4b4d0U;
    h->mcu_id = 0x0123456789ABCDEFULL;
    h->power_stage_id = 1;
    h->power_input_id = 1;
}

void b6PortTelemetry(b6_telemetry_t* t)
{
    t->source = B6_SRC_B6;
    t->flags = B6_FLAG_ARMED | B6_FLAG_DRIVER_ENABLED;
    t->throttle = -250;
    t->duty = 333;
    t->duty_limit = 1000;
    t->erpm = 123456;
    t->comm_interval_us = 81;
    t->vbus_mv = 29600;
    t->current_ma = -1500;
    t->consumed_uah = 987654;
    t->temp_stage_cc = 4250;
    t->temp_mcu_cc = B6_TEMP_UNKNOWN;
    t->advance_deg = 15;
    t->zero_crosses = 0xDEADBEEFU;
    t->bemf_timeouts = 7;
    t->desyncs = 3;
}

// two settings: 0 = 0..100, 1 = even 2..36; staged until SAVE
static uint16_t par_saved[2] = { 10, 14 }, par_staged[2] = { 10, 14 };
static int par_saves, restarts;

uint16_t b6PortParamCount(void)
{
    return 2;
}

uint8_t b6PortParamInfo(uint16_t index, b6_param_info_t* info)
{
    info->flags = B6_PARAM_RESTART | (index ? B6_PARAM_EVEN : 0);
    info->min = index ? 2 : 0;
    info->max = index ? 36 : 100;
    info->def = 0xFFFF;
    info->mul = 40;
    info->div = 1;
    info->add = -150;
    memcpy(info->name, index ? "motor_poles" : "motor_kv", index ? 11 : 8);
    memcpy(info->unit, "KV", 2);
    return 0;
}

uint8_t b6PortParamGet(uint16_t index, uint16_t* value, uint16_t* saved)
{
    *value = par_staged[index];
    *saved = par_saved[index];
    return 0;
}

uint8_t b6PortParamSet(uint16_t index, uint16_t value)
{
    if (value > (index ? 36 : 100) || (index && (value < 2 || (value & 1U)))) {
        return B6_NACK_RANGE;
    }
    par_staged[index] = value;
    return 0;
}

uint8_t b6PortParamSave(uint8_t* changed)
{
    *changed = memcmp(par_staged, par_saved, sizeof(par_saved)) != 0;
    if (*changed) {
        memcpy(par_saved, par_staged, sizeof(par_saved));
        par_saves++;
    }
    return 0;
}

void b6PortRestart(void)
{
    restarts++;
}

// ---------------------------------------------------------------- helpers
static void reset(void)
{
    b6Init(TICK_HZ);
    tx_len = 0;
    tx_room = sizeof(tx);
    arm_refuse = 0;
    bidir = 0;
    arms = disarms = disarm_requests = cuts = links = throttle_calls = 0;
    last_throttle = 0;
    par_saved[0] = par_staged[0] = 10;
    par_saved[1] = par_staged[1] = 14;
    par_saves = restarts = 0;
}

static void request(uint8_t seq, uint8_t type, const uint8_t* payload, uint16_t len)
{
    uint8_t frame[B6_OVERHEAD + 128];
    const uint32_t n = b6Encode(frame, sizeof(frame), seq, type, payload, len);
    for (uint32_t i = 0; i < n; i++) {
        b6RxByte(frame[i]);
    }
}

static void throttleReq(uint8_t seq, int16_t permille, uint16_t watchdog)
{
    const uint8_t p[4] = { (uint8_t)permille, (uint8_t)((uint16_t)permille >> 8), (uint8_t)watchdog,
        (uint8_t)(watchdog >> 8) };
    request(seq, B6_THROTTLE, p, 4);
}

static void ticks(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        b6Tick();
    }
}

// decoded transmit frames, as the PC sees them
#define MAX_RX 256
static b6_frame_t rx[MAX_RX];
static int rx_count;

static void decodeTx(void)
{
    b6_parser_t p;
    b6ParserReset(&p);
    rx_count = 0;
    for (uint32_t i = 0; i < tx_len; i++) {
        b6ParserPush(&p, tx[i]);
        while (rx_count < MAX_RX && b6ParserNext(&p, &rx[rx_count])) {
            rx_count++;
        }
    }
    CHECK(p.dropped == 0 && p.crc_errors == 0, "transmitted stream decodes without loss");
    tx_len = 0;
}

static const b6_frame_t* findType(uint8_t type, int nth)
{
    for (int i = 0; i < rx_count; i++) {
        if (rx[i].type == type && nth-- == 0) {
            return &rx[i];
        }
    }
    return NULL;
}

static int countType(uint8_t type)
{
    int n = 0;
    for (int i = 0; i < rx_count; i++) {
        n += rx[i].type == type;
    }
    return n;
}

static uint16_t u16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t u32(const uint8_t* p)
{
    return (uint32_t)u16(p) | ((uint32_t)u16(p + 2) << 16);
}

// the one response to a request: ACK, or NACK with its code (0 = ACK)
static int responseCode(uint8_t seq)
{
    for (int i = 0; i < rx_count; i++) {
        if (rx[i].seq == seq && rx[i].type == B6_ACK) {
            CHECK(rx[i].len == 1 && rx[i].payload[0] == seq, "ACK payload is the request sequence");
            return 0;
        }
        if (rx[i].seq == seq && rx[i].type == B6_NACK) {
            CHECK(rx[i].len == 2 && rx[i].payload[0] == seq, "NACK payload starts with the sequence");
            return rx[i].payload[1];
        }
    }
    return -1;
}

static int eventCount(uint16_t code)
{
    int n = 0;
    for (int i = 0; i < rx_count; i++) {
        if (rx[i].type == B6_EVENT && rx[i].len == B6_EVENT_SIZE && u16(rx[i].payload + 4) == code) {
            n++;
        }
    }
    return n;
}

static uint32_t eventData(uint16_t code)
{
    for (int i = 0; i < rx_count; i++) {
        if (rx[i].type == B6_EVENT && u16(rx[i].payload + 4) == code) {
            return u32(rx[i].payload + 6);
        }
    }
    return 0xFFFFFFFFU;
}

static void armOk(uint8_t seq)
{
    request(seq, B6_ARM, NULL, 0);
    decodeTx();
    CHECK(responseCode(seq) == 0 && b6ThrottleActive(), "ARM accepted");
}

// ---------------------------------------------------------------- tests
static void testCrcAndFrame(void)
{
    const uint8_t check[] = "123456789";
    CHECK(b6Crc16(0xFFFFU, check, 9) == 0x29B1U, "CRC-16/CCITT-FALSE check value 0x29B1, got %04X",
        b6Crc16(0xFFFFU, check, 9));

    // THROTTLE +500 permille, 200 ms, sequence 7, built by hand
    const uint8_t payload[4] = { 0xF4, 0x01, 0xC8, 0x00 };
    uint8_t f[16];
    const uint32_t n = b6Encode(f, sizeof(f), 7, B6_THROTTLE, payload, 4);
    const uint8_t head[] = { 0xA5, 0x5A, 0x04, 0x00, 0x07, 0x22, 0xF4, 0x01, 0xC8, 0x00 };
    CHECK(n == 12 && memcmp(f, head, sizeof(head)) == 0, "frame layout: sync, LE length, seq, type, payload");
    const uint16_t crc = b6Crc16(0xFFFFU, f + 2, 8);
    CHECK(f[10] == (uint8_t)crc && f[11] == (uint8_t)(crc >> 8), "CRC little-endian over length..payload");
    CHECK(b6Encode(f, 11, 7, B6_THROTTLE, payload, 4) == 0, "encode refuses a short buffer");
}

static void testParser(void)
{
    b6_parser_t p;
    b6_frame_t fr;
    uint8_t stream[256];
    uint32_t n = 0;
    const uint8_t pay[3] = { 1, 2, 3 };

    // garbage, a false sync, a frame with a bad CRC, an overlong frame, then two good frames
    const uint8_t junk[] = { 0x00, 0xA5, 0x00, 0xA5, 0xA5, 0x5A };
    memcpy(stream + n, junk, sizeof(junk));
    n += sizeof(junk);                                          // A5 5A starts a frame that never completes ...
    n += b6Encode(stream + n, sizeof(stream) - n, 1, B6_PING, pay, 3);
    stream[n - 1] ^= 0xFF;                                      // ... and a bad CRC
    const uint8_t overlong[] = { 0xA5, 0x5A, 0x00, 0x02, 0x09, B6_PING };   // length 512 > 64
    memcpy(stream + n, overlong, sizeof(overlong));
    n += sizeof(overlong);
    n += b6Encode(stream + n, sizeof(stream) - n, 2, B6_PING, pay, 3);
    n += b6Encode(stream + n, sizeof(stream) - n, 3, B6_HELLO, NULL, 0);

    b6ParserReset(&p);
    int got = 0;
    uint8_t seqs[4] = { 0 };
    for (uint32_t i = 0; i < n; i++) {
        b6ParserPush(&p, stream[i]);
        while (b6ParserNext(&p, &fr)) {
            if (got < 4) {
                seqs[got] = fr.seq;
            }
            got++;
            if (fr.seq == 2) {
                CHECK(fr.type == B6_PING && fr.len == 3 && memcmp(fr.payload, pay, 3) == 0, "payload of frame 2");
            }
        }
    }
    CHECK(got == 2 && seqs[0] == 2 && seqs[1] == 3, "resync after junk / bad CRC / overlong: got %d frames", got);
    CHECK(p.crc_errors >= 1, "CRC error counted");

    // a frame split at every position into two pushes ends up identical
    uint8_t one[32];
    const uint32_t m = b6Encode(one, sizeof(one), 9, B6_TELEM_CFG, pay, 2);
    b6ParserReset(&p);
    int ok = 0;
    for (uint32_t i = 0; i < m; i++) {
        b6ParserPush(&p, one[i]);
        if (b6ParserNext(&p, &fr)) {
            ok += (i == m - 1U) && fr.seq == 9 && fr.len == 2;
        }
    }
    CHECK(ok == 1, "frame completes exactly on its last byte");
}

static void testHelloPing(void)
{
    reset();
    const uint8_t ver[2] = { 0, 1 };
    request(5, B6_HELLO, ver, 2);
    const uint8_t ping[5] = { 'h', 'e', 'l', 'l', 'o' };
    request(6, B6_PING, ping, 5);
    decodeTx();

    const b6_frame_t* h = findType(B6_HELLO, 0);
    CHECK(h && h->seq == 5 && h->len == B6_HELLO_SIZE, "HELLO reply, 50 bytes, echoed sequence");
    if (h) {
        CHECK(h->payload[0] == B6_PROTO_MAJOR && h->payload[1] == B6_PROTO_MINOR, "protocol version 0.1");
        CHECK(memcmp(h->payload + 2, "AM32 2.21\0\0\0\0\0\0\0", 16) == 0, "firmware field zero padded");
        CHECK(memcmp(h->payload + 18, "EVK_PSC3\0\0\0\0\0\0\0\0", 16) == 0, "target field");
        CHECK(u32(h->payload + 34) == 0x16e4b4d0U, "build hash at 34");
        CHECK(u32(h->payload + 38) == 0x89ABCDEFU && u32(h->payload + 42) == 0x01234567U, "MCU ID u64 at 38");
        CHECK(u16(h->payload + 46) == 1 && u16(h->payload + 48) == 1, "power stage / input IDs at 46 / 48");
    }
    const b6_frame_t* pg = findType(B6_PING, 0);
    CHECK(pg && pg->seq == 6 && pg->len == 5 && memcmp(pg->payload, ping, 5) == 0, "PING echoed");
    CHECK(links == 2, "every valid frame reports link activity");

    request(7, 0x66, NULL, 0);
    request(8, B6_TELEMETRY, NULL, 0);
    request(9, B6_STATUS, NULL, 0);
    decodeTx();
    CHECK(responseCode(7) == B6_NACK_UNKNOWN && responseCode(8) == B6_NACK_UNKNOWN
            && responseCode(9) == B6_NACK_UNKNOWN,
        "unknown and not implemented types: NACK 1");
}

static void testArmThrottle(void)
{
    reset();
    throttleReq(1, 100, 200);
    throttleReq(2, 0, 200);
    decodeTx();
    CHECK(responseCode(1) == B6_NACK_NOT_ARMED, "THROTTLE != 0 while disarmed: NACK 4");
    CHECK(responseCode(2) == 0 && throttle_calls == 0, "THROTTLE 0 while disarmed: ACK, nothing applied");

    arm_refuse = B6_NACK_INPUT_NOT_ZERO;
    request(3, B6_ARM, NULL, 0);
    arm_refuse = B6_NACK_BUSY;
    request(4, B6_ARM, NULL, 0);
    arm_refuse = B6_NACK_FAULT;
    request(5, B6_ARM, NULL, 0);
    decodeTx();
    CHECK(responseCode(3) == B6_NACK_INPUT_NOT_ZERO && responseCode(4) == B6_NACK_BUSY
            && responseCode(5) == B6_NACK_FAULT && arms == 0 && !b6ThrottleActive(),
        "ARM refusals of the firmware passed through as NACK codes");

    arm_refuse = 0;
    request(6, B6_ARM, NULL, 0);
    decodeTx();
    CHECK(responseCode(6) == 0 && arms == 1 && b6ThrottleActive(), "ARM: ACK");
    CHECK(eventCount(B6_EVT_ARMED) == 1, "EVENT armed");
    request(7, B6_ARM, NULL, 0);
    decodeTx();
    CHECK(responseCode(7) == 0 && arms == 1, "second ARM: ACK, no second arming");

    throttleReq(8, 500, 200);
    throttleReq(9, 1001, 200);
    throttleReq(10, -1, 200);
    const uint8_t short_payload[2] = { 0x10, 0x00 };
    request(11, B6_THROTTLE, short_payload, 2);
    decodeTx();
    CHECK(responseCode(8) == 0 && last_throttle == 500, "THROTTLE 500 applied");
    CHECK(responseCode(9) == B6_NACK_RANGE, "THROTTLE above 1000: NACK 7");
    CHECK(responseCode(10) == B6_NACK_RANGE, "negative THROTTLE, not bidirectional: NACK 7");
    CHECK(responseCode(11) == B6_NACK_BAD_LENGTH, "THROTTLE of 2 bytes: NACK 2");
    CHECK(b6Throttle() == 500, "last valid throttle kept");

    bidir = 1;
    throttleReq(12, -1000, 200);
    decodeTx();
    CHECK(responseCode(12) == 0 && last_throttle == -1000, "bidirectional: -1000 accepted");

    request(13, B6_DISARM, NULL, 0);
    decodeTx();
    CHECK(responseCode(13) == 0 && disarms == 1 && disarm_requests == 1 && !b6ThrottleActive(), "DISARM");
    CHECK(eventCount(B6_EVT_DISARMED) == 1, "EVENT disarmed");
    request(14, B6_DISARM, NULL, 0);
    decodeTx();
    CHECK(responseCode(14) == 0 && disarms == 1 && disarm_requests == 2 && eventCount(B6_EVT_DISARMED) == 0,
        "DISARM while disarmed: ACK, only the fault acknowledge runs");
}

static void testWatchdog(void)
{
    reset();
    armOk(1);
    throttleReq(2, 300, 200);
    decodeTx();

    ticks(MS(200) - 1);
    b6Service();
    CHECK(cuts == 0 && b6ThrottleActive(), "no cut 1 tick before 200 ms");
    ticks(1);
    CHECK(cuts == 1, "throttle cut in the tick that ends 200 ms");
    ticks(3);
    CHECK(cuts == 4, "cut repeated every tick until the main loop disarms");
    CHECK(b6ThrottleActive(), "still armed until b6Service()");

    // a THROTTLE that arrives after the trip but before b6Service() does not revive it
    throttleReq(3, 300, 200);
    decodeTx();
    CHECK(responseCode(3) == B6_NACK_NOT_ARMED && last_throttle == 300 && throttle_calls == 1,
        "THROTTLE after the trip: NACK, not applied");

    b6Service();
    decodeTx();
    CHECK(!b6ThrottleActive() && disarms == 1, "watchdog disarms");
    CHECK(eventCount(B6_EVT_LINK_WATCHDOG) == 1 && eventData(B6_EVT_LINK_WATCHDOG) == 200,
        "EVENT link watchdog with the time in ms");
    CHECK(eventCount(B6_EVT_DISARMED) == 1, "EVENT disarmed after it");
    CHECK(findType(B6_EVENT, 0) && u16(findType(B6_EVENT, 0)->payload + 4) == B6_EVT_LINK_WATCHDOG,
        "watchdog event comes first");
    CHECK(b6_stats.watchdog_trips == 1, "trip counted");
    const int cuts_after = cuts;
    ticks(MS(500));
    CHECK(cuts == cuts_after, "no cuts once disarmed");

    // ARM without THROTTLE: the default 200 ms run from ARM
    reset();
    armOk(4);
    ticks(MS(200));
    b6Service();
    decodeTx();
    CHECK(!b6ThrottleActive() && eventCount(B6_EVT_LINK_WATCHDOG) == 1, "ARM alone trips after 200 ms");

    // regular THROTTLE keeps it alive
    reset();
    armOk(5);
    for (int i = 0; i < 40; i++) {
        ticks(MS(150));
        throttleReq((uint8_t)(10 + i), 200, 200);
        b6Service();
    }
    decodeTx();
    CHECK(b6ThrottleActive() && cuts == 0 && eventCount(B6_EVT_LINK_WATCHDOG) == 0, "6 s of THROTTLE every 150 ms");

    // the watchdog time is clamped to 20..1000 ms, 0 means 200
    reset();
    armOk(6);
    throttleReq(7, 0, 5);
    ticks(MS(20) - 1);
    CHECK(cuts == 0, "5 ms clamped to 20 ms: not yet");
    ticks(1);
    CHECK(cuts == 1, "5 ms clamped to 20 ms: trips");

    reset();
    armOk(8);
    throttleReq(9, 0, 5000);
    ticks(MS(1000) - 1);
    CHECK(cuts == 0, "5000 ms clamped to 1000 ms: not yet");
    ticks(1);
    CHECK(cuts == 1, "5000 ms clamped to 1000 ms: trips");

    reset();
    armOk(10);
    throttleReq(11, 0, 0);
    ticks(MS(200) - 1);
    CHECK(cuts == 0, "0 = 200 ms: not yet");
    ticks(1);
    CHECK(cuts == 1, "0 = 200 ms: trips");
}

static void testStopLatch(void)
{
    reset();
    armOk(1);
    throttleReq(2, 500, 200);
    request(3, B6_STOP, NULL, 0);
    throttleReq(4, 600, 200);
    decodeTx();
    CHECK(responseCode(3) == 0 && eventCount(B6_EVT_STOPPED) == 1, "STOP: ACK and EVENT stopped");
    CHECK(responseCode(4) == 0 && last_throttle == 0 && b6Throttle() == 0 && b6StopLatched(),
        "THROTTLE after STOP: acknowledged, held at 0");
    CHECK(b6ThrottleActive(), "STOP keeps B6 armed (motor stopped)");

    request(5, B6_DISARM, NULL, 0);
    armOk(6);
    throttleReq(7, 600, 200);
    decodeTx();
    CHECK(!b6StopLatched() && last_throttle == 600, "DISARM releases the STOP latch");

    reset();
    request(8, B6_STOP, NULL, 0);
    decodeTx();
    CHECK(responseCode(8) == 0 && !b6StopLatched() && eventCount(B6_EVT_STOPPED) == 0,
        "STOP while disarmed: ACK, nothing to latch");
}

static void testArmedRefusals(void)
{
    reset();
    request(1, B6_PARAM_SAVE, NULL, 0);
    const uint8_t reg[3] = { 0x10, 0x34, 0x12 };
    request(2, B6_DRV_REG_WRITE, reg, 3);
    request(3, B6_BEEP, NULL, 0);
    decodeTx();
    CHECK(responseCode(1) == 0 && responseCode(2) == B6_NACK_UNKNOWN,
        "PARAM_SAVE disarmed: ACK; DRV_REG_WRITE not implemented yet (NACK 1)");
    CHECK(par_saves == 0, "PARAM_SAVE without a change writes nothing");
    CHECK(responseCode(3) == 0, "BEEP disarmed: ACK");

    armOk(4);
    request(5, B6_PARAM_SAVE, NULL, 0);
    request(6, B6_DRV_REG_WRITE, reg, 3);
    request(7, B6_BEEP, NULL, 0);
    decodeTx();
    CHECK(responseCode(5) == B6_NACK_ARMED && responseCode(6) == B6_NACK_ARMED && responseCode(7) == B6_NACK_ARMED,
        "PARAM_SAVE / DRV_REG_WRITE / BEEP armed: NACK 3");
}

static void testParams(void)
{
    reset();
    const uint8_t i0[2] = { 0, 0 }, i1[2] = { 1, 0 }, i9[2] = { 9, 0 };
    request(1, B6_PARAM_INFO, i1, 2);
    request(2, B6_PARAM_INFO, i9, 2);
    request(3, B6_PARAM_INFO, i0, 1);
    decodeTx();
    const b6_frame_t* f = findType(B6_PARAM_INFO, 0);
    CHECK(f && f->seq == 1 && f->len == B6_PARAM_INFO_SIZE, "PARAM_INFO answer: 52 bytes, request sequence");
    CHECK(f && u16(f->payload) == 1 && u16(f->payload + 2) == 2 && f->payload[4] == 0
        && f->payload[5] == (B6_PARAM_RESTART | B6_PARAM_EVEN), "PARAM_INFO: index, count, type, flags");
    CHECK(f && u16(f->payload + 6) == 2 && u16(f->payload + 8) == 36 && u16(f->payload + 10) == 0xFFFF
        && u16(f->payload + 12) == 40 && u16(f->payload + 14) == 1 && (int32_t)u32(f->payload + 16) == -150,
        "PARAM_INFO: min, max, default, mul, div, add (signed)");
    CHECK(f && memcmp(f->payload + 20, "motor_poles", 12) == 0 && f->payload[20 + 23] == 0
        && memcmp(f->payload + 44, "KV", 3) == 0, "PARAM_INFO: zero padded name (24) and unit (8)");
    CHECK(responseCode(2) == B6_NACK_RANGE && responseCode(3) == B6_NACK_BAD_LENGTH,
        "PARAM_INFO: index past the end NACK 7, short request NACK 2");

    const uint8_t set_ok[4] = { 1, 0, 20, 0 }, set_odd[4] = { 1, 0, 21, 0 }, set_big[4] = { 0, 0, 101, 0 };
    request(4, B6_PARAM_SET, set_ok, 4);
    request(5, B6_PARAM_SET, set_odd, 4);
    request(6, B6_PARAM_SET, set_big, 4);
    request(7, B6_PARAM_SET, set_ok, 2);
    request(8, B6_PARAM_GET, i1, 2);
    decodeTx();
    f = findType(B6_PARAM_GET, 0);
    CHECK(f && f->seq == 4 && f->len == B6_PARAM_VALUE_SIZE && u16(f->payload) == 1 && u16(f->payload + 2) == 20
        && u16(f->payload + 4) == 14, "PARAM_SET answers with the staged value and the saved one");
    CHECK(responseCode(5) == B6_NACK_RANGE && responseCode(6) == B6_NACK_RANGE && responseCode(7) == B6_NACK_BAD_LENGTH,
        "PARAM_SET: odd / too big NACK 7, short NACK 2");
    f = findType(B6_PARAM_GET, 1);
    CHECK(f && f->seq == 8 && u16(f->payload + 2) == 20 && u16(f->payload + 4) == 14, "PARAM_GET: staged 20, saved 14");

    armOk(9);
    request(10, B6_PARAM_SET, set_ok, 4);
    request(11, B6_PARAM_GET, i1, 2);
    decodeTx();
    CHECK(responseCode(10) == B6_NACK_ARMED, "PARAM_SET armed: NACK 3");
    CHECK(findType(B6_PARAM_GET, 0) != NULL, "PARAM_GET works while armed");
    request(12, B6_DISARM, NULL, 0);

    request(13, B6_PARAM_SAVE, NULL, 0);
    decodeTx();
    CHECK(responseCode(13) == 0 && par_saves == 1 && par_saved[1] == 20, "PARAM_SAVE disarmed: ACK, the staged value written");
    b6Service();
    CHECK(restarts == 0, "no restart before B6_RESET_DELAY_MS");
    request(14, B6_ARM, NULL, 0);
    request(15, B6_PARAM_SET, set_ok, 4);
    decodeTx();
    CHECK(responseCode(14) == B6_NACK_BUSY && responseCode(15) == B6_NACK_BUSY && arms == 1,
        "ARM and PARAM_SET refused (NACK 8) while the restart is pending");
    ticks(MS(B6_RESET_DELAY_MS) - 1);
    b6Service();
    CHECK(restarts == 0, "no restart 1 tick before the delay");
    ticks(1);
    b6Service();
    CHECK(restarts == 1, "restart B6_RESET_DELAY_MS after the ACK");
}

static void testForceDisarm(void)
{
    reset();
    armOk(1);
    b6ForceDisarm();
    decodeTx();
    CHECK(!b6ThrottleActive() && disarms == 1 && disarm_requests == 0 && eventCount(B6_EVT_DISARMED) == 1,
        "firmware disarm (over-current) without acknowledging the fault");
}

static void testTelemetry(void)
{
    reset();
    const uint8_t rate[2] = { 0xE8, 0x03 };     // 1000 Hz
    request(1, B6_TELEM_CFG, rate, 2);
    decodeTx();
    CHECK(responseCode(1) == 0, "TELEM_CFG: ACK");
    for (int i = 0; i < 100; i++) {             // 100 ms of 20 kHz ticks, main loop after each
        ticks(MS(1));
        b6Service();
    }
    decodeTx();
    const int n = countType(B6_TELEMETRY);
    CHECK(n >= 99 && n <= 101, "1000 Hz for 100 ms: %d records", n);

    const b6_frame_t* t = findType(B6_TELEMETRY, 1);
    CHECK(t && t->len == B6_TELEMETRY_SIZE, "record of 44 bytes");
    if (t) {
        const uint8_t* p = t->payload;
        CHECK(u32(p) == 2000U, "time stamp of the second record: 2000 us, got %u", u32(p));
        CHECK(p[4] == 1 && p[5] == B6_SRC_B6, "layout 1, source");
        CHECK(u16(p + 6) == (B6_FLAG_ARMED | B6_FLAG_DRIVER_ENABLED), "flags at 6");
        CHECK((int16_t)u16(p + 8) == -250 && u16(p + 10) == 333 && u16(p + 12) == 1000, "throttle, duty, limit");
        CHECK(u32(p + 14) == 123456U && u16(p + 18) == 81 && u16(p + 20) == 29600, "eRPM, interval, VBUS");
        CHECK((int32_t)u32(p + 22) == -1500 && u32(p + 26) == 987654U, "current, charge");
        CHECK((int16_t)u16(p + 30) == 4250 && (int16_t)u16(p + 32) == INT16_MIN, "temperatures");
        CHECK(p[34] == 15 && p[35] == 0, "advance, reserved");
        CHECK(u32(p + 36) == 0xDEADBEEFU && u16(p + 40) == 7 && u16(p + 42) == 3, "zero crosses, timeouts, desyncs");
    }
    // records are numbered by the ESC's own sequence
    const b6_frame_t* t0 = findType(B6_TELEMETRY, 0);
    CHECK(t0 && t && (uint8_t)(t0->seq + 1) == t->seq, "telemetry sequence counts up");

    const uint8_t fast[2] = { 0x10, 0x27 };     // 10000 Hz -> 2000
    request(2, B6_TELEM_CFG, fast, 2);
    decodeTx();
    for (int i = 0; i < 20; i++) {
        ticks(MS(1));
        b6Service();
    }
    decodeTx();
    CHECK(countType(B6_TELEMETRY) == 40, "rate clamped to 2000 Hz: %d records in 20 ms", countType(B6_TELEMETRY));

    const uint8_t off[2] = { 0, 0 };
    request(3, B6_TELEM_CFG, off, 2);
    decodeTx();
    ticks(MS(50));
    b6Service();
    decodeTx();
    CHECK(countType(B6_TELEMETRY) == 0, "rate 0: off");

    // a full transmit buffer drops whole frames, never parts
    request(4, B6_TELEM_CFG, rate, 2);
    decodeTx();
    tx_room = 100;
    for (int i = 0; i < 10; i++) {
        ticks(MS(1));
        b6Service();
    }
    decodeTx();
    CHECK(countType(B6_TELEMETRY) == 1 && b6_stats.tx_dropped >= 8, "only whole frames when the buffer fills");
}

static void testThrottleScale(void)
{
    CHECK(b6ThrottleToInput(0, 0) == 0 && b6ThrottleToInput(0, 1) == 0, "0 is stop");
    CHECK(b6ThrottleToInput(1, 0) == 49 && b6ThrottleToInput(1000, 0) == 2047, "unidirectional 48..2047");
    CHECK(b6ThrottleToInput(-500, 0) == 0, "negative unidirectional: stop");
    CHECK(b6ThrottleToInput(1, 1) == 1048 && b6ThrottleToInput(1000, 1) == 2047, "bidirectional forward 1048..2047");
    CHECK(b6ThrottleToInput(-1, 1) == 48 && b6ThrottleToInput(-1000, 1) == 1047, "bidirectional reverse 48..1047");
}

int main(void)
{
    testCrcAndFrame();
    testParser();
    testHelloPing();
    testArmThrottle();
    testWatchdog();
    testStopLatch();
    testArmedRefusals();
    testParams();
    testForceDisarm();
    testTelemetry();
    testThrottleScale();
    return report("test_b6");
}
