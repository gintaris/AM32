/*
 * b6_protocol.h
 *
 * Protocol B6: binary packets between a PC program and the ESC over a byte
 * stream (UART, later CAN FD or USB). Specification: protocol-b6.md (v0.1).
 *
 * Frame: sync A5 5A, payload length (LE16, <= 1024), sequence, type,
 * payload (little-endian fields), CRC-16/CCITT-FALSE (LE16) over length to
 * payload. The ESC answers every request with its own response, ACK or NACK
 * and echoes the request's sequence.
 *
 * This part has no AM32 globals and no hardware: the firmware glue
 * (Src/b6_am32.c) or a host test implements the b6Port* hooks.
 *
 * Contexts: b6Tick() runs in the control loop interrupt, everything else in
 * the main loop.
 */

#ifndef B6_PROTOCOL_H_
#define B6_PROTOCOL_H_

#include <stdint.h>

#define B6_PROTO_MAJOR          0
#define B6_PROTO_MINOR          1

#define B6_SYNC0                0xA5
#define B6_SYNC1                0x5A
#define B6_HEADER               6U      // sync, length, sequence, type
#define B6_OVERHEAD             8U      // header and CRC
#define B6_MAX_PAYLOAD          1024U   // protocol limit
#define B6_RX_PAYLOAD_MAX       64U     // longest request kept; longer frames are skipped like bad ones

// message types
#define B6_HELLO                0x01
#define B6_PING                 0x02
#define B6_TELEMETRY            0x10
#define B6_TELEM_CFG            0x11
#define B6_STATUS               0x12
#define B6_ARM                  0x20
#define B6_DISARM               0x21
#define B6_THROTTLE             0x22
#define B6_STOP                 0x23
#define B6_BEEP                 0x24
#define B6_PARAM_INFO           0x30
#define B6_PARAM_GET            0x31
#define B6_PARAM_SET            0x32
#define B6_PARAM_SAVE           0x33
#define B6_DRV_REG_READ         0x40
#define B6_DRV_REG_WRITE        0x41
#define B6_EVENT                0x50
#define B6_TRACE_GET            0x51
#define B6_ACK                  0x7E
#define B6_NACK                 0x7F

// NACK codes
#define B6_NACK_UNKNOWN         1
#define B6_NACK_BAD_LENGTH      2
#define B6_NACK_ARMED           3       // not allowed while armed
#define B6_NACK_NOT_ARMED       4
#define B6_NACK_INPUT_NOT_ZERO  5
#define B6_NACK_FAULT           6
#define B6_NACK_RANGE           7
#define B6_NACK_BUSY            8

// EVENT codes (parameters.md section 4)
#define B6_EVT_ARMED            1
#define B6_EVT_DISARMED         2
#define B6_EVT_STARTED          3
#define B6_EVT_STOPPED          4
#define B6_EVT_STARTUP_FAILED   5
#define B6_EVT_DESYNC           6
#define B6_EVT_BEMF_TIMEOUT     7
#define B6_EVT_STALL            8
#define B6_EVT_LOW_VOLTAGE      9
#define B6_EVT_TEMPERATURE      10
#define B6_EVT_CURRENT_LIMIT    11      // data: bit 0 OCD1, bit 1 OCD2 (fast over-current inputs)
#define B6_EVT_DRIVER_FAULT     12
#define B6_EVT_DRIVER_RETRY     13
#define B6_EVT_LINK_WATCHDOG    14      // data: watchdog time, ms
#define B6_EVT_RESET            15      // data: reset cause of the MCU

// telemetry input source
#define B6_SRC_NONE             0
#define B6_SRC_B6               1
#define B6_SRC_DSHOT            2
#define B6_SRC_SERVO            3

// telemetry flags
#define B6_FLAG_ARMED           (1U << 0)
#define B6_FLAG_RUNNING         (1U << 1)
#define B6_FLAG_REVERSED        (1U << 2)
#define B6_FLAG_BRAKING         (1U << 3)
#define B6_FLAG_SINE            (1U << 4)
#define B6_FLAG_CURRENT_LIMIT   (1U << 5)
#define B6_FLAG_TEMP_LIMIT      (1U << 6)
#define B6_FLAG_LOW_VOLTAGE     (1U << 7)
#define B6_FLAG_OCD1            (1U << 8)
#define B6_FLAG_OCD2            (1U << 9)
#define B6_FLAG_DRIVER_FAULT    (1U << 10)
#define B6_FLAG_DRIVER_ENABLED  (1U << 11)
#define B6_FLAG_NBRAKE          (1U << 12)
#define B6_FLAG_STOP_LATCHED    (1U << 13)
#define B6_FLAG_CURRENT_ZERO    (1U << 14)  // current sensor zero learned (PSC3 CURRENT_ZERO_CAL)

#define B6_WATCHDOG_DEFAULT_MS  200U
#define B6_WATCHDOG_MIN_MS      20U
#define B6_WATCHDOG_MAX_MS      1000U
#define B6_TELEM_MAX_HZ         2000U

#define B6_TELEMETRY_LAYOUT     1U
#define B6_TELEMETRY_SIZE       44U
#define B6_HELLO_SIZE           50U
#define B6_EVENT_SIZE           10U
#define B6_PARAM_INFO_SIZE      52U
#define B6_PARAM_VALUE_SIZE     6U
#define B6_PARAM_NAME_LEN       24U
#define B6_PARAM_UNIT_LEN       8U
#define B6_RESET_DELAY_MS       50U     // PARAM_SAVE: ACK leaves the UART before the restart

// PARAM_INFO flags
#define B6_PARAM_RESTART        (1U << 0)   // takes effect after PARAM_SAVE (the ESC restarts)
#define B6_PARAM_EVEN           (1U << 1)   // only even values
#define B6_TEMP_UNKNOWN         INT16_MIN   // temperature not measured

typedef struct {
    uint32_t time_us;
    uint8_t source;
    uint16_t flags;
    int16_t throttle;           // permille
    uint16_t duty;              // permille
    uint16_t duty_limit;        // permille
    uint32_t erpm;
    uint16_t comm_interval_us;
    uint16_t vbus_mv;
    int32_t current_ma;
    uint32_t consumed_uah;
    int16_t temp_stage_cc;      // 0.01 degC
    int16_t temp_mcu_cc;
    uint8_t advance_deg;
    uint32_t zero_crosses;
    uint16_t bemf_timeouts;
    uint16_t desyncs;
} b6_telemetry_t;

typedef struct {
    char firmware[16];          // zero padded, not terminated when 16 characters long
    char target[16];
    uint32_t build_hash;
    uint64_t mcu_id;
    uint16_t power_stage_id;
    uint16_t power_input_id;
} b6_hello_t;

typedef struct {
    uint8_t seq;
    uint8_t type;
    uint16_t len;
    uint8_t payload[B6_RX_PAYLOAD_MAX];
} b6_frame_t;

// receive window: bytes are pushed one by one, b6ParserNext() takes frames out.
// A bad length or CRC drops one byte and searches for the next sync
typedef struct {
    uint8_t buf[B6_OVERHEAD + B6_RX_PAYLOAD_MAX];
    uint16_t n;
    uint32_t crc_errors;
    uint32_t dropped;           // bytes skipped while looking for a frame
} b6_parser_t;

// one setting: an unsigned byte. Shown as (raw * mul + add) / div in unit
typedef struct {
    uint8_t type;               // 0: unsigned 8 bit
    uint8_t flags;              // B6_PARAM_*
    uint16_t min;
    uint16_t max;
    uint16_t def;               // 0xFFFF: no default given
    uint16_t mul;
    uint16_t div;
    int32_t add;
    char name[B6_PARAM_NAME_LEN];   // zero padded
    char unit[B6_PARAM_UNIT_LEN];
} b6_param_info_t;

typedef struct {
    uint32_t frames;
    uint32_t tx_dropped;        // frames that did not fit into the transmit buffer
    uint32_t watchdog_trips;
} b6_stats_t;

uint16_t b6Crc16(uint16_t crc, const uint8_t* data, uint32_t len);
uint32_t b6Encode(uint8_t* out, uint32_t size, uint8_t seq, uint8_t type, const uint8_t* payload, uint16_t len);
void b6ParserReset(b6_parser_t* p);
void b6ParserPush(b6_parser_t* p, uint8_t byte);
uint8_t b6ParserNext(b6_parser_t* p, b6_frame_t* f);
void b6PackTelemetry(uint8_t out[B6_TELEMETRY_SIZE], const b6_telemetry_t* t);
void b6PackHello(uint8_t out[B6_HELLO_SIZE], const b6_hello_t* h);
void b6PackParamInfo(uint8_t out[B6_PARAM_INFO_SIZE], uint16_t index, uint16_t count, const b6_param_info_t* i);
uint16_t b6ThrottleToInput(int16_t permille, uint8_t bidirectional);

// session
void b6Init(uint32_t tick_hz);
void b6RxByte(uint8_t byte);
void b6Tick(void);
void b6Service(void);
void b6Event(uint16_t code, uint32_t data);
void b6ForceDisarm(void);
uint8_t b6ThrottleActive(void);
uint8_t b6StopLatched(void);
uint8_t b6WatchdogTripped(void);
int16_t b6Throttle(void);
uint32_t b6Micros(void);
uint32_t b6Ticks(void);
extern b6_parser_t b6_parser;
extern b6_stats_t b6_stats;

// hooks of the firmware (or the test)
uint8_t b6PortSend(const uint8_t* frame, uint32_t len);    // queue a whole frame, 0 if it does not fit
void b6PortLinkActive(void);                                // any valid frame received
uint8_t b6PortArmCheck(void);                               // 0, or the NACK code that refuses ARM
void b6PortArm(void);
void b6PortDisarm(void);
void b6PortDisarmRequest(void);                             // every DISARM request, after b6PortDisarm()
void b6PortThrottle(int16_t permille);                      // range checked, 0 while stopped
void b6PortThrottleCut(void);                               // control loop interrupt: watchdog expired
uint8_t b6PortBidirectional(void);
uint8_t b6PortBeep(void);                                   // 0, or a NACK code
void b6PortHello(b6_hello_t* h);
void b6PortTelemetry(b6_telemetry_t* t);
uint16_t b6PortParamCount(void);
uint8_t b6PortParamInfo(uint16_t index, b6_param_info_t* info);       // 0, or a NACK code
uint8_t b6PortParamGet(uint16_t index, uint16_t* value, uint16_t* saved);
uint8_t b6PortParamSet(uint16_t index, uint16_t value);                // range checked, staged until SAVE
uint8_t b6PortParamSave(uint8_t* changed);                             // writes flash if *changed is set
void b6PortRestart(void);                                              // MCU reset after a PARAM_SAVE

#endif /* B6_PROTOCOL_H_ */
