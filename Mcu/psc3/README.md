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
| C (W) | 4 | AN_B4 | M_SPEED_POT net, remove R21 (1k from the potentiometer R20) | DAC4 |

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

### BEMF from the SAR (KIT_2GO_PSC3 default, `BEMF_ADC`)

Option 2 is implemented and is the default of the 2GO target (define
`PSC3_BEMF_CSG` for the CSG comparators instead):

- `setPwmDutyAll()` also sets CC1 of PWM counter 261 to half the duty cycle;
  its CC1 match (`tr_out1[261]`) reaches the HPPASS through the 1:1 trigger
  `pass.tr_b_in[2]` (HPPASS TR2) in the middle of every PWM on time
- SAR entry 0 samples AN_B2 / AN_B3 / AN_B4 (channels 10, 11, 12) at the same
  time; its interrupt (`COMPARATOR_IRQ`, priority 0) passes the samples to
  `bemfAdcProcess()` (`Src/comparator_adc.c`)
- the floating phase is compared with half the DC link reading; the emulated
  comparator gives `getCompOutputLevel()` and calls `interruptRoutine()` on the
  edge selected by `rising`, ignoring edges in the first half of the
  commutation interval like the STM32 comparator interrupts

The zero cross resolution is one PWM period (41.7 us at 24 kHz), enough for
the small 2GO motor at moderate speed. The phase dividers must have the ratio
of the DC link divider.

## ADC

`Src/ADC.c`, 12 bit SAR, VDDA reference, offset and gain calibrated when the
HPPASS starts, 711 ns sample time:

| Reading | Pin | SAR channel | Conversion |
| --- | --- | --- | --- |
| bus voltage | AN_A4 | 4 | main.c with `TARGET_VOLTAGE_DIVIDER` (TODO kit divider) |
| current | AN_A2 (shunt amplifier, 1 Ohm shunt) | 2 | main.c with `CURRENT_OFFSET` / `MILLIVOLT_PER_AMP` (TODO amplifier gain) |
| temperature | AN_A3 thermistor | 3 | `getConvertedDegrees()`, table for a 10 k / B3380 NTC with 10 k pull-up (TODO part) |

SAR entry 1 converts these three on the firmware trigger HPPASS TR0 from
`startADCConversion()` in the 1 kHz part of the main loop;
`ADC_DMA_Callback()` reads the channel result registers (no DMA needed) and
updates the virtual neutral.

## Signal input and bidirectional DShot

`Src/IO.c`, J4 pin P8.0, TCPWM counter 516, DataWire 0 channel 0:

- input: the pin goes as `peri.tr_io_input[38]` through PERI trigger group 11
  to the one-to-one input 1 of counter 516 (capture mode, both edges). Every
  capture (`tr_out0` = cc0 event) triggers one DMA transfer of CC0 into
  `dma_buffer[]` through trigger group 0, like the STM32 input capture DMA.
  The 16 bit counter runs at CPU_FREQUENCY_MHZ / (`ic_timer_prescaler` + 1)
  from PCLK divider 1: 300 clocks per DShot600 bit with prescaler 0 and per
  DShot300 bit with prescaler 1
- bidirectional DShot answer: the pin switches to `line[516]` (HSIOM 10), the
  counter runs one GCR bit per period (120 clocks: 750 / 375 kbit/s) and each
  terminal count triggers a DMA transfer of the next level ('0' / '1') into
  `LINE_SEL`, so the bits have no compare glitches; `gcr[] != 0` drives low
  like the inverted STM32 output
- the DMA interrupt (`IC_DMA_IRQ_NAME`) works like the G431 handler and pends
  `DSHOT_SW_IRQ` (unused TCPWM counter 3) for `processDshot()`

## Settings (EEPROM)

`Src/eeprom.c` keeps the 192 byte settings in the 512 byte flash row at
`EEPROM_START_ADD` (0x1203FC00, row aligned, flash sector 1):

- writes go through the boot ROM flash API (`Device/cyboot_flash_list.h`,
  Apache-2.0, as `Cy_Flash_WriteRow` in the PDL): the rest of the row is kept,
  then one blocking write row erases and programs it; the result is in
  `eeprom_write_status` (`CYBOOT_FLASH_SUCCESS` = 0x0D50B002)
- read while write works only between the two 128 kB sectors, so the
  firmware has to stay in sector 0; `PSC3M5_FLASH.ld` stops the link
  otherwise. The cache prefetch is off during the write and the I-cache is
  invalidated afterwards (the settings are read through the cached C-AHB
  alias)
- erased PSC3 flash reads 0x00 (`valEmpty` of the Infineon flash loader
  `PSC3xxE.FLM`). Zero settings are valid values (found on the kit: beep
  volume 0, silent startup tune), and 0xFF like an erased STM32 page is no
  better (rc_car_reverse on switches the complementary PWM off, so the
  IRSM836 bootstrap capacitors never charge). `read_flash_bin()` returns an
  all-zero settings row as the AM32 configurator defaults (the table of
  `Src/DroneCAN/DroneCAN.c`), the tune and the CAN block as 0xFF
- tones: the PWM prescaler is the PCLK divider and slows the TCPWM dead time
  down too, so `setPwmPrescaler()` scales the dead time and the tone duty
  (sized for an STM32 period of about 2000) while a prescaler is set
- a future AM32 bootloader for the PSC3 must use the same row and must not
  report the STM32 flash size codes in its device info (`checkDeviceInfo()`
  would move `eeprom_address` to an STM32 address)

## Telemetry UART

`Src/serial_telemetry.c`: SCB0 UART TX on P9.3 (HSIOM 14), which the kit's
J-Link forwards as its virtual COM port, 115200 8N1 (180 MHz / 120 / 13
oversampling = 115385 baud, +0.16 %, PCLK group 4 divider 0). The SCB FIFO
holds 128 bytes in byte mode and the largest AM32 packet is 49 bytes, so
`send_telem_DMA()` copies the packet into the TX FIFO and no DMA or
interrupt is needed; a packet that does not fit behind the previous one is
dropped. The SCB resets to MSB first and 16 bit FIFO elements, both are set
to the UART values. The KISS packet code (`Src/kiss_telemetry.c`) is
unchanged.

## Host unit tests

```
make psc3_test HOSTCC=clang
```

`Test/unit` builds `Src/phaseouts.c` against plain memory registers and checks
every commutation step for both `comp_pwm` settings (no shoot through, expected
phase states), plus the PWM, dead time, sound and timer clock arithmetic.
`test_edl7141` runs the 6EDL7141 start-up and fault handling against a
simulated device (frame layout from the datasheet example, configuration
with EN_DRV low and read back, no SPI answer, read back mismatch, charge pumps
not coming up, nFAULT service). On Windows the clang of the Visual Studio
Build Tools works as `HOSTCC`.

## Bring-up images

```
make psc3_boot_stub   # obj/PSC3_BOOT_STUB.hex, 0x12000000
make psc3_blink       # obj/PSC3_BLINK.hex, 0x12001000, LED P7.0 at 1 Hz
```

## Programming and debugging

The build copies `openocd.cfg` and `PSC3M5.svd` to `obj/`, and `obj/debug.elf`
is the last built firmware. The firmware needs the boot stub (or a bootloader)
at 0x12000000.

- J-Link Commander (SEGGER J-Link software V9.84 tested, the kit has a
  J-Link LITE): `JLink -CommanderScript Mcu/psc3/jlink_blink.jlink` loads
  the stub and the blink test, `Mcu/psc3/jlink_flash.jlink` the stub and the
  firmware. J-Link has no PSC3M5 part name, the generic `PSC3xxF` (256 kB)
  works. Its flash loader only takes the S-AHB alias 0x32000000, so the
  scripts load the `.bin` files there (`loadbin`), not the 0x12000000 hex/elf.
- SFLASH: the secure aliases 0x13400000/0x33400000 of the trim rows give a
  bus fault on the kit (found on the first boot, HardFault in `SystemInit`),
  the code reads them through the non-secure alias (`PSC3_SFLASH`,
  0x03400000).
- Pins: all ports come out of reset non-secure (`HSIOM->SECURE_PRT[n].
  NONSECURE_MASK` = all ones) and their GPIO/HSIOM registers then ignore the
  secure CPU (writes lost, reads 0). `SystemClock_Config()` makes all pins
  secure. J-Link reads the peripherals as non-secure, so the secure GPIO
  state is not visible in `mem32`; read it into RAM to check it.
- Infineon OpenOCD (ModusToolbox Programming Tools, upstream OpenOCD has no
  PSC3 target):

  ```
  openocd -s <ProgTools>/openocd/scripts -f Mcu/psc3/openocd.cfg -c "program obj/PSC3_BOOT_STUB.hex" -c "program obj/debug.elf reset exit"
  ```

- VS Code (Cortex-Debug): "AM32 PSC3 JLink" or "AM32 PSC3 OpenOCD" in
  `.vscode/launch.json`, both load the stub and the firmware, with the SVD.

## Kit bring-up: BEMF with the SAR (BEMF_ADC)

Run on the KIT_PSC3M5_2GO with the phase divider rework (AN_B2, AN_B3,
AN_B4 with R21 removed, dividers matched to the DC link ratio within 1 %)
and DShot600 from a flight controller (RP2350B). Findings, all in
`Src/comparator_adc.c` and `Inc/comparator.h`:

- comparator level: AM32 expects the STM32 level, '1' while the floating
  phase is below the neutral (the STM32 targets have the phase on the minus
  input). The emulation and the CSG variant had it inverted, so every second
  step commutated at once and the others never
- sampling point: in the middle of the on time the floating phase also
  carries the voltage the di/dt of the driven windings couples into it (about
  +20 % of VBUS on the kit motor at 5 V, even with the rotor standing still).
  Below 45 % duty with complementary PWM the phases are sampled in the middle
  of the off time and compared with `BEMF_OFF_THRESHOLD` above 0 V, above
  55 % in the on time with VBUS / 2 (`bemfSamplePoint()`, peripherals.h)
- demagnetization: after a commutation the new floating phase is clamped to
  0 V while its current decays, which looks like a finished falling BEMF
  cross. A cross needs the level before it first; if that does not show up
  by the middle of the commutation interval, the cross is taken there
- the first two samples after a commutation are ignored (switching spike)
- `PSC3_BEMF_TRACE` (targets.h) records the floating phase sample, level
  and step of every PWM period of a motor start in `bemf_trace` (8192
  entries, 340 ms) for the debugger

State at the end of the kit tests: the kit motor runs steadily at about 1 %
throttle and loses sync above about 1.2 to 1.8 %. The traces show the steps
after a falling cross much shorter than the others (about 3 ms against
9.5 ms), so AM32 commutates almost at once after a falling cross instead of
30 degrees later; the cause is still open. The kit is a hard bench for six
step BEMF: the motor runs from 5 V, the BEMF is 10 to 25 ADC counts and the
demagnetization is long. The timing work continues on the own ESC (4-8S,
CSG comparators with a real neutral divider).

## Own ESC target (ESC60A_PSC3) and the 6EDL7141

`ESC60A_PSC3` (`HARDWARE_GROUP_PSC3_ESC60` in `Inc/targets.h`) follows the
schematic ESC_PSC3_60A Rev 0.3: PSC3M5FDS2ABQ1 (E-LQFP-48, same pin numbers as
VQFN-48), Infineon 6EDL7141 gate driver, 12x BSC014N06NS, TLI4971 current
sensor. No board exists yet; the target builds and the driver logic is unit
tested, nothing has run on hardware.

- PWM: each phase on the line / line_compl pair of one counter (P4.0/4.1 on
  260, P4.2/4.3 on 261, P4.4/4.5 on 262) into INHx / INLx, so per phase duty
  cycles and the sine start work
- BEMF: CSG comparators with a real neutral (`PSC3_BEMF_CSG`), slice k with
  AN_Bk (phase) against AN_A(k+1) (star of the phases)
- 6EDL7141 over SPI (`Src/edl7141.c`, hooks in `Src/edl7141_hal.c`): SCB3 on
  P6.0 MOSI, P6.1 MISO, P6.2 SCLK, nSCS on P6.3 as a GPIO, EN_DRV P7.1,
  nFAULT P7.2, nBRAKE from P2.2 through a Schottky diode. 24 bit frames (command, 7 bit address, 16 bit data, MSB
  first), sent as three bytes at 2 MHz, CPOL 0 / CPHA 1; a read returns the
  data in the same frame (datasheet Rev 1.04, section 7.1.2)
- start-up (`edl7141Start()` from `enableCorePeripherals()`): the 6EDL7141
  already powers the MCU (DVDD), so it is in STANDBY. DEVICE_ID check, then
  the configuration with EN_DRV low and a read back of every register,
  clear the faults, EN_DRV high and wait for both charge pumps (SUPPLY_ST).
  On any failure EN_DRV stays low, nBRAKE low, and `edl7141_status` says why
  (1 no SPI answer, 2 read back, 3 charge pumps)
- nBRAKE: after the start-up the VSENSE pin (pull-down that selects DVDD
  3.3 V) is the active low brake input (datasheet Rev 1.20, section 3.9.2).
  The MCU drives it high through a Schottky diode just before EN_DRV;
  BRAKE_CFG is High Z (`EDL_CFG_PWM` 0x0020), so when the MCU resets and the
  pull-down takes over the bridge switches off instead of braking
- configuration (`Inc/edl7141.h`, a target can override each value):
  6PWM, PVCC 12 V, dead time 120 ns instead of the 4.04 us reset value (the
  PSC3 PWM inserts DEAD_TIME), gate current 400 mA source / 500 mA sink
  instead of 200 mA (to be tuned on the board), current sense amplifiers off
  (the current comes from the TLI4971)
- faults: `edl7141Service()` in the 1 kHz ADC callback reads FAULT_ST on
  nFAULT into `edl7141_fault_st`, counts it and clears the non latched faults
- retry: after a failed start `edl7141Service()` calls `edl7141Start()` again
  every `EDL_RETRY_MS` (500 ms) while the motor is stopped
  (`edl7141_retries` counts them), for boards where the gate driver gets its
  supply after the MCU
- open: TLI4971-A120T5 sensitivity (`MILLIVOLT_PER_AMP` 10 is a placeholder),
  OTP programming is not used

## KIT_PSC3M5_EVK target (EVK_PSC3)

`EVK_PSC3` (`HARDWARE_GROUP_PSC3_EVK`) runs the ESC60A design on the
KIT_PSC3M5_EVK (PSC3M5FDS2AFQ1, E-LQFP-80) as the controller of a modular DEV
ESC: a power stage with the 6EDL7141 plugs into the EVK headers J5 / J24, a
power input board with the TLI4971 feeds it. Same die as the ESC, so the
pin functions below come from `gpio_psc3_e_lqfp_80.h` (mtb-pdl-cat1 3.23.0)
and the header pins from the EVK schematic 630-60703-01 Rev 5.0.

| Function | MCU | EVK header |
|---|---|---|
| PWM A / B / C high, low | P4.0 / 4.1, P4.2 / 4.3, P4.4 / 4.5 (TCPWM 260-262) | J5-35 / 34, 33 / 32, 36 / 37 |
| 6EDL7141 SDI, nSCS, SCLK, SDO | P7.1, P7.3, P7.0, P7.2 (SCB2, HSIOM 17) | J5-14, 16, 18, 20 |
| EN_DRV, nFAULT, nBRAKE | P9.4, P9.5, P3.0 | J5-11, 9, 24 |
| DShot input | P8.0 (TCPWM 516) | J5-19 (R113) |
| Telemetry TX | P9.3 (SCB0) | J24-3 |
| BEMF A / B / C (CSG +) | AN_B1 / B2 / B3 | J24-18 / 17 / 19 |
| Neutral (CSG -) | AN_A2 / A3 / A4 | J24-27 / 22 / 24 |
| Current, NTC, VBAT | AN_A0, AN_A1, AN_A5 | J24-23, 25, 26 |
| LED | P8.4 (LED2, active low) | - |

What is different from the ESC:
- SPI on SCB2 (Arduino SPI pins): P6.2 / P6.3 are the KitProg3 UART
- EVK rework: R6 off, R7 0R on, so AN_B1 reaches J24-18; by default it goes
  to the HRPWM RC filter (10k, 1 uF)
- AN_B4 is the EVK potentiometer, CAN is the on-board TLE9251V on P5.2 /
  P5.3 (channel 1) with STB on P7.6 (no CAN driver yet)
- VIN_EXP (J5-39, J24-2) is tied to the DC jack through R42 and clamped by
  ESD12VD3B: never more than 12 V
- the power stage is usually powered after the EVK (USB): the 6EDL7141 start
  retry configures it once it answers

Programming: KitProg3 on the USB-C connector with the Infineon OpenOCD and
`Mcu/psc3/openocd_kitprog3.cfg` (as in "Programming and debugging", with this
file instead of `openocd.cfg`), or a J-Link on the 10-pin SWD header J11 with
`jlink_flash.jlink` (use `obj/AM32_EVK_PSC3_2.21.bin`).

## Continuous integration

`.github/workflows/CICD_build.yml`: `make -j8` builds `KIT_2GO_PSC3` with all
other targets (it is in `MCU_TYPES`), and the `psc3-tests` job runs the host
unit tests (`make psc3_test HOSTCC=gcc`) and cppcheck on `Mcu/psc3`.
