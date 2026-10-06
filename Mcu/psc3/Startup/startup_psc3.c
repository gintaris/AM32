/*
 * startup_psc3.c
 *
 * Reset handler and vector table for the PSC3M5 (Cortex-M33). Written for
 * AM32, the IRQ list follows the PSC3M5FDS2LGQ1 device header.
 *
 * The image runs in the secure state. Reset_Handler sets VTOR, the stack
 * limit and the FPU access, copies .data, clears .bss and calls main().
 */

#include "main.h"
#include "peripherals.h"

extern uint32_t _estack;
extern uint32_t _end;
// section limits from the linker script, arrays of unknown size
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[];

int main(void);
void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SecureFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

void ioss_interrupts_gpio_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_gpio_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_gpio_2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_gpio_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_gpio_4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_gpio_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_gpio_6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_gpio_7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_gpio_8_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_gpio_9_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_sec_gpio_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_sec_gpio_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_sec_gpio_2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_sec_gpio_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_sec_gpio_4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_sec_gpio_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_sec_gpio_6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_sec_gpio_7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_sec_gpio_8_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupts_sec_gpio_9_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupt_vdd_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupt_gpio_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ioss_interrupt_sec_gpio_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void scb_0_interrupt_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void srss_interrupt_mcwdt_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void srss_interrupt_backup_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_ipc_dpslp_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_ipc_dpslp_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void srss_interrupt_wdt_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void lpcomp_interrupt_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void srss_interrupt_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void scb_1_interrupt_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void scb_2_interrupt_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void scb_3_interrupt_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void scb_4_interrupt_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void scb_5_interrupt_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupt_fm_cbus_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_8_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_9_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_10_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_11_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_12_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_13_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_14_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw0_15_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_8_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_9_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_10_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_11_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_12_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_13_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_14_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_dw1_15_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupt_ppu_sramc0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupt_cm33_0_fp_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_cm33_0_cti_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_cm33_0_cti_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupts_fault_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupt_ppu_cpuss_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cpuss_interrupt_msc_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_256_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_257_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_258_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_259_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_260_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_261_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_262_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_263_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_512_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_513_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_514_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_515_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_516_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_517_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_518_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_interrupts_519_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void srss_interrupt_main_ppu_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cryptolite_interrupt_error_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cryptolite_interrupt_trng_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void canfd_0_interrupts0_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void canfd_0_interrupts1_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void canfd_0_interrupts0_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void canfd_0_interrupts1_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void canfd_0_interrupt0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void canfd_0_interrupt1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void cordic_interrupt_mxcordic_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_motif_interrupt_64_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void tcpwm_0_motif_interrupt_65_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_mcpass_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_results_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_entry_done_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_entry_done_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_entry_done_2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_entry_done_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_entry_done_4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_entry_done_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_entry_done_6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_entry_done_7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_fir_done_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_fir_done_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_ranges_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_range_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_range_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_range_2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_range_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_range_4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_range_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_range_6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_sar_range_7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_csg_dacs_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_csg_dac_0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_csg_dac_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_csg_dac_2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_csg_dac_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_csg_dac_4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_csg_dac_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_csg_dac_6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_csg_dac_7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_csg_cmps_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void pass_interrupt_fifos_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

typedef void (*vector_t)(void);

__attribute__((used, section(".isr_vector")))
const vector_t vector_table[] = {
    (vector_t)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    SecureFault_Handler,
    0,
    0,
    0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
    ioss_interrupts_gpio_0_IRQHandler, // 0
    ioss_interrupts_gpio_1_IRQHandler, // 1
    ioss_interrupts_gpio_2_IRQHandler, // 2
    ioss_interrupts_gpio_3_IRQHandler, // 3
    ioss_interrupts_gpio_4_IRQHandler, // 4
    ioss_interrupts_gpio_5_IRQHandler, // 5
    ioss_interrupts_gpio_6_IRQHandler, // 6
    ioss_interrupts_gpio_7_IRQHandler, // 7
    ioss_interrupts_gpio_8_IRQHandler, // 8
    ioss_interrupts_gpio_9_IRQHandler, // 9
    ioss_interrupts_sec_gpio_0_IRQHandler, // 10
    ioss_interrupts_sec_gpio_1_IRQHandler, // 11
    ioss_interrupts_sec_gpio_2_IRQHandler, // 12
    ioss_interrupts_sec_gpio_3_IRQHandler, // 13
    ioss_interrupts_sec_gpio_4_IRQHandler, // 14
    ioss_interrupts_sec_gpio_5_IRQHandler, // 15
    ioss_interrupts_sec_gpio_6_IRQHandler, // 16
    ioss_interrupts_sec_gpio_7_IRQHandler, // 17
    ioss_interrupts_sec_gpio_8_IRQHandler, // 18
    ioss_interrupts_sec_gpio_9_IRQHandler, // 19
    ioss_interrupt_vdd_IRQHandler, // 20
    ioss_interrupt_gpio_IRQHandler, // 21
    ioss_interrupt_sec_gpio_IRQHandler, // 22
    scb_0_interrupt_IRQHandler, // 23
    srss_interrupt_mcwdt_0_IRQHandler, // 24
    srss_interrupt_backup_IRQHandler, // 25
    cpuss_interrupts_ipc_dpslp_0_IRQHandler, // 26
    cpuss_interrupts_ipc_dpslp_1_IRQHandler, // 27
    srss_interrupt_wdt_IRQHandler, // 28
    lpcomp_interrupt_IRQHandler, // 29
    srss_interrupt_IRQHandler, // 30
    scb_1_interrupt_IRQHandler, // 31
    scb_2_interrupt_IRQHandler, // 32
    scb_3_interrupt_IRQHandler, // 33
    scb_4_interrupt_IRQHandler, // 34
    scb_5_interrupt_IRQHandler, // 35
    cpuss_interrupt_fm_cbus_IRQHandler, // 36
    cpuss_interrupts_dw0_0_IRQHandler, // 37
    cpuss_interrupts_dw0_1_IRQHandler, // 38
    cpuss_interrupts_dw0_2_IRQHandler, // 39
    cpuss_interrupts_dw0_3_IRQHandler, // 40
    cpuss_interrupts_dw0_4_IRQHandler, // 41
    cpuss_interrupts_dw0_5_IRQHandler, // 42
    cpuss_interrupts_dw0_6_IRQHandler, // 43
    cpuss_interrupts_dw0_7_IRQHandler, // 44
    cpuss_interrupts_dw0_8_IRQHandler, // 45
    cpuss_interrupts_dw0_9_IRQHandler, // 46
    cpuss_interrupts_dw0_10_IRQHandler, // 47
    cpuss_interrupts_dw0_11_IRQHandler, // 48
    cpuss_interrupts_dw0_12_IRQHandler, // 49
    cpuss_interrupts_dw0_13_IRQHandler, // 50
    cpuss_interrupts_dw0_14_IRQHandler, // 51
    cpuss_interrupts_dw0_15_IRQHandler, // 52
    cpuss_interrupts_dw1_0_IRQHandler, // 53
    cpuss_interrupts_dw1_1_IRQHandler, // 54
    cpuss_interrupts_dw1_2_IRQHandler, // 55
    cpuss_interrupts_dw1_3_IRQHandler, // 56
    cpuss_interrupts_dw1_4_IRQHandler, // 57
    cpuss_interrupts_dw1_5_IRQHandler, // 58
    cpuss_interrupts_dw1_6_IRQHandler, // 59
    cpuss_interrupts_dw1_7_IRQHandler, // 60
    cpuss_interrupts_dw1_8_IRQHandler, // 61
    cpuss_interrupts_dw1_9_IRQHandler, // 62
    cpuss_interrupts_dw1_10_IRQHandler, // 63
    cpuss_interrupts_dw1_11_IRQHandler, // 64
    cpuss_interrupts_dw1_12_IRQHandler, // 65
    cpuss_interrupts_dw1_13_IRQHandler, // 66
    cpuss_interrupts_dw1_14_IRQHandler, // 67
    cpuss_interrupts_dw1_15_IRQHandler, // 68
    cpuss_interrupt_ppu_sramc0_IRQHandler, // 69
    cpuss_interrupt_cm33_0_fp_IRQHandler, // 70
    cpuss_interrupts_cm33_0_cti_0_IRQHandler, // 71
    cpuss_interrupts_cm33_0_cti_1_IRQHandler, // 72
    cpuss_interrupts_fault_0_IRQHandler, // 73
    cpuss_interrupt_ppu_cpuss_IRQHandler, // 74
    cpuss_interrupt_msc_IRQHandler, // 75
    tcpwm_0_interrupts_0_IRQHandler, // 76
    tcpwm_0_interrupts_1_IRQHandler, // 77
    tcpwm_0_interrupts_2_IRQHandler, // 78
    tcpwm_0_interrupts_3_IRQHandler, // 79
    tcpwm_0_interrupts_256_IRQHandler, // 80
    tcpwm_0_interrupts_257_IRQHandler, // 81
    tcpwm_0_interrupts_258_IRQHandler, // 82
    tcpwm_0_interrupts_259_IRQHandler, // 83
    tcpwm_0_interrupts_260_IRQHandler, // 84
    tcpwm_0_interrupts_261_IRQHandler, // 85
    tcpwm_0_interrupts_262_IRQHandler, // 86
    tcpwm_0_interrupts_263_IRQHandler, // 87
    tcpwm_0_interrupts_512_IRQHandler, // 88
    tcpwm_0_interrupts_513_IRQHandler, // 89
    tcpwm_0_interrupts_514_IRQHandler, // 90
    tcpwm_0_interrupts_515_IRQHandler, // 91
    tcpwm_0_interrupts_516_IRQHandler, // 92
    tcpwm_0_interrupts_517_IRQHandler, // 93
    tcpwm_0_interrupts_518_IRQHandler, // 94
    tcpwm_0_interrupts_519_IRQHandler, // 95
    srss_interrupt_main_ppu_IRQHandler, // 96
    cryptolite_interrupt_error_IRQHandler, // 97
    cryptolite_interrupt_trng_IRQHandler, // 98
    canfd_0_interrupts0_0_IRQHandler, // 99
    canfd_0_interrupts1_0_IRQHandler, // 100
    canfd_0_interrupts0_1_IRQHandler, // 101
    canfd_0_interrupts1_1_IRQHandler, // 102
    canfd_0_interrupt0_IRQHandler, // 103
    canfd_0_interrupt1_IRQHandler, // 104
    cordic_interrupt_mxcordic_IRQHandler, // 105
    tcpwm_0_motif_interrupt_64_IRQHandler, // 106
    tcpwm_0_motif_interrupt_65_IRQHandler, // 107
    pass_interrupt_mcpass_IRQHandler, // 108
    pass_interrupt_sar_results_IRQHandler, // 109
    pass_interrupt_sar_entry_done_0_IRQHandler, // 110
    pass_interrupt_sar_entry_done_1_IRQHandler, // 111
    pass_interrupt_sar_entry_done_2_IRQHandler, // 112
    pass_interrupt_sar_entry_done_3_IRQHandler, // 113
    pass_interrupt_sar_entry_done_4_IRQHandler, // 114
    pass_interrupt_sar_entry_done_5_IRQHandler, // 115
    pass_interrupt_sar_entry_done_6_IRQHandler, // 116
    pass_interrupt_sar_entry_done_7_IRQHandler, // 117
    pass_interrupt_sar_fir_done_0_IRQHandler, // 118
    pass_interrupt_sar_fir_done_1_IRQHandler, // 119
    pass_interrupt_sar_ranges_IRQHandler, // 120
    pass_interrupt_sar_range_0_IRQHandler, // 121
    pass_interrupt_sar_range_1_IRQHandler, // 122
    pass_interrupt_sar_range_2_IRQHandler, // 123
    pass_interrupt_sar_range_3_IRQHandler, // 124
    pass_interrupt_sar_range_4_IRQHandler, // 125
    pass_interrupt_sar_range_5_IRQHandler, // 126
    pass_interrupt_sar_range_6_IRQHandler, // 127
    pass_interrupt_sar_range_7_IRQHandler, // 128
    pass_interrupt_csg_dacs_IRQHandler, // 129
    pass_interrupt_csg_dac_0_IRQHandler, // 130
    pass_interrupt_csg_dac_1_IRQHandler, // 131
    pass_interrupt_csg_dac_2_IRQHandler, // 132
    pass_interrupt_csg_dac_3_IRQHandler, // 133
    pass_interrupt_csg_dac_4_IRQHandler, // 134
    pass_interrupt_csg_dac_5_IRQHandler, // 135
    pass_interrupt_csg_dac_6_IRQHandler, // 136
    pass_interrupt_csg_dac_7_IRQHandler, // 137
    pass_interrupt_csg_cmps_IRQHandler, // 138
    pass_interrupt_fifos_IRQHandler, // 139
};

void Reset_Handler(void)
{
    __disable_irq();

    // the boot ROM may leave VTOR pointing at its own table
    SCB->VTOR = (uint32_t)vector_table;
    __set_MSPLIM((uint32_t)&_end);

    // full access to the FPU (CP10 / CP11)
    SCB->CPACR |= (3UL << 20) | (3UL << 22);
    __DSB();
    __ISB();

    // the linker symbols are separate objects, so count words instead of
    // comparing their addresses
    const uint32_t data_words = ((uintptr_t)_edata - (uintptr_t)_sdata) / 4U;
    const uint32_t bss_words = ((uintptr_t)_ebss - (uintptr_t)_sbss) / 4U;
    for (uint32_t i = 0; i < data_words; i++) {
        _sdata[i] = _sidata[i];
    }
    for (uint32_t i = 0; i < bss_words; i++) {
        _sbss[i] = 0;
    }

    SystemInit();
    __enable_irq();
    main();
    while (1) {
    }
}

void Default_Handler(void)
{
    while (1) {
    }
}
