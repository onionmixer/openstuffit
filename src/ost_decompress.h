#ifndef OST_DECOMPRESS_H
#define OST_DECOMPRESS_H

#include "ost.h"

typedef struct {
    uint8_t *data;
    size_t size;
} ost_decompressed;

ost_status ost_decompress_fork(const uint8_t *data,
                               size_t data_size,
                               const ost_fork_info *fork,
                               ost_decompressed *out);
ost_status ost_decompress_fork_with_password(const uint8_t *data,
                                             size_t data_size,
                                             const ost_fork_info *fork,
                                             const char *password,
                                             ost_decompressed *out);
void ost_decompressed_free(ost_decompressed *out);

#endif
