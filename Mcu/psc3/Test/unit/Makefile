# Host unit tests for the PSC3 MCU layer:
#   make -C Mcu/psc3/Test/unit                 (cc from PATH)
#   make -C Mcu/psc3/Test/unit HOSTCC=clang
# On Windows the clang of the Visual Studio Build Tools works.

HOSTCC ?= cc
ROOT := ../../../..
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -DUSE_MAKE -DKIT_2GO_PSC3 \
	-Istubs -I. -I$(ROOT)/Inc -I$(ROOT)/Mcu/psc3/Inc
OUT := build

TESTS := test_phaseouts test_pwm_math test_timer_math test_comparator test_comparator_adc test_dshot test_telemetry \
	test_edl7141 test_b6 test_b6_am32 test_can_psc3

.PHONY: test clean
test: $(addprefix $(OUT)/,$(addsuffix .exe,$(TESTS)))
	@for t in $^; do ./$$t || exit 1; done

$(OUT)/test_phaseouts.exe: test_phaseouts.c $(ROOT)/Mcu/psc3/Src/phaseouts.c stubs/*.h unit.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS) -o $@ test_phaseouts.c $(ROOT)/Mcu/psc3/Src/phaseouts.c

$(OUT)/test_pwm_math.exe: test_pwm_math.c unit.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS) -o $@ test_pwm_math.c

clean:
	rm -rf $(OUT)

$(OUT)/test_timer_math.exe: test_timer_math.c unit.h $(ROOT)/Mcu/psc3/Inc/psc3_timing.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS) -o $@ test_timer_math.c

$(OUT)/test_comparator.exe: test_comparator.c $(ROOT)/Mcu/psc3/Src/comparator.c stubs/*.h unit.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS) -DPSC3_BEMF_CSG -o $@ test_comparator.c $(ROOT)/Mcu/psc3/Src/comparator.c

$(OUT)/test_comparator_adc.exe: test_comparator_adc.c $(ROOT)/Mcu/psc3/Src/comparator_adc.c stubs/*.h unit.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS) -o $@ test_comparator_adc.c $(ROOT)/Mcu/psc3/Src/comparator_adc.c

$(OUT)/test_dshot.exe: test_dshot.c $(ROOT)/Src/dshot.c stubs/*.h unit.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS) -o $@ test_dshot.c $(ROOT)/Src/dshot.c

$(OUT)/test_telemetry.exe: test_telemetry.c $(ROOT)/Src/kiss_telemetry.c stubs/*.h unit.h $(ROOT)/Mcu/psc3/Inc/psc3_timing.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS) -o $@ test_telemetry.c $(ROOT)/Src/kiss_telemetry.c

$(OUT)/test_edl7141.exe: test_edl7141.c $(ROOT)/Mcu/psc3/Src/edl7141.c $(ROOT)/Mcu/psc3/Inc/edl7141.h unit.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS) -DUSE_6EDL7141 -o $@ test_edl7141.c $(ROOT)/Mcu/psc3/Src/edl7141.c

$(OUT)/test_b6.exe: test_b6.c $(ROOT)/Src/b6_protocol.c $(ROOT)/Inc/b6_protocol.h unit.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS) -DUSE_B6_PROTOCOL -o $@ test_b6.c $(ROOT)/Src/b6_protocol.c

$(OUT)/test_b6_am32.exe: test_b6_am32.c $(ROOT)/Src/b6_am32.c $(ROOT)/Src/b6_protocol.c $(ROOT)/Inc/b6_protocol.h 		$(ROOT)/Inc/b6_am32.h stubs/*.h unit.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS) -DUSE_B6_PROTOCOL -o $@ test_b6_am32.c $(ROOT)/Src/b6_am32.c $(ROOT)/Src/b6_protocol.c

# DroneCAN CAN driver of EVK_PSC3_CAN on the real M_TTCAN register layout
CFLAGS_CAN := $(patsubst -Istubs,-Istubs_can -Istubs,$(filter-out -DKIT_2GO_PSC3,$(CFLAGS))) -DEVK_PSC3_CAN -DCANARD_ENABLE_CANFD=1 -I$(ROOT)/Mcu/psc3/Device -I$(ROOT)/Src/DroneCAN -I$(ROOT)/Src/DroneCAN/libcanard
$(OUT)/test_can_psc3.exe: test_can_psc3.c $(ROOT)/Src/DroneCAN/sys_can_psc3.c stubs_can/peripherals.h unit.h $(ROOT)/Inc/targets.h
	@mkdir -p $(OUT)
	"$(HOSTCC)" $(CFLAGS_CAN) -o $@ test_can_psc3.c $(ROOT)/Src/DroneCAN/sys_can_psc3.c
