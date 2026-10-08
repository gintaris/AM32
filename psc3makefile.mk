MCU := PSC3
PART := PSC3M5FDS2LGQ1

MCU_LC := $(call lc,$(MCU))

TARGETS_$(MCU) := $(call get_targets,$(MCU))

HAL_FOLDER_$(MCU) := $(HAL_FOLDER)/$(MCU_LC)

MCU_$(MCU) := -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
LDSCRIPT_$(MCU) := $(wildcard $(HAL_FOLDER_$(MCU))/*.ld)

SRC_BASE_DIR_$(MCU) := \
	$(HAL_FOLDER_$(MCU))/Startup

SRC_DIR_$(MCU) := $(SRC_BASE_DIR_$(MCU)) \
	$(HAL_FOLDER_$(MCU))/Src

CFLAGS_$(MCU) := \
	-I$(HAL_FOLDER_$(MCU))/Inc \
	-I$(HAL_FOLDER_$(MCU))/Device \
	-I$(HAL_FOLDER_$(MCU))/Device/ip \
	-I$(HAL_FOLDER_$(MCU))/CMSIS

CFLAGS_$(MCU) += \
	-D$(PART)

SRC_$(MCU) := $(foreach dir,$(SRC_DIR_$(MCU)),$(wildcard $(dir)/*.[cs]))

# PSC3 bring-up images, not AM32 firmware targets:
#  psc3_boot_stub - 4 kB first image that starts the firmware at 0x12001000
#  psc3_blink     - LED blink with the AM32 startup and clock setup
# the rules below must not become the default goal
.DEFAULT_GOAL := all
.PHONY: psc3_boot_stub psc3_blink

PSC3_TOOLS_CFLAGS := $(MCU_PSC3) $(CFLAGS_PSC3) -I$(MAIN_INC_DIR) -DKIT_2GO_PSC3 -Os -g3 \
	-Wall -Wextra -Werror -ffunction-sections --specs=nosys.specs -specs=nano.specs \
	-Wl,--gc-sections -Wl,--print-memory-usage

psc3_boot_stub: obj/PSC3_BOOT_STUB.hex
obj/PSC3_BOOT_STUB.hex: $(HAL_FOLDER_PSC3)/Boot/boot_stub.c $(HAL_FOLDER_PSC3)/Boot/boot_stub.ld
	$(QUIET)$(MKDIR) -p obj
	$(QUIET)$(CC) $(MCU_PSC3) -Os -g3 -Wall -Wextra -Werror -nostdlib -nostartfiles \
		-Wl,--print-memory-usage -T$(HAL_FOLDER_PSC3)/Boot/boot_stub.ld \
		-o $(@:.hex=.elf) $(HAL_FOLDER_PSC3)/Boot/boot_stub.c
	$(QUIET)$(OBJCOPY) -O ihex $(@:.hex=.elf) $@
	$(QUIET)$(OBJCOPY) -O binary $(@:.hex=.elf) $(@:.hex=.bin)

PSC3_BLINK_SRC := $(HAL_FOLDER_PSC3)/Test/blink.c $(HAL_FOLDER_PSC3)/Startup/startup_psc3.c \
	$(HAL_FOLDER_PSC3)/Src/system_psc3.c

psc3_blink: obj/PSC3_BLINK.hex
obj/PSC3_BLINK.hex: $(PSC3_BLINK_SRC) $(LDSCRIPT_PSC3)
	$(QUIET)$(MKDIR) -p obj
	$(QUIET)$(CC) $(PSC3_TOOLS_CFLAGS) -T$(LDSCRIPT_PSC3) -o $(@:.hex=.elf) $(PSC3_BLINK_SRC)
	$(QUIET)$(OBJCOPY) -O ihex $(@:.hex=.elf) $@
	$(QUIET)$(OBJCOPY) -O binary $(@:.hex=.elf) $(@:.hex=.bin)

# host unit tests of the PSC3 MCU layer, set HOSTCC if cc is not a C compiler
.PHONY: psc3_test
psc3_test:
	$(MAKE) -C $(HAL_FOLDER_PSC3)/Test/unit
