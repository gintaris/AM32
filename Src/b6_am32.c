/*
 * b6_am32.c
 *
 * Protocol B6 hooks on the AM32 state of main.c (targets with
 * USE_B6_PROTOCOL).
 *
 * ARM makes B6 the throttle source: the DShot / servo input capture is
 * switched off until DISARM, so the two inputs never drive the motor
 * together, and arming refuses while another source is armed or its input
 * is not zero. While B6 owns the throttle, its value goes through setInput()
 * every millisecond like a 1 kHz DShot stream (DShot scale, bidirectional
 * as in DroneCAN). Every valid frame clears signaltimeout, so the main loop
 * does not reset the MCU while the PC is connected without a DShot signal;
 * the B6 link watchdog (b6_protocol.c) is the stricter limit while armed.
 */

#include "b6_am32.h"

#include <string.h>

#include "b6_protocol.h"
#include "common.h"
#include "comparator.h"
#include "peripherals.h"
#include "phaseouts.h"
#include "sounds.h"
#include "targets.h"
#include "version.h"

#ifdef USE_B6_PROTOCOL

#ifndef B6_BUILD_HASH
#define B6_BUILD_HASH 0     // set by the makefile from git
#endif

#define INPUT_PERIOD_TICKS  (LOOP_FREQUENCY_HZ / 1000)

extern volatile char armed;
extern volatile char inputSet;
extern char dshot;
extern volatile char servoPwm;
extern uint16_t duty_cycle_setpoint;
extern volatile uint16_t duty_cycle;
extern uint16_t duty_cycle_maximum;
extern int16_t use_current_limit_adjust;
extern char use_current_limit;
extern volatile uint32_t commutation_interval;
extern volatile uint32_t zero_crosses;
extern char old_routine;
extern char stepper_sine;
extern char prop_brake_active;
extern volatile uint8_t zero_throttle_brake_active;
extern char LOW_VOLTAGE_CUTOFF;
extern char cell_count;
extern int32_t consumed_current;
extern uint8_t temp_advance;
extern uint8_t auto_advance_level;
extern uint8_t bemf_timeout_happened;
void saveEEpromSettings(void);     // main.c
#if DRONECAN_SUPPORT
extern uint32_t desync_happened;
#else
extern uint8_t desync_happened;
#endif
extern void setInput(void);

volatile uint32_t b6_commutations;
static uint32_t input_tick;

uint8_t b6PortSend(const uint8_t* frame, uint32_t len)
{
    return b6HwWrite(frame, len);
}

void b6PortLinkActive(void)
{
    signaltimeout = 0;
}

uint8_t b6PortArmCheck(void)
{
    if (armed) {
        return B6_NACK_BUSY;            // armed by DShot / servo
    }
    if (b6HwFault() || LOW_VOLTAGE_CUTOFF) {
        return B6_NACK_FAULT;
    }
    if (newinput != 0) {
        return B6_NACK_INPUT_NOT_ZERO;  // DShot / servo input not at zero
    }
    return 0;
}

void b6PortArm(void)
{
    b6HwInputCapture(0);
    newinput = 0;
    adjusted_input = 0;
    input = 0;
    if (cell_count == 0 && eepromBuffer.low_voltage_cut_off == 1) {
        cell_count = battery_voltage / 370;
    }
    inputSet = 1;
    armed = 1;
    input_tick = b6Ticks();
}

void b6PortDisarm(void)
{
    __disable_irq();
    armed = 0;
    inputSet = 0;
    newinput = 0;
    adjusted_input = 0;
    input = 0;
    duty_cycle_setpoint = 0;
    maskPhaseInterrupts();
    DISABLE_COM_TIMER_INT();
    NVIC_ClearPendingIRQ(COM_TIMER_IRQ);    // a pending commutation would drive the phases again
#ifdef COMPARATOR_IRQ
    NVIC_ClearPendingIRQ(COMPARATOR_IRQ);
#endif
    old_routine = 1;
    running = 0;
    zero_crosses = 0;
    stepper_sine = 0;
    prop_brake_active = 0;
    allOff();
    __enable_irq();
    if (!b6HwFault()) {
        b6HwInputCapture(1);    // stays off while an over-current is latched
    }
}

// DISARM from the PC also acknowledges a latched over-current
void b6PortDisarmRequest(void)
{
    if (!b6HwFault()) {
        return;
    }
    b6HwClearFaults();
    if (!b6HwFault() && !b6ThrottleActive()) {
        b6HwInputCapture(1);
    }
}

void b6PortThrottle(int16_t permille)
{
    newinput = b6ThrottleToInput(permille, eepromBuffer.bi_direction);
    setInput();
    input_tick = b6Ticks();
}

// control loop interrupt, link watchdog expired: no throttle from now on
void b6PortThrottleCut(void)
{
    newinput = 0;
    adjusted_input = 0;
    input = 0;
    duty_cycle_setpoint = 0;
}

uint8_t b6PortBidirectional(void)
{
    return eepromBuffer.bi_direction;
}

uint8_t b6PortBeep(void)
{
    if (running) {
        return B6_NACK_BUSY;
    }
    playDefaultTone();
    return 0;
}

static void putVersion(char* s)
{
    // "AM32 <major>.<minor>"
    uint32_t n = 0;
    const uint32_t parts[2] = { VERSION_MAJOR, VERSION_MINOR };
    memcpy(s, "AM32 ", 5);
    n = 5;
    for (uint32_t i = 0; i < 2U; i++) {
        uint32_t v = parts[i];
        char digits[4];
        uint32_t k = 0;
        do {
            digits[k++] = (char)('0' + v % 10U);
            v /= 10U;
        } while (v && k < sizeof(digits));
        while (k) {
            s[n++] = digits[--k];
        }
        if (i == 0) {
            s[n++] = '.';
        }
    }
}

void b6PortHello(b6_hello_t* h)
{
    putVersion(h->firmware);
    strncpy(h->target, FILE_NAME, sizeof(h->target));
    h->build_hash = B6_BUILD_HASH;
    h->mcu_id = b6HwMcuId();
    h->power_stage_id = b6HwPowerStageId();
    h->power_input_id = b6HwPowerInputId();
}

static uint16_t sat16(uint32_t v)
{
    return v > 0xFFFFU ? 0xFFFFU : (uint16_t)v;
}

void b6PortTelemetry(b6_telemetry_t* t)
{
    const uint8_t b6 = b6ThrottleActive();
    uint16_t flags = b6HwFlags();
    uint16_t limit = duty_cycle_maximum;

    if (armed) {
        flags |= B6_FLAG_ARMED;
        t->source = b6 ? B6_SRC_B6 : (dshot ? B6_SRC_DSHOT : (servoPwm ? B6_SRC_SERVO : B6_SRC_NONE));
    }
    if (running) {
        flags |= B6_FLAG_RUNNING;
    }
    if (forward == eepromBuffer.dir_reversed) {
        flags |= B6_FLAG_REVERSED;
    }
    if (prop_brake_active || zero_throttle_brake_active) {
        flags |= B6_FLAG_BRAKING;
    }
    if (stepper_sine) {
        flags |= B6_FLAG_SINE;
    }
    if (use_current_limit && use_current_limit_adjust < 2000) {
        flags |= B6_FLAG_CURRENT_LIMIT;
        if ((uint16_t)use_current_limit_adjust < limit) {
            limit = (uint16_t)(use_current_limit_adjust > 0 ? use_current_limit_adjust : 0);
        }
    }
    if (duty_cycle_maximum < 2000 && degrees_celsius > eepromBuffer.limits.temperature) {
        flags |= B6_FLAG_TEMP_LIMIT;
    }
    if (LOW_VOLTAGE_CUTOFF) {
        flags |= B6_FLAG_LOW_VOLTAGE;
    }
    if (b6StopLatched()) {
        flags |= B6_FLAG_STOP_LATCHED;
    }
    t->flags = flags;
    if (b6) {
        t->throttle = b6Throttle();
    } else if (adjusted_input >= 47) {
        t->throttle = (int16_t)((adjusted_input - 47) / 2);
    }
    t->duty = (uint16_t)(duty_cycle / 2U);
    t->duty_limit = (uint16_t)(limit / 2U);
    t->erpm = (uint32_t)e_rpm * 100U;
    t->comm_interval_us = running ? sat16(commutation_interval / 2U) : 0xFFFFU;   // 2 MHz timer
    t->vbus_mv = sat16((uint32_t)battery_voltage * 10U);
    t->current_ma = (int32_t)actual_current * 10;
    t->consumed_uah = (uint32_t)(((int64_t)consumed_current * 1000) >> 16);
    t->temp_stage_cc = (int16_t)(degrees_celsius * 100);
    t->temp_mcu_cc = B6_TEMP_UNKNOWN;
    t->advance_deg = (uint8_t)(((eepromBuffer.auto_advance ? auto_advance_level : temp_advance) * 60U) / 64U);
    t->zero_crosses = b6_commutations;
    t->bemf_timeouts = bemf_timeout_happened;
    t->desyncs = (uint16_t)desync_happened;
}

// ---------------------------------------------------------------- settings
// A subset of the EEPROM settings for PARAM_INFO / GET / SET / SAVE. SET
// changes a staged copy only; SAVE writes it to the flash and restarts the
// MCU, because main.c applies the settings once at start-up.
typedef struct {
    uint8_t offset;             // byte in eepromBuffer.buffer
    uint8_t flags;
    uint8_t min;
    uint8_t max;
    uint16_t mul;
    uint16_t div;
    int16_t add;
    const char* name;
    const char* unit;
} param_t;

#define P(off, fl, lo, hi, mu, di, ad, nm, un) { off, B6_PARAM_RESTART | (fl), lo, hi, mu, di, ad, nm, un }

static const param_t params[] = {
    P(26, 0, 0, 255, 40, 1, 20, "motor_kv", "KV"),
    P(27, B6_PARAM_EVEN, 2, 36, 1, 1, 0, "motor_poles", ""),
    P(25, 0, 50, 150, 1, 1, 0, "startup_power", ""),
    P(23, 0, 10, 42, 15, 16, -150, "timing_advance", "deg"),
    P(47, 0, 0, 1, 1, 1, 0, "auto_advance", ""),
    P(24, 0, 16, 48, 1, 1, 0, "pwm_frequency", "kHz"),
    P(21, 0, 0, 2, 1, 1, 0, "variable_pwm", ""),
    P(20, 0, 0, 1, 1, 1, 0, "complementary_pwm", ""),
    P(5, 0, 1, 250, 1, 10, 0, "max_ramp", "%/ms"),
    P(6, 0, 0, 50, 1, 1, 0, "minimum_duty", ""),
    P(19, 0, 0, 1, 1, 1, 0, "sine_start", ""),
    P(45, 0, 1, 10, 1, 1, 0, "sine_start_power", ""),
    P(40, 0, 5, 25, 1, 1, 0, "sine_changeover", "%"),
    P(22, 0, 0, 1, 1, 1, 0, "stuck_rotor_protection", ""),
    P(29, 0, 0, 1, 1, 1, 0, "stall_protection", ""),
    P(43, 0, 70, 255, 1, 1, 0, "temperature_limit", "degC"),
    P(44, 0, 0, 255, 2, 1, 0, "current_limit", "A"),   // 1..100 = 2..200 A; 0 or above 100 = off (AM32 default 102)
    P(36, 0, 0, 2, 1, 1, 0, "low_voltage_cutoff", ""),
    P(37, 0, 0, 100, 1, 100, 250, "cell_cutoff", "V"),
    P(17, 0, 0, 1, 1, 1, 0, "reversed", ""),
    P(18, 0, 0, 1, 1, 1, 0, "bidirectional", ""),
    P(28, 0, 0, 1, 1, 1, 0, "brake_on_stop", ""),
    P(30, 0, 0, 11, 1, 1, 0, "beep_volume", ""),
};

#define PARAM_COUNT ((uint16_t)(sizeof(params) / sizeof(params[0])))

static uint8_t staged[sizeof(eepromBuffer.buffer)];
static uint8_t staged_valid;

static void stageLoad(void)
{
    if (!staged_valid) {
        memcpy(staged, eepromBuffer.buffer, sizeof(staged));
        staged_valid = 1;
    }
}

// the motor must stand still, whichever input armed it
static uint8_t paramBusy(void)
{
    return (armed || running) ? (uint8_t)B6_NACK_ARMED : 0U;
}

uint16_t b6PortParamCount(void)
{
    return PARAM_COUNT;
}

uint8_t b6PortParamInfo(uint16_t index, b6_param_info_t* info)
{
    if (index >= PARAM_COUNT) {
        return B6_NACK_RANGE;
    }
    const param_t* p = &params[index];
    info->type = 0;
    info->flags = p->flags;
    info->min = p->min;
    info->max = p->max;
    info->def = 0xFFFFU;
    info->mul = p->mul;
    info->div = p->div;
    info->add = p->add;
    strncpy(info->name, p->name, sizeof(info->name));
    strncpy(info->unit, p->unit, sizeof(info->unit));
    return 0;
}

uint8_t b6PortParamGet(uint16_t index, uint16_t* value, uint16_t* saved)
{
    if (index >= PARAM_COUNT) {
        return B6_NACK_RANGE;
    }
    stageLoad();
    *value = staged[params[index].offset];
    *saved = eepromBuffer.buffer[params[index].offset];
    return 0;
}

uint8_t b6PortParamSet(uint16_t index, uint16_t value)
{
    if (index >= PARAM_COUNT) {
        return B6_NACK_RANGE;
    }
    const param_t* p = &params[index];
    if (value < p->min || value > p->max || ((p->flags & B6_PARAM_EVEN) && (value & 1U))) {
        return B6_NACK_RANGE;
    }
    const uint8_t busy = paramBusy();
    if (busy) {
        return busy;
    }
    stageLoad();
    staged[p->offset] = (uint8_t)value;
    return 0;
}

uint8_t b6PortParamSave(uint8_t* changed)
{
    const uint8_t busy = paramBusy();
    if (busy) {
        return busy;
    }
    stageLoad();
    *changed = memcmp(staged, eepromBuffer.buffer, sizeof(staged)) != 0;
    if (*changed) {
        memcpy(eepromBuffer.buffer, staged, sizeof(staged));
        saveEEpromSettings();
    }
    return 0;
}

void b6PortRestart(void)
{
    b6HwRestart();
}

void b6Am32Init(void)
{
    staged_valid = 0;   // staged settings follow the EEPROM read at start-up
    b6HwInit();
    b6Init(LOOP_FREQUENCY_HZ);
    b6Event(B6_EVT_RESET, b6HwResetCause());
}

void b6Am32Tick(void)
{
    b6Tick();
    if (b6ThrottleActive()) {
        signaltimeout = 0;  // the B6 link watchdog (20..1000 ms) replaces the 0.5 s signal timeout
    }
}

void b6Am32Poll(void)
{
    uint8_t c;
    while (b6HwRead(&c)) {
        b6RxByte(c);
    }
    const uint8_t ocd = b6HwOcdTripped();
    if (ocd) {
        b6Event(B6_EVT_CURRENT_LIMIT, ocd);
        b6ForceDisarm();
    }
    b6Service();
    // a watchdog trip after b6Service() must not see the old throttle applied again
    if (b6ThrottleActive() && !b6WatchdogTripped() && (b6Ticks() - input_tick) >= INPUT_PERIOD_TICKS) {
        b6PortThrottle(b6Throttle());
    }
}

#endif // USE_B6_PROTOCOL
