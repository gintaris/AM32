/*
 * Host test for the telemetry: the shared KISS packet (Src/kiss_telemetry.c)
 * that serial_telemetry.c puts into the SCB FIFO, and the UART bit rate of
 * psc3_timing.h.
 */

#include <string.h>

#include "eeprom.h"
#include "kiss_telemetry.h"
#include "psc3_timing.h"
#include "unit.h"

EEprom_t eepromBuffer;

// KISS / BLHeli telemetry CRC8 (polynomial 0x07), as get_crc8() in functions.c
uint8_t get_crc8(uint8_t* buf, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t b = buf[i];
        for (int j = 0; j < 8; j++) {
            crc = ((crc ^ b) & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
            b = (uint8_t)(b << 1);
        }
    }
    return crc;
}

int main(void)
{
    // UART: 180 MHz / divider / 13 oversampling within 1 % of 115200
    const double baud = (double)PSC3_COUNTER_CLOCK_HZ / PCLK_DIVIDE_UART / TELEM_UART_OVS;
    CHECK(baud > TELEM_BAUD * 0.99 && baud < TELEM_BAUD * 1.01, "baud %.0f", baud);
    CHECK(PCLK_DIVIDE_UART >= 1 && PCLK_DIVIDE_UART <= 65536, "16 bit divider");
    CHECK(TELEM_UART_OVS >= 8 && TELEM_UART_OVS <= 16, "SCB oversampling range");

    // 10 byte KISS packet, big endian fields, CRC last
    makeTelemPackage(37, 0x0612, 0x0345, 0x0102, 0x2710);
    static const uint8_t expect[9] = { 37, 0x06, 0x12, 0x03, 0x45, 0x01, 0x02, 0x27, 0x10 };
    CHECK(memcmp(aTxBuffer, expect, 9) == 0, "KISS payload layout");
    CHECK(aTxBuffer[9] == get_crc8(aTxBuffer, 9), "KISS CRC");
    CHECK(sizeof(kiss_telem_pkt_t) == 10, "KISS packet size");

    // ESC info packet: 48 settings bytes and CRC, fits the 128 byte SCB FIFO
    for (int i = 0; i < 48; i++) {
        eepromBuffer.buffer[i] = (uint8_t)(i * 3);
    }
    makeInfoPacket();
    CHECK(aTxBuffer[47] == 141 && aTxBuffer[48] == get_crc8(aTxBuffer, 48), "ESC info packet");
    CHECK(sizeof(aTxBuffer) <= 128, "largest packet fits the SCB TX FIFO");

    return report("test_telemetry");
}
