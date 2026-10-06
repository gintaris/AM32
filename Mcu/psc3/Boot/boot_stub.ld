/* boot_stub.ld, the stub lives in the 4 kB bootloader area */
ENTRY(stub_reset)

MEMORY
{
  BOOTLOADER (rx)  : ORIGIN = 0x12000000, LENGTH = 4K
  RAM        (rw)  : ORIGIN = 0x34000000, LENGTH = 64K
}

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } >BOOTLOADER
  .text : { *(.text*) *(.rodata*) } >BOOTLOADER
  .data : { *(.data*) } >RAM AT> BOOTLOADER
  .bss (NOLOAD) : { *(.bss*) *(COMMON) } >RAM
}
