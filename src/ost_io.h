#ifndef OST_IO_H
#define OST_IO_H

#include "ost.h"

OST_API ost_status ost_read_file(const char *path, ost_buffer *out);
OST_API void ost_buffer_free(ost_buffer *buffer);
OST_API const char *ost_basename_const(const char *path);

#endif
