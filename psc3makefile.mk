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

