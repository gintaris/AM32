/*
 * Host test of the current sensor zero calibration (Mcu/psc3/Src/current_zero.c)
 */

#include <stdio.h>

#include "current_zero.h"
#include "unit.h"

#define NOMINAL 2048U   // 1650 mV of 3300 mV, 12 bit
#define WINDOW  74U     // +-60 mV

static uint16_t feed(current_zero_t* z, uint16_t raw, uint8_t off, int n)
{
    uint16_t out = 0;
    for (int i = 0; i < n; i++) {
        out = currentZeroUpdate(z, raw, off, NOMINAL, WINDOW);
    }
    return out;
}

int main(void)
{
    current_zero_t z;
    uint16_t out;

    // DEV ESC Rev B case: zero 1.663 V = raw 2064; after learning a stopped
    // motor reads the nominal zero, so the common code sees 0 A
    currentZeroInit(&z, NOMINAL);
    out = feed(&z, 2064, 1, 10);
    CHECK(out == 2064 && z.valid == 0, "not valid yet: got %u valid %u", out, z.valid);
    out = feed(&z, 2064, 1, 300);
    CHECK(out == NOMINAL, "learned: got %u", out);
    CHECK(z.valid == 1 && z.zero == 2064, "valid %u zero %u", z.valid, z.zero);

    // running: the zero is frozen, the load shows on top of the nominal zero
    out = feed(&z, 2064 + 50, 0, 1000);
    CHECK(out == NOMINAL + 50, "running: got %u", out);
    CHECK(z.zero == 2064, "frozen while running: zero %u", z.zero);

    // stopped with a reading out of the window: ignored, zero kept
    out = feed(&z, NOMINAL + WINDOW + 1, 1, 1000);
    CHECK(out == NOMINAL + WINDOW + 1 - 16, "out of window: got %u", out);
    CHECK(z.zero == 2064, "out of window kept: zero %u", z.zero);

    // slow drift is followed (IIR)
    feed(&z, 2070, 1, 2000);
    CHECK(z.zero == 2070, "drift: zero %u", z.zero);

    // no sensor (reading near 0): never valid, raw passes unchanged
    currentZeroInit(&z, NOMINAL);
    out = feed(&z, 25, 1, 5000);
    CHECK(out == 25 && z.valid == 0, "no sensor: got %u valid %u", out, z.valid);

    // clamping at both ends
    currentZeroInit(&z, NOMINAL);
    feed(&z, NOMINAL - WINDOW, 1, 300);
    out = feed(&z, 4095, 0, 1);
    CHECK(out == 4095, "clamp high: got %u", out);
    currentZeroInit(&z, NOMINAL);
    feed(&z, NOMINAL + WINDOW, 1, 300);
    out = feed(&z, 0, 0, 1);
    CHECK(out == 0, "clamp low: got %u", out);

    return report("test_current_zero");
}
