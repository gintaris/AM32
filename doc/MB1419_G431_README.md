# MB1419-G431CBU6 Nucleo Board Support for AM32

This target provides support for the MB1419-G431CBU6 Nucleo board from ST Microelectronics.

## Board Information
- MCU: STM32G431CBU6
- Core: ARM Cortex-M4F
- Flash: 128KB
- RAM: 32KB
- Package: UFQFPN48

## Configuration
The MB1419_G431 target uses the following pin configuration:

- Signal input: PA2 (TIM15_CH1)
- Phase outputs:
  - Phase A: PF0 (Low side), PA10 (High side)
  - Phase B: PB0 (Low side), PA9 (High side)
  - Phase C: PA7 (Low side), PA8 (High side)
- Voltage sensing: PA5 (ADC channel 5)
- Current sensing: PA4 (ADC channel 4)
- UART (Serial telemetry): PA9/PA10 (if using built-in ST-Link)

## DroneCAN Support
The MB1419_G431_CAN target adds support for the DroneCAN protocol using the following pins:
- CAN_RX: PA11
- CAN_TX: PA12

DroneCAN configurations:
- Node name: "com.am32.esc_dev"
- HSE value: 8MHz
- CAN bitrate: 1 Mbps (fixed)

## Building

To build the firmware for this target, use one of the following commands:

```
make MB1419_G431       # Standard version
make MB1419_G431_CAN   # DroneCAN version
```

The compiled binary will be available in the 'obj' directory.

## Flashing

The Nucleo board comes with an integrated ST-Link debugger, so you can flash the firmware directly using:

```
st-flash write obj/AM32_MB1419_G431_X.XX.bin 0x8001000       # Standard version
st-flash write obj/AM32_MB1419_G431_CAN_X.XX.bin 0x8001000   # DroneCAN version
```

Where X.XX is the firmware version.

## Note

This target is based on the HARDWARE_GROUP_G4_A configuration and is intended for evaluation and testing purposes. 