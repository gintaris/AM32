/*
 * eeprom.c
 *
 * Settings storage in the last flash rows, DP9.
 */

#include "eeprom.h"

#include <string.h>

void read_flash_bin(uint8_t* data, uint32_t add, int out_buff_len)
{
    memcpy(data, (const void*)add, out_buff_len);
}

void save_flash_nolib(uint8_t* data, int length, uint32_t add)
{
    (void)data;
    (void)length;
    (void)add;
}
