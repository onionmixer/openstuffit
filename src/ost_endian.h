#ifndef OST_ENDIAN_H
#define OST_ENDIAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ost_read_u16_be(const uint8_t *data, size_t size, size_t off, uint16_t *out);
bool ost_read_u32_be(const uint8_t *data, size_t size, size_t off, uint32_t *out);
bool ost_read_u16_le(const uint8_t *data, size_t size, size_t off, uint16_t *out);
bool ost_read_u32_le(const uint8_t *data, size_t size, size_t off, uint32_t *out);

#endif
