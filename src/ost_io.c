#include "ost_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ost_status ost_read_file(const char *path, ost_buffer *out) {
    if (!path || !out) return OST_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(path, "rb");
    if (!fp) return OST_ERR_IO;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return OST_ERR_IO;
    }
    long end = ftell(fp);
    if (end < 0) {
        fclose(fp);
        return OST_ERR_IO;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return OST_ERR_IO;
    }

    size_t size = (size_t)end;
    uint8_t *data = NULL;
    if (size > 0) {
        data = (uint8_t *)malloc(size);
        if (!data) {
            fclose(fp);
            return OST_ERR_NO_MEMORY;
        }
        if (fread(data, 1, size, fp) != size) {
            free(data);
            fclose(fp);
            return OST_ERR_IO;
        }
    }
    fclose(fp);

    out->data = data;
    out->size = size;
    return OST_OK;
}

void ost_buffer_free(ost_buffer *buffer) {
    if (!buffer) return;
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
}

const char *ost_basename_const(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
