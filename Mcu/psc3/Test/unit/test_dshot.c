/*
 * Host test for the shared DShot decoder (Src/dshot.c, computeDshotDMA) with
 * edge time stamps as the PSC3 input capture produces them: counter 516
 * captures both edges into dma_buffer[] (rising, falling, ...) with a 16 bit
 * counter clocked at CPU_FREQUENCY_MHZ / (ic_timer_prescaler + 1):
 *   DShot600, prescaler 0: 180 MHz, 1.667 us bit = 300 clocks
 *   DShot300, prescaler 1:  90 MHz, 3.333 us bit = 300 clocks
 * Also checks the input type detection thresholds of signal.c (checkDshot)
 * for the detection prescaler CPU_FREQUENCY_MHZ / 6 of IO.c.
 */

#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "dshot.h"
#include "unit.h"

// globals of main.c / signal.c / IO.c used by dshot.c
EEprom_t eepromBuffer;
uint16_t dshot_frametime_high = 50000;
uint16_t dshot_frametime_low = 0;
uint8_t dshotcommand;
char forward = 1;
uint8_t running;
uint16_t signaltimeout;
volatile uint16_t newinput;
char play_tone_flag;
uint16_t battery_voltage;
int16_t actual_current;
volatile int16_t degrees_celsius;
uint32_t dma_buffer[64];
uint8_t buffer_padding = 7;
volatile char armed = 1;
volatile char dshot_telemetry;
uint8_t last_dshot_command;
char send_esc_info_flag;
volatile char send_telemetry;
int zero_crosses;
uint8_t max_duty_cycle_change;

extern uint16_t dshot_goodcounts;
extern uint16_t dshot_badcounts;

void saveEEpromSettings(void)
{
}

uint8_t getInputPinState(void)
{
    return 0;
}

#define BIT_CLOCKS  300
#define T1H         225     // 0.75 of a bit
#define T0H         112     // 0.375 of a bit

static uint16_t dshotFrame(uint16_t throttle, int telemetry_request, int inverted_crc)
{
    const uint16_t value = (uint16_t)((throttle << 1) | (telemetry_request ? 1 : 0));
    uint16_t crc = (value ^ (value >> 4) ^ (value >> 8)) & 0xF;
    if (inverted_crc) {
        crc = (~crc) & 0xF;
    }
    return (uint16_t)((value << 4) | crc);
}

// time stamps of the 16 rising and falling edges, 16 bit counter starting at
// start, with up to +-jitter clocks of noise
static void captureFrame(uint16_t frame, uint32_t start, int jitter)
{
    for (int i = 0; i < 16; i++) {
        const int bit = (frame >> (15 - i)) & 1;
        const int noise_r = jitter ? (rand() % (2 * jitter + 1)) - jitter : 0;
        const int noise_f = jitter ? (rand() % (2 * jitter + 1)) - jitter : 0;
        const uint32_t rise = start + (uint32_t)(i * BIT_CLOCKS + noise_r);
        dma_buffer[2 * i] = rise & 0xFFFF;
        dma_buffer[2 * i + 1] = (rise + (uint32_t)((bit ? T1H : T0H) + noise_f)) & 0xFFFF;
    }
}

static void decode(uint16_t throttle, int tlm, int bidir, uint32_t start, int jitter)
{
    dshot_telemetry = (char)bidir;
    send_telemetry = 0;
    newinput = 0;
    captureFrame(dshotFrame(throttle, tlm, bidir), start, jitter);
    computeDshotDMA();
}

int main(void)
{
    EDT_ARMED = 1;  // set by main.c when extended telemetry arming is off
    srand(1);

    // throttle values, normal and bidirectional, with counter wrap and noise
    static const uint16_t throttles[] = { 48, 49, 100, 555, 1000, 1024, 1500, 2000, 2047 };
    for (unsigned t = 0; t < sizeof(throttles) / sizeof(throttles[0]); t++) {
        for (int bidir = 0; bidir <= 1; bidir++) {
            const uint16_t good = dshot_goodcounts;
            decode(throttles[t], 0, bidir, 1000, 0);
            CHECK(newinput == throttles[t], "throttle %u bidir %d -> %u", throttles[t], bidir, newinput);
            decode(throttles[t], 0, bidir, 0xFF00, 0);     // the 16 bit counter wraps inside the frame
            CHECK(newinput == throttles[t], "wrap: throttle %u -> %u", throttles[t], newinput);
            decode(throttles[t], 0, bidir, 30000, 20);     // +-20 clocks = +-0.11 us at DShot600
            CHECK(newinput == throttles[t], "jitter: throttle %u -> %u", throttles[t], newinput);
            CHECK(dshot_goodcounts == good + 3, "good frames counted");
        }
    }

    // telemetry request bit
    decode(1200, 1, 0, 500, 0);
    CHECK(newinput == 1200 && send_telemetry == 1, "telemetry request");

    // a frame with a wrong CRC is dropped
    const uint16_t bad = dshot_badcounts;
    dshot_telemetry = 0;
    newinput = 777;
    captureFrame(dshotFrame(1300, 0, 0) ^ 0x0001, 500, 0);
    computeDshotDMA();
    CHECK(newinput == 777, "bad CRC must not change the input");
    CHECK(dshot_badcounts == bad + 1, "bad CRC counted");

    // the normal CRC is rejected in bidirectional mode and the other way round
    decode(1300, 0, 0, 500, 0);
    dshot_telemetry = 1;
    newinput = 777;
    captureFrame(dshotFrame(1300, 0, 0), 500, 0);
    computeDshotDMA();
    CHECK(newinput == 777, "normal CRC in bidirectional mode");

    // zero throttle and commands
    decode(0, 0, 0, 500, 0);
    CHECK(newinput == 0 && dshotcommand == 0, "zero throttle");
    running = 1;    // commands are only executed while stopped
    decode(7, 0, 0, 500, 0);
    CHECK(newinput == 0 && dshotcommand == 7, "command 7 -> %u", dshotcommand);
    running = 0;

    // input detection with the detection prescaler of IO.c: the shortest
    // pulse (T0H) must land in the DShot600 (1..3) or DShot300 (4..8) window
    // of checkDshot() in signal.c
    const double detect_clock = CPU_FREQUENCY_MHZ * 1e6 / (CPU_FREQUENCY_MHZ / 6 + 1);
    const int t0h_600 = (int)(0.625e-6 * detect_clock);
    const int t0h_300 = (int)(1.25e-6 * detect_clock);
    CHECK(t0h_600 >= 1 && t0h_600 < 4, "DShot600 T0H %d detection clocks", t0h_600);
    CHECK(t0h_300 >= 4 && t0h_300 <= 8, "DShot300 T0H %d detection clocks", t0h_300);
    // checkDshot then sets ic_timer_prescaler 0 / 1, which gives the 300 clock bits above
    CHECK(CPU_FREQUENCY_MHZ * 1e6 / (0 + 1) / 600000 == BIT_CLOCKS, "DShot600 capture clock");
    CHECK(CPU_FREQUENCY_MHZ * 1e6 / (1 + 1) / 300000 == BIT_CLOCKS, "DShot300 capture clock");

    // bidirectional DShot answer: make_dshot_package() fills gcr[] with one
    // entry per bit, IO.c drives the line low for gcr[] != 0. Decode the line
    // levels like a flight controller: idle high, start bit low, a '1' of the
    // 20 bit GCR word is a level change, 5 -> 4 bit GCR table, then
    // eee mmmmmmmmm cccc with the inverted nibble CRC
    static const uint8_t gcr_decode[32] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 9, 10, 11, 0xFF, 13, 14, 15,
        0xFF, 0xFF, 2, 3, 0xFF, 5, 6, 7, 0xFF, 0, 8, 1, 0xFF, 4, 12, 0xFF
    };
    static const uint16_t periods[] = { 100, 300, 511, 512, 1000, 4000, 12345, 40000, 65535 };
    running = 1;
    for (unsigned t = 0; t < sizeof(periods) / sizeof(periods[0]); t++) {
        memset(gcr, 0, sizeof(gcr));
        make_dshot_package(periods[t]);
        const int pad = buffer_padding;
        CHECK(gcr[pad] == 0 && gcr[pad + 1] != 0, "idle high, then the start bit low");
        uint32_t word = 0;
        int level = 1;  // start bit: line low
        for (int i = 0; i < 20; i++) {
            const int next = gcr[pad + 2 + i] != 0;
            word = (word << 1) | (uint32_t)(next ^ level);
            level = next;
        }
        uint16_t value = 0;
        int valid = 1;
        for (int n = 3; n >= 0; n--) {
            const uint8_t nibble = gcr_decode[(word >> (5 * n)) & 0x1F];
            valid &= nibble != 0xFF;
            value = (uint16_t)((value << 4) | (nibble & 0xF));
        }
        CHECK(valid, "period %u: invalid GCR symbol", periods[t]);
        const uint16_t data = value >> 4;
        const uint16_t crc = (uint16_t)(~(data ^ (data >> 4) ^ (data >> 8)) & 0xF);
        CHECK((value & 0xF) == crc, "period %u: CRC", periods[t]);
        const uint32_t period = (uint32_t)(data & 0x1FF) << (data >> 9);
        // the 9 bit mantissa keeps the period to 1 / 256
        CHECK(period <= periods[t] && period * 257U / 256U + 1U >= periods[t], "period %u decoded as %u",
            periods[t], (unsigned)period);
    }
    // stopped: the longest period
    running = 0;
    make_dshot_package(1000);
    running = 1;
    CHECK(gcr[buffer_padding + 1] != 0, "stopped answer sent");

    return report("test_dshot");
}
