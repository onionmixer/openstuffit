#ifndef OST_DETECT_H
#define OST_DETECT_H

#include "ost.h"

OST_API ost_status ost_detect_buffer(const uint8_t *data, size_t size, const char *name, ost_detection *out);
bool ost_is_sit_classic_at(const uint8_t *data, size_t size, size_t off);
bool ost_is_sit5_at(const uint8_t *data, size_t size, size_t off);
bool ost_is_sitx_at(const uint8_t *data, size_t size, size_t off);
bool ost_is_pe_sfx_at(const uint8_t *data, size_t size, size_t off);

#endif
