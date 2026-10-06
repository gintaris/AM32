/*
 * eeprom.c
 *
 * Settings storage in a 512 byte flash row at EEPROM_START_ADD.
 *
 * PSC3 flash rows are written by the boot ROM flash API (cyboot_flash_list.h,
 * as Cy_Flash_WriteRow in the PDL does for the MXS40FLASHC): a blocking
 * write row erases and programs one row and computes the column 33 refresh
 * data. The flash has two 128 kB sectors with read while write only between
 * them, so the settings row sits in sector 1 and the firmware must stay in
 * sector 0 (checked by PSC3M5_FLASH.ld). The cache prefetch is switched off
 * during the write (TRM, read while write) and the cache is invalidated
 * afterwards because the firmware reads the settings through the cached
 * code bus alias. Erased flash reads 0x00 (ERASED_FLASH_BYTE).
 */

#include "eeprom.h"

#include <string.h>

#include "cyboot_flash_list.h"
#include "targets.h"

#define FLASH_SBUS_ALIAS(addr)  ((addr) | 0x20000000UL)    // 0x12.. code bus -> 0x32.. system bus

// the ROM updates the refresh structure on every write, even without refresh
static cyboot_flash_refresh_t flash_refresh[CYBOOT_N_FLASH_SECTORS];
static cyboot_flash_context_t flash_ctx;
static cyboot_flash_row_t row_buffer;

// result of the last write, CYBOOT_FLASH_SUCCESS when it worked
volatile uint32_t eeprom_write_status;

// the ROM flash driver interrupt (also used by blocking operations)
void cpuss_interrupt_fm_cbus_IRQHandler(void)
{
    ROM_FUNC->cyboot_flash_irq_handler();
}

void read_flash_bin(uint8_t* data, uint32_t add, int out_buff_len)
{
    memcpy(data, (const void*)add, out_buff_len);
}

void save_flash_nolib(uint8_t* data, int length, uint32_t add)
{
    const uint32_t row = add & ~(CY_FLASH_ROW_SIZE - 1UL);
    const uint32_t offset = add - row;

    if (length <= 0 || offset + (uint32_t)length > CY_FLASH_ROW_SIZE) {
        eeprom_write_status = CYBOOT_FLASH_PARAM_INVALID;
        return;
    }

    // keep the rest of the row
    memcpy(row_buffer, (const void*)row, CY_FLASH_ROW_SIZE);
    memcpy((uint8_t*)row_buffer + offset, data, (uint32_t)length);
    memset((uint8_t*)row_buffer + CY_FLASH_ROW_SIZE, 0, CYBOOT_FLASH_COLUMN33_SIZE);

    flash_ctx.flags = 0;            // blocking, column 33 computed by the ROM
    flash_ctx.hv_params_addr = 0;   // programming parameters from SFLASH
    flash_ctx.refresh = flash_refresh;

    NVIC_SetPriority(cpuss_interrupt_fm_cbus_IRQn, 7);
    NVIC_EnableIRQ(cpuss_interrupt_fm_cbus_IRQn);

    const uint32_t prefetch = ICACHE0->CTL & ICACHE_CTL_PREF_EN_Msk;
    ICACHE0->CTL &= ~ICACHE_CTL_PREF_EN_Msk;

    eeprom_write_status = ROM_FUNC->cyboot_flash_write_row(FLASH_SBUS_ALIAS(row), row_buffer, &flash_ctx);

    ICACHE0->CMD = ICACHE_CMD_INV_Msk;
    while (ICACHE0->CMD & ICACHE_CMD_INV_Msk) {
    }
    ICACHE0->CTL |= prefetch;
    __DSB();
    __ISB();
}
