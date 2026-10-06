/*
 * PSC3M5_FLASH.ld
 *
 * AM32 linker script for the PSC3M5 (256 kB flash, 64 kB SRAM). The firmware
 * runs as a single secure image, so the secure aliases are used:
 *   flash 0x12000000 (secure C-AHB), SRAM 0x34000000 (secure S-AHB)
 *
 * Flash map:
 *   0x12000000  4 kB     bootloader (AM32 bootloader or the jump stub)
 *   0x12001000  1 kB     vector table, VTOR needs 1 kB alignment (156 entries)
 *   0x12001400           application
 *   0x1203FBE0  32 B     file name
 *   0x1203FC00  1 kB     EEPROM (two 512 byte flash rows), EEPROM_START_ADD
 */

ENTRY(Reset_Handler)

_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x800;

MEMORY
{
  BOOTLOADER   (rx)  : ORIGIN = 0x12000000, LENGTH = 4K
  FLASH_VECTAB (rx)  : ORIGIN = 0x12001000, LENGTH = 1K
  FLASH        (rx)  : ORIGIN = 0x12001400, LENGTH = 256K - 4K - 1K - 1K - 32
  FILE_NAME    (rx)  : ORIGIN = 0x1203FBE0, LENGTH = 32
  EEPROM       (rx)  : ORIGIN = 0x1203FC00, LENGTH = 1K
  RAM          (rwx) : ORIGIN = 0x34000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH_VECTAB

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  .file_name :
  {
    . = ALIGN(4);
    KEEP (*(.file_name))
    . = ALIGN(4);
  } >FILE_NAME

  _sidata = LOADADDR(.data);

  /* code that runs while the flash is being written (eeprom.c) */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.ramfunc*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
