#ifndef OST_BINHEX_H
#define OST_BINHEX_H

#include "ost.h"

OST_API ost_status ost_binhex_decode(const uint8_t *data, size_t size, ost_binhex_file *out);
OST_API void ost_binhex_free(ost_binhex_file *file);

#endif
