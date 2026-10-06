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

## Clocks

`SystemInit()` (`Src/system_psc3.c`) runs before `main()`:

1. CLK_HF0 to the 8 MHz IMO through the glitch free direct mux
2. core voltage to OD (1.2 V) with the SFLASH LDO trim, needed above 150 MHz
3. flash R-bus wait states 60 x f / 1000 + 1 = 11, I-cache on
4. DPLL0 on clock path 1: IHO 48 MHz / Q 6 = 8 MHz, x P 45 = 360 MHz DCO, / 2 = 180 MHz
5. CLK_HF1 (IOSS), CLK_HF3 (TCPWM, HPPASS, SCB0..4) and CLK_HF0 (CPU) from path 1

All TCPWM counters therefore count from 180 MHz = `CPU_FREQUENCY_MHZ`.
If the DPLL does not lock the CPU stays on the IMO and `psc3_dpll_locked`
reads 0 in the debugger. The sequences follow the PDL (`Cy_SysPm_LdoSetVoltage`,
`Cy_SysLib_SetWaitStates`, `Cy_SysClk_DpllLp*`).

`SystemClock_Config()` releases HSIOM, GPIO, TCPWM0, HPPASS and SCB0 in their
PERI groups (SL_CTL2 / SL_CTL) and sets the PERI_PCLK group 5 dividers.

The watchdog is the free running SRSS WDT on the ILO: it resets on the third
uncleared match, about 2 s with `IGNORE_BITS_ABOVE` = 15. It is enabled and
locked from reset with all 32 bits compared, so `MX_IWDG_Init()` unlocks it
first.

## Timers

| AM32 | TCPWM counter | Clock |
| --- | --- | --- |
| PWM (TIM1) | group 1: 260, 261, 262 | PCLK 16 bit divider 0, /1 (`SET_PRESCALER_PWM` sets it to presc + 1) |
| input capture / DShot out | group 2: 516 | PCLK 16 bit divider 1, /(ic_timer_prescaler + 1) |
| `INTERVAL_TIMER` | group 2: 512 | PCLK 16 bit divider 2, 2 MHz |
| `COM_TIMER` | group 2: 513 | PCLK 16 bit divider 2, 2 MHz |
| `UTILITY_TIMER` | group 2: 514 | PCLK 16 bit divider 3, 1 MHz |
| `TEN_KHZ_TIMER` | group 2: 515 | PCLK 16 bit divider 3, 1 MHz, period for `LOOP_FREQUENCY_HZ` |

The 32 bit counters of group 0 are free. Counter 3 only serves as a software
interrupt for `processDshot()` (`DSHOT_SW_IRQ`).

All timer counters run in timer mode counting up, with a terminal count every
PERIOD + 1 ticks like the STM32 ARR + 1. The clocks are in `Inc/psc3_timing.h`.
`COM_TIMER` and `TEN_KHZ_TIMER` interrupt on the terminal count
(`Src/psc3_it.c`); the control loop runs at exactly `LOOP_FREQUENCY_HZ`.

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

AM32 drives all phases with the same duty cycle for six step commutation, so
the three counters run the same PWM and each gate pin selects its signal with
`LINE_SEL` (fixed 0 / fixed 1 / PWM / inverted PWM), the stepper motor control
feature of TCPWM v2 (`Src/phaseouts.c`):

| Phase state | High gate | Low gate |
| --- | --- | --- |
| PWM | PWM | inverted PWM (`comp_pwm`) or 0 |
| LOW | 0 | 1 |
| FLOAT | 0 | 0 |

The counters run in PWM_DT mode, counting up, line set at the terminal count
and cleared at the CC0 match (STM32 PWM mode 1). In the TCPWM output path the
line selection comes before the dead time insertion, so each gate pin gets
`DEAD_TIME` (60 clocks = 333 ns) on its rising edge whatever it selects: a
LINE_SEL change cannot turn on both gates of a phase even though they belong
to different counters. This still has to be confirmed on the scope
(bring-up step 4).

The high and low gate of a phase come from different counters, so the
counters must stay in step:

- they start together on `tcpwm[0].tr_all_cnt_in[0]`, pulsed by software
  through PERI trigger group 9 (`startPwm()`, TRM 24.2.5)
- new PERIOD / CC0 values go to the buffer registers and are taken over at the
  next terminal count after a switch event that all three counters get from
  `tr_all_cnt_in[1]` (`SET_DUTY_CYCLE_ALL`, `SET_AUTO_RELOAD_PWM`). A switch
  reaching only some counters would give a phase two duty cycles for a period.

Limits of this pin map:

- per phase duty cycles are impossible (every counter drives gates of two
  phases), so the target sets `NO_SINE_START` and `setPWMCompare1..3` do
  nothing. A board for this port should put the high and low gate of each phase
  on the `line` / `line_compl` pair of one counter.
- `USE_INVERTED_HIGH` / `USE_INVERTED_LOW` are rejected: there is no output
  polarity control, inverted gates would get the dead time on the wrong edge.
  The IRSM836 inputs are active high.

The IPM is enabled (`IPM_ENABLE_ACTIVE`, polarity still to be checked on the
schematic) only after the counters run with all gates off.

## BEMF comparators

The five HPPASS CSG slices each have a high speed comparator. Slice k
compares AN_Ak or AN_Bk (+) with its 10 bit DAC or with AN_A(k+1) / AN_B(k+1)
(-) (TRM table 253). `Src/comparator.c` uses one slice per phase; the
comparator output is '1' when the phase is above the neutral, like the STM32
targets. `changeCompInput()` selects the slice of the floating phase and its
edge (rising BEMF: falling output edge), the combined CSG comparator interrupt
(`pass_interrupt_csg_cmps`) ignores edges in the first half of the commutation
interval and calls `interruptRoutine()`.

The HPPASS is started by `Src/hppass.c`: SFLASH trims, AREF, start-up timer
(200 ns ticks from clk_infra = CLK_HF3 / 2), and a two state AC program that
enables the CSG slices (and the SAR, DP7) and waits for BLOCK_READY.
`hppass_ready` reads 1 in the debugger when that worked. The CSG clock is
90 MHz from an 8 bit divider of PCLK group 5.

KIT_PSC3M5_2GO analog pins (kit guide): AN_A0 / AN_A1 shunt +/-, AN_A2 current
amplifier, AN_A3 thermistor, AN_A4 DC link divider, AN_B2 / AN_B3 on J4,
AN_B4 potentiometer, AN_A5 / AN_B1 not connected. The phase voltages do not
reach the MCU, so BEMF sensing needs a rework:

| Phase | CSG slice | + input | 2GO pin | - input |
| --- | --- | --- | --- | --- |
| A (U) | 2 | AN_B2 | J4 | DAC2 |
| B (V) | 3 | AN_B3 | J4 | DAC3 |
| C (W) | 4 | AN_B4 | potentiometer R20 wiper (lift it) | DAC4 |

The phase dividers need the ratio of the DC link divider on AN_A4; the DACs
then hold VBUS / 2 (`compReferenceFromBus()` from the 12 bit DC link reading).
No free pin is left for a neutral divider.

Limit of the DAC neutral: with complementary PWM the motor neutral is at
VBUS / 2 only while the PWM is on and near 0 V while it is off, so the
comparator compares correctly only in the on time and toggles with the PWM
once the BEMF is positive. AM32 expects a comparator against the real
(resistor) neutral. Options, to be decided with the kit on the bench:

1. a board for this port: phase dividers on AN_Bk and a neutral divider on
   AN_A(k+1) / AN_B(k+1) of the same slice (set `PHASE_x_COMP_NEG` to
   `CSG_NEG_AN_A` / `CSG_NEG_AN_B`), no firmware change
2. 2GO: sample the floating phase with the SAR in the PWM on time (TCPWM
   trigger) and emulate the comparator in the ADC interrupt (the planned ADC
   fallback, together with DP7)
3. 2GO: CSG hysteretic DAC mode toggled by the PWM edges, so the DAC follows
   the neutral (VBUS / 2 on, 0 off), with the DAC deglitch latch; untested idea

## Host unit tests

```
make psc3_test HOSTCC=clang
```

`Test/unit` builds `Src/phaseouts.c` against plain memory registers and checks
every commutation step for both `comp_pwm` settings (no shoot through, expected
phase states), plus the PWM, dead time, sound and timer clock arithmetic. On
Windows the clang of the Visual Studio Build Tools works as `HOSTCC`.

## Bring-up images

```
make psc3_boot_stub   # obj/PSC3_BOOT_STUB.hex, 0x12000000
make psc3_blink       # obj/PSC3_BLINK.hex, 0x12001000, LED P7.0 at 1 Hz
```

Flash both with the Infineon OpenOCD (ModusToolbox Programming Tools,
`openocd.cfg`), upstream OpenOCD has no PSC3 target.
