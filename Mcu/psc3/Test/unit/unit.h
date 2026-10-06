/*
 * unit.h
 *
 * Minimal checks for the PSC3 host unit tests.
 */

#ifndef UNIT_H_
#define UNIT_H_

#include <stdio.h>

static int unit_failures;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            unit_failures++;                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

static inline int report(const char* name)
{
    printf("%s: %s\n", name, unit_failures ? "FAILED" : "ok");
    return unit_failures != 0;
}

#endif /* UNIT_H_ */
