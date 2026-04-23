#ifndef OST_DUMP_H
#define OST_DUMP_H

#include "ost.h"

ost_status ost_dump_headers(const uint8_t *data, size_t size, const ost_detection *det, bool json);
ost_status ost_dump_forks(const ost_detection *det, bool json);
ost_status ost_dump_entry(const ost_archive *archive, const char *selector, bool json);
ost_status ost_dump_hex(const uint8_t *data, size_t size, uint64_t offset, uint64_t length, bool json);

#endif
