#ifndef OST_UNICODE_H
#define OST_UNICODE_H

#include "ost.h"

OST_API ost_status ost_normalize_utf8(const char *input, ost_unicode_normalization mode, char **output);
OST_API const char *ost_unicode_normalization_name(ost_unicode_normalization mode);
OST_API bool ost_parse_unicode_normalization(const char *name, ost_unicode_normalization *mode);

#endif
