#ifndef OST_WRITE_H
#define OST_WRITE_H

#include "ost.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *output_path;
    const char *const *input_paths;
    size_t input_path_count;
    bool follow_links;
    uint8_t default_type[4];
    uint8_t default_creator[4];
} ost_create_options;

void ost_create_options_init(ost_create_options *options);
ost_status ost_write_sit_classic(const ost_create_options *options);

#endif
