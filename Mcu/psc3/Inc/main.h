/*
 * main.h
 *
 * PSC3 (Infineon PSOC Control C3) MCU layer for AM32. The firmware runs as a
 * single secure image, so the secure peripheral aliases are used.
 */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "psc3m5fds2lgq1_s.h"
#include <stdint.h>
#include <string.h>
#include "targets.h"
#include "psc3_common.h"

void Error_Handler(void);

#ifndef RESET
#define RESET 0
#endif

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
