# PSC3 (Infineon PSOC Control C3) MCU layer

Target hardware: KIT_PSC3M5_2GO, MCU PSC3M5FDS2LGQ1 (Cortex-M33, 256 kB flash,
64 kB SRAM, VQFN-48), IRSM836 IPM. Build target: `make KIT_2GO_PSC3`.

## Sources and licences

| Folder | Content | Licence |
| --- | --- | --- |
| `Device/` | PSC3 device headers and `ip/` register headers from `mtb-pdl-cat1` release-v3.23.0 (`devices/COMPONENT_CAT1B/include`), unmodified | Apache-2.0, see `Device/LICENSE` |
| `PSC3M5.svd` | `devices/COMPONENT_CAT1B/svd/psc3.svd` from the same PDL release | Apache-2.0 |
| `CMSIS/` | CMSIS-Core 5.x headers (copy of `Mcu/a153/CMSIS`) | Apache-2.0 |
| everything else | written for AM32 | GPL-3.0 |

No PDL driver sources, no BSP code, no Infineon example code and no prebuilt
binaries (`sec_api_link.o`, `secure_region_*.elf`) are used. The peripherals
are programmed at register level through the device headers.
`Inc/system_cat1b.h` replaces the BSP header that the device header includes.

## Boot

With the factory boot configuration (lifecycle NORMAL, `BOOT_SIMPLE_APP`) the
boot ROM starts the plain image at the start of the flash: a vector table, no
MCUboot header and no signature. The CPU stays in the secure state, so the
first image must be a secure image. AM32 therefore runs as one secure image
and uses the secure aliases of all memories and peripherals
(`psc3m5fds2lgq1_s.h`). See AN240106 "Getting started with PSOC Control C3
security".

| Region | Secure address | Size | Content |
| --- | --- | --- | --- |
| flash (C-AHB) | `0x12000000` | 4 kB | bootloader area: `Boot/boot_stub.c` for now, AM32 bootloader later |
| | `0x12001000` | 1 kB | AM32 vector table (VTOR needs 1 kB alignment for 156 entries) |
| | `0x12001400` | | AM32 firmware |
| | `0x1203FBE0` | 32 B | file name |
| | `0x1203FC00` | 1 kB | EEPROM (two 512 byte rows), `EEPROM_START_ADD` |
| SRAM (S-AHB) | `0x34000000` | 64 kB | data, stack at the top |

The same flash is visible at `0x32000000` (secure S-AHB), `0x02000000` and
`0x22000000` (non-secure). The bootloader device info is expected at
`0x12000FE0` (`DEVINFO_ADDRESS`).

`Boot/boot_stub.c` checks the stack pointer and reset vector at `0x12001000`,
sets VTOR and jumps to the firmware. It stands in for the AM32 bootloader
until that is ported; it does not provide the bootloader device info.

## Timers

| AM32 | TCPWM counter | Clock |
| --- | --- | --- |
| PWM (TIM1) | group 1: 260, 261, 262 | PCLK 16 bit divider 0, /1 (`SET_PRESCALER_PWM` sets it to presc + 1) |
| input capture / DShot out | group 2: 516 | PCLK 16 bit divider 1, /(ic_timer_prescaler + 1) |
| `INTERVAL_TIMER` | group 2: 512 | PCLK 16 bit divider 2, 2 MHz |
| `COM_TIMER` | group 2: 513 | PCLK 16 bit divider 2, 2 MHz |
| `UTILITY_TIMER` | group 2: 514 | PCLK 16 bit divider 3, 1 MHz |
| `TEN_KHZ_TIMER` | group 2: 515 | PCLK 16 bit divider 3, 1 MHz, period for `LOOP_FREQUENCY_HZ` |

The 32 bit counters of group 0 are free.

## Gate outputs

The 2GO gate pins are TCPWM group 1 outputs, but the `line` / `line_compl`
pairs do not match the high / low pairs of one phase:

| Pin | BSP name | TCPWM output |
| --- | --- | --- |
| P4.5 | UH | line_compl[262] |
| P4.2 | UL | line[261] |
| P4.4 | VH | line[262] |
| P4.1 | VL | line_compl[260] |
| P4.3 | WH | line_compl[261] |
| P4.0 | WL | line[260] |

AM32 drives all phases with the same duty cycle, so the three counters run the
same PWM and each gate pin selects its signal with `LINE_SEL`
(fixed 0 / fixed 1 / PWM / inverted PWM), the stepper motor control feature of
TCPWM v2. Whether "PWM" and "inverted PWM" on `line_compl` carry the dead time
the same way as on `line` has to be checked on the scope (bring-up step 4).

## Bring-up images

```
make psc3_boot_stub   # obj/PSC3_BOOT_STUB.hex, 0x12000000
make psc3_blink       # obj/PSC3_BLINK.hex, 0x12001000, LED P7.0 at 1 Hz
```

Flash both with the Infineon OpenOCD (ModusToolbox Programming Tools,
`openocd.cfg`), upstream OpenOCD has no PSC3 target.
