#ifndef OST_SIT13_H
#define OST_SIT13_H

#include "ost.h"

ost_status ost_sit13_decompress(const uint8_t *src, size_t src_size, size_t out_size, uint8_t **out, size_t *actual);

#endif
