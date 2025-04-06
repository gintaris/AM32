# Manual Setup Instructions for MB1419 Targets in Keil

Since modifying the project file directly caused issues, here are instructions to manually set up the MB1419 targets in Keil MDK:

## Setup MB1419_G431 Target

1. Open Keil MDK and load the `Keil_Projects/Am32G431.uvprojx` project
2. Right-click on the G431_test target in the Project window
3. Select "Manage Project Items..."
4. Go to the "Target" tab
5. Click the "Add" button at the bottom
6. Name the new target "MB1419_G431"
7. Click OK to create the target
8. With the MB1419_G431 target selected in the dropdown, go to Project → Options for Target
9. On the "Target" tab:
   - Make sure Compiler Version is set to V6.23
   - Set the Output name to "AM32_MB1419_G431"
10. On the "C/C++" tab:
   - In the "Define" box, add `MB1419_G431` to the list of defined symbols
11. Click OK to save the changes

## Setup MB1419_G431_CAN Target

1. Right-click on the MB1419_G431 target you just created
2. Select "Manage Project Items..."
3. Go to the "Target" tab
4. Click the "Add" button
5. Name the new target "MB1419_G431_CAN"
6. Click OK to create the target
7. With the MB1419_G431_CAN target selected in the dropdown, go to Project → Options for Target
8. On the "Target" tab:
   - Make sure Compiler Version is set to V6.23
   - Set the Output name to "AM32_MB1419_G431_CAN"
9. On the "C/C++" tab:
   - In the "Define" box, change `MB1419_G431` to `MB1419_G431_CAN`
10. Click OK to save the changes

## Building the Targets

1. Select the desired target from the dropdown in the toolbar
2. Click Build (F7) to compile the firmware
3. The output files will be created in the Objects directory

## Flashing to the Nucleo Board

Use ST-Link to flash the compiled binary to your MB1419-G431CBU6 Nucleo board:

```
st-flash write Objects/AM32_MB1419_G431.bin 0x8001000     # For standard version
st-flash write Objects/AM32_MB1419_G431_CAN.bin 0x8001000 # For DroneCAN version
``` 