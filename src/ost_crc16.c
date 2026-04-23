#include "ost_crc16.h"

uint16_t ost_crc16_ibm(const uint8_t *data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 1) != 0) crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else crc >>= 1;
        }
    }
    return crc;
}

uint16_t ost_crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data[i] << 8));
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000u) != 0) crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
            else crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}
