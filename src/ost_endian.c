#include "ost_endian.h"

bool ost_read_u16_be(const uint8_t *data, size_t size, size_t off, uint16_t *out) {
    if (!data || !out || off > size || size - off < 2) return false;
    *out = (uint16_t)(((uint16_t)data[off] << 8) | data[off + 1]);
    return true;
}

bool ost_read_u32_be(const uint8_t *data, size_t size, size_t off, uint32_t *out) {
    if (!data || !out || off > size || size - off < 4) return false;
    *out = ((uint32_t)data[off] << 24) |
           ((uint32_t)data[off + 1] << 16) |
           ((uint32_t)data[off + 2] << 8) |
           (uint32_t)data[off + 3];
    return true;
}

bool ost_read_u16_le(const uint8_t *data, size_t size, size_t off, uint16_t *out) {
    if (!data || !out || off > size || size - off < 2) return false;
    *out = (uint16_t)(((uint16_t)data[off + 1] << 8) | data[off]);
    return true;
}

bool ost_read_u32_le(const uint8_t *data, size_t size, size_t off, uint32_t *out) {
    if (!data || !out || off > size || size - off < 4) return false;
    *out = ((uint32_t)data[off + 3] << 24) |
           ((uint32_t)data[off + 2] << 16) |
           ((uint32_t)data[off + 1] << 8) |
           (uint32_t)data[off];
    return true;
}
