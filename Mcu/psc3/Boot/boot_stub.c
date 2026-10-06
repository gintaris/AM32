/*
 * boot_stub.c
 *
 * Minimal first image for the PSC3 until the AM32 bootloader is ported.
 *
 * With the factory boot configuration (LCS NORMAL, BOOT_SIMPLE_APP) the boot
 * ROM launches the plain image at the start of the flash in the secure state,
 * without a header or signature. AM32 keeps the first 4 kB for the
 * bootloader, so this stub sits at 0x12000000 and starts the firmware whose
 * vector table is at 0x12001000.
 */

#include <stdint.h>

#define APP_VECTORS     0x12001000UL
#define SRAM_START      0x34000000UL
#define SRAM_END        0x34010000UL

#define SCB_VTOR        (*(volatile uint32_t*)0xE000ED08UL)

void stub_reset(void);

static void stub_fault(void)
{
    while (1) {
    }
}

__attribute__((used, section(".isr_vector")))
const uint32_t stub_vectors[4] = {
    SRAM_END,
    (uint32_t)stub_reset,
    (uint32_t)stub_fault,       // NMI
    (uint32_t)stub_fault,       // HardFault
};

__attribute__((naked, noreturn)) static void jump(uint32_t sp, uint32_t pc)
{
    __asm volatile(
        "msr msp, %0\n"
        "bx %1\n" ::"r"(sp),
        "r"(pc));
}

void stub_reset(void)
{
    const volatile uint32_t* app = (const volatile uint32_t*)APP_VECTORS;
    const uint32_t sp = app[0];
    const uint32_t pc = app[1];

    // stay here if there is no valid firmware (erased flash reads 0xFFFFFFFF)
    if (sp <= SRAM_START || sp > SRAM_END || (pc & 1UL) == 0 || pc < APP_VECTORS) {
        stub_fault();
    }
    SCB_VTOR = APP_VECTORS;
    __asm volatile("dsb\nisb" ::: "memory");
    jump(sp, pc);
}
