/*
 * b6_protocol.c
 *
 * Protocol B6, ESC side (see b6_protocol.h): framing, CRC, the requests of
 * protocol v0.1 and the safety rules
 *  - B6 owns the throttle only between ARM and DISARM; the firmware glue
 *    keeps the DShot / servo input off meanwhile
 *  - no valid THROTTLE within the watchdog time (default 200 ms, 20..1000)
 *    while armed: b6Tick() cuts the throttle in the control loop interrupt at
 *    once, b6Service() then sends EVENT "link watchdog" and disarms. A lost
 *    USB link ends the same way
 *  - STOP holds the throttle at 0 until DISARM
 *  - PARAM_SAVE and DRV_REG_WRITE are refused while armed
 */

#include "b6_protocol.h"

#include <string.h>

#include "targets.h"

#ifdef USE_B6_PROTOCOL

b6_parser_t b6_parser;
b6_stats_t b6_stats;

static uint32_t tick_hz = 20000U;
static uint32_t us_per_tick = 50U;
static volatile uint32_t ticks;

static volatile uint8_t armed;
static uint8_t stop_latched;
static int16_t throttle;
static uint16_t watchdog_ms = B6_WATCHDOG_DEFAULT_MS;
static volatile uint32_t watchdog_left;     // ticks, counted down by b6Tick()
static volatile uint8_t watchdog_tripped;
static uint8_t tx_seq;
static uint16_t telem_hz;
static uint32_t telem_next;

static void putU16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void putU32(uint8_t* p, uint32_t v)
{
    putU16(p, (uint16_t)v);
    putU16(p + 2, (uint16_t)(v >> 16));
}

static uint16_t getU16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

// CRC-16/CCITT-FALSE: polynomial 0x1021, start 0xFFFF, no reflection, no final xor
uint16_t b6Crc16(uint16_t crc, const uint8_t* data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint32_t b = 0; b < 8U; b++) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint32_t b6Encode(uint8_t* out, uint32_t size, uint8_t seq, uint8_t type, const uint8_t* payload, uint16_t len)
{
    if (len > B6_MAX_PAYLOAD || size < B6_OVERHEAD + (uint32_t)len) {
        return 0;
    }
    out[0] = B6_SYNC0;
    out[1] = B6_SYNC1;
    putU16(out + 2, len);
    out[4] = seq;
    out[5] = type;
    if (len) {
        memcpy(out + B6_HEADER, payload, len);
    }
    putU16(out + B6_HEADER + len, b6Crc16(0xFFFFU, out + 2, 4U + len));
    return B6_OVERHEAD + (uint32_t)len;
}

void b6ParserReset(b6_parser_t* p)
{
    memset(p, 0, sizeof(*p));
}

static void dropBytes(b6_parser_t* p, uint16_t k)
{
    memmove(p->buf, p->buf + k, (size_t)(p->n - k));
    p->n = (uint16_t)(p->n - k);
}

void b6ParserPush(b6_parser_t* p, uint8_t byte)
{
    if (p->n == sizeof(p->buf)) {
        // cannot happen with b6ParserNext() called after every byte
        dropBytes(p, 1);
        p->dropped++;
    }
    p->buf[p->n++] = byte;
}

uint8_t b6ParserNext(b6_parser_t* p, b6_frame_t* f)
{
    while (p->n > 0U) {
        if (p->buf[0] != B6_SYNC0 || (p->n > 1U && p->buf[1] != B6_SYNC1)) {
            dropBytes(p, 1);
            p->dropped++;
            continue;
        }
        if (p->n < B6_HEADER) {
            return 0;
        }
        const uint16_t len = getU16(p->buf + 2);
        if (len > B6_RX_PAYLOAD_MAX) {
            dropBytes(p, 1);
            p->dropped++;
            continue;
        }
        const uint16_t total = (uint16_t)(B6_OVERHEAD + len);
        if (p->n < total) {
            return 0;
        }
        if (getU16(p->buf + B6_HEADER + len) != b6Crc16(0xFFFFU, p->buf + 2, 4U + len)) {
            p->crc_errors++;
            dropBytes(p, 1);
            p->dropped++;
            continue;
        }
        f->len = len;
        f->seq = p->buf[4];
        f->type = p->buf[5];
        memcpy(f->payload, p->buf + B6_HEADER, len);
        dropBytes(p, total);
        return 1;
    }
    return 0;
}

void b6PackTelemetry(uint8_t out[B6_TELEMETRY_SIZE], const b6_telemetry_t* t)
{
    putU32(out + 0, t->time_us);
    out[4] = B6_TELEMETRY_LAYOUT;
    out[5] = t->source;
    putU16(out + 6, t->flags);
    putU16(out + 8, (uint16_t)t->throttle);
    putU16(out + 10, t->duty);
    putU16(out + 12, t->duty_limit);
    putU32(out + 14, t->erpm);
    putU16(out + 18, t->comm_interval_us);
    putU16(out + 20, t->vbus_mv);
    putU32(out + 22, (uint32_t)t->current_ma);
    putU32(out + 26, t->consumed_uah);
    putU16(out + 30, (uint16_t)t->temp_stage_cc);
    putU16(out + 32, (uint16_t)t->temp_mcu_cc);
    out[34] = t->advance_deg;
    out[35] = 0;
    putU32(out + 36, t->zero_crosses);
    putU16(out + 40, t->bemf_timeouts);
    putU16(out + 42, t->desyncs);
}

void b6PackHello(uint8_t out[B6_HELLO_SIZE], const b6_hello_t* h)
{
    out[0] = B6_PROTO_MAJOR;
    out[1] = B6_PROTO_MINOR;
    memcpy(out + 2, h->firmware, 16);
    memcpy(out + 18, h->target, 16);
    putU32(out + 34, h->build_hash);
    putU32(out + 38, (uint32_t)h->mcu_id);
    putU32(out + 42, (uint32_t)(h->mcu_id >> 32));
    putU16(out + 46, h->power_stage_id);
    putU16(out + 48, h->power_input_id);
}

/*
 * Throttle in permille to the AM32 input in the DShot scale: 0 stop,
 * 48..2047 forward. Bidirectional: 48..1047 reverse, 1048..2047 forward,
 * as setInput() reads a bidirectional DShot value.
 */
uint16_t b6ThrottleToInput(int16_t permille, uint8_t bidirectional)
{
    if (permille == 0) {
        return 0;
    }
    if (!bidirectional) {
        return permille > 0 ? (uint16_t)(48 + (permille * 1999) / 1000) : 0U;
    }
    if (permille > 0) {
        return (uint16_t)(1048 + (permille * 999) / 1000);
    }
    return (uint16_t)(48 + (-permille * 999) / 1000);
}

static void send(uint8_t seq, uint8_t type, const uint8_t* payload, uint16_t len)
{
    uint8_t frame[B6_OVERHEAD + B6_RX_PAYLOAD_MAX];
    const uint32_t n = b6Encode(frame, sizeof(frame), seq, type, payload, len);
    if (n == 0 || !b6PortSend(frame, n)) {
        b6_stats.tx_dropped++;
    }
}

static void ack(const b6_frame_t* f)
{
    send(f->seq, B6_ACK, &f->seq, 1);
}

static void nack(const b6_frame_t* f, uint8_t code)
{
    const uint8_t p[2] = { f->seq, code };
    send(f->seq, B6_NACK, p, 2);
}

void b6Event(uint16_t code, uint32_t data)
{
    uint8_t p[B6_EVENT_SIZE];
    putU32(p, b6Micros());
    putU16(p + 4, code);
    putU32(p + 6, data);
    send(tx_seq++, B6_EVENT, p, sizeof(p));
}

static uint32_t watchdogTicks(void)
{
    return ((uint32_t)watchdog_ms * tick_hz + 999U) / 1000U;
}

static void disarm(void)
{
    if (!armed) {
        return;
    }
    armed = 0;
    watchdog_left = 0;
    throttle = 0;
    stop_latched = 0;
    b6PortDisarm();
    b6Event(B6_EVT_DISARMED, 0);
}

// the firmware disarmed by itself (over-current): B6 follows
void b6ForceDisarm(void)
{
    disarm();
}

static void handleArm(const b6_frame_t* f)
{
    if (armed) {
        ack(f);
        return;
    }
    const uint8_t refused = b6PortArmCheck();
    if (refused) {
        nack(f, refused);
        return;
    }
    throttle = 0;
    stop_latched = 0;
    b6PortArm();
    watchdog_tripped = 0;
    watchdog_left = watchdogTicks();
    armed = 1;
    ack(f);
    b6Event(B6_EVT_ARMED, 0);
}

static void handleThrottle(const b6_frame_t* f)
{
    if (f->len < 4U) {
        nack(f, B6_NACK_BAD_LENGTH);
        return;
    }
    const int16_t p = (int16_t)getU16(f->payload);
    uint16_t wd = getU16(f->payload + 2);
    if (p > 1000 || p < -1000 || (p < 0 && !b6PortBidirectional())) {
        nack(f, B6_NACK_RANGE);
        return;
    }
    if (!armed) {
        if (p != 0) {
            nack(f, B6_NACK_NOT_ARMED);
        } else {
            ack(f);
        }
        return;
    }
    if (wd == 0) {
        wd = B6_WATCHDOG_DEFAULT_MS;
    }
    watchdog_ms = wd < B6_WATCHDOG_MIN_MS ? B6_WATCHDOG_MIN_MS : (wd > B6_WATCHDOG_MAX_MS ? B6_WATCHDOG_MAX_MS : wd);
    if (watchdog_tripped) {
        nack(f, B6_NACK_NOT_ARMED);     // disarmed in b6Service() right after this frame
        return;
    }
    throttle = stop_latched ? 0 : p;
    watchdog_left = watchdogTicks();
    b6PortThrottle(throttle);
    ack(f);
}

static void handle(const b6_frame_t* f)
{
    uint8_t p[B6_HELLO_SIZE];

    b6_stats.frames++;
    b6PortLinkActive();
    switch (f->type) {
    case B6_HELLO: {
        b6_hello_t h;
        memset(&h, 0, sizeof(h));
        b6PortHello(&h);
        b6PackHello(p, &h);
        send(f->seq, B6_HELLO, p, B6_HELLO_SIZE);
        break;
    }
    case B6_PING:
        send(f->seq, B6_PING, f->payload, f->len);
        break;
    case B6_TELEM_CFG:
        if (f->len < 2U) {
            nack(f, B6_NACK_BAD_LENGTH);
            break;
        }
        telem_hz = getU16(f->payload);
        if (telem_hz > B6_TELEM_MAX_HZ) {
            telem_hz = B6_TELEM_MAX_HZ;
        }
        telem_next = ticks + tick_hz / (telem_hz ? telem_hz : 1U);
        ack(f);
        break;
    case B6_ARM:
        handleArm(f);
        break;
    case B6_DISARM:
        ack(f);
        disarm();
        b6PortDisarmRequest();
        break;
    case B6_THROTTLE:
        handleThrottle(f);
        break;
    case B6_STOP:
        ack(f);
        if (armed) {
            stop_latched = 1;
            throttle = 0;
            b6PortThrottle(0);
            b6Event(B6_EVT_STOPPED, 1);
        }
        break;
    case B6_BEEP: {
        const uint8_t refused = armed ? (uint8_t)B6_NACK_ARMED : b6PortBeep();
        if (refused) {
            nack(f, refused);
        } else {
            ack(f);
        }
        break;
    }
    case B6_PARAM_SAVE:
    case B6_DRV_REG_WRITE:
        // the armed check comes first: these must never run while armed
        nack(f, armed ? B6_NACK_ARMED : B6_NACK_UNKNOWN);
        break;
    default:
        nack(f, B6_NACK_UNKNOWN);
        break;
    }
}

void b6Init(uint32_t hz)
{
    tick_hz = hz;
    us_per_tick = 1000000U / hz;
    ticks = 0;
    armed = 0;
    stop_latched = 0;
    throttle = 0;
    watchdog_ms = B6_WATCHDOG_DEFAULT_MS;
    watchdog_left = 0;
    watchdog_tripped = 0;
    telem_hz = 0;
    b6ParserReset(&b6_parser);
    memset(&b6_stats, 0, sizeof(b6_stats));
}

void b6RxByte(uint8_t byte)
{
    b6_frame_t f;

    b6ParserPush(&b6_parser, byte);
    while (b6ParserNext(&b6_parser, &f)) {
        handle(&f);
    }
}

// control loop interrupt: time base and the link watchdog
void b6Tick(void)
{
    ticks++;
    if (watchdog_left) {
        if (--watchdog_left == 0U) {
            watchdog_tripped = 1;
        }
    }
    if (watchdog_tripped && armed) {
        b6PortThrottleCut();    // again every tick until b6Service() disarms
    }
}

void b6Service(void)
{
    if (watchdog_tripped) {
        watchdog_tripped = 0;
        if (armed) {
            b6_stats.watchdog_trips++;
            b6Event(B6_EVT_LINK_WATCHDOG, watchdog_ms);
            disarm();
        }
    }
    if (telem_hz) {
        const uint32_t period = tick_hz / telem_hz;
        if ((int32_t)(ticks - telem_next) > (int32_t)tick_hz / 20) {
            telem_next = ticks;     // more than 50 ms behind (blocking main loop): skip
        }
        for (uint32_t n = 0; n < 4U && (int32_t)(ticks - telem_next) >= 0; n++) {
            b6_telemetry_t t;
            uint8_t p[B6_TELEMETRY_SIZE];
            memset(&t, 0, sizeof(t));
            t.time_us = b6Micros();
            b6PortTelemetry(&t);
            b6PackTelemetry(p, &t);
            send(tx_seq++, B6_TELEMETRY, p, B6_TELEMETRY_SIZE);
            telem_next += period;
        }
    }
}

uint8_t b6ThrottleActive(void)
{
    return armed;
}

// watchdog expired, the disarm in b6Service() still to come
uint8_t b6WatchdogTripped(void)
{
    return watchdog_tripped;
}

uint8_t b6StopLatched(void)
{
    return stop_latched;
}

int16_t b6Throttle(void)
{
    return throttle;
}

uint32_t b6Ticks(void)
{
    return ticks;
}

uint32_t b6Micros(void)
{
    return ticks * us_per_tick;
}

#endif // USE_B6_PROTOCOL
