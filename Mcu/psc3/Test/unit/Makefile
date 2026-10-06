# Host unit tests for the PSC3 MCU layer:
#   make -C Mcu/psc3/Test/unit                 (cc from PATH)
#   make -C Mcu/psc3/Test/unit HOSTCC=clang
# On Windows the clang of the Visual Studio Build Tools works.

HOSTCC ?= cc
ROOT := ../../../..
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -DUSE_MAKE -DKIT_2GO_PSC3 \
	-Istubs -I. -I$(ROOT)/Inc -I$(ROOT)/Mcu/psc3/Inc
OUT := build

TESTS := test_phaseouts test_pwm_math test_timer_math

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
