#ifndef OST_CRC16_H
#define OST_CRC16_H

#include <stddef.h>
#include <stdint.h>

uint16_t ost_crc16_ibm(const uint8_t *data, size_t len);
uint16_t ost_crc16_ccitt(const uint8_t *data, size_t len);

#endif
