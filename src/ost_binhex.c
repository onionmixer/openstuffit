#include "ost_binhex.h"

#include "ost_crc16.h"
#include "ost_endian.h"
#include "ost_macroman.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    unsigned bytes;
    uint8_t prev_bits;
    uint8_t rle_byte;
    unsigned rle_num;
    bool eof;
} hqx_reader;

static int hqx_code_value(uint8_t c) {
    static const char codes[] = "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";
    for (int i = 0; i < 64; i++) {
        if ((uint8_t)codes[i] == c) return i;
    }
    return -1;
}

static bool find_hqx_start(const uint8_t *data, size_t size, size_t *start) {
    static const char marker[] = "(This file must be converted with BinHex";
    size_t marker_len = sizeof(marker) - 1u;
    if (!data || !start || size < marker_len) return false;
    for (size_t i = 0; i <= size - marker_len; i++) {
        if (memcmp(data + i, marker, marker_len) != 0) continue;
        size_t p = i + marker_len;
        while (p < size && data[p] != '\n' && data[p] != '\r') p++;
        while (p < size && (data[p] == '\n' || data[p] == '\r' || data[p] == '\t' || data[p] == ' ')) p++;
        if (p < size && data[p] == ':') {
            *start = p + 1u;
            return true;
        }
    }
    return false;
}

static bool hqx_get_bits(hqx_reader *rd, uint8_t *out) {
    while (rd->pos < rd->size) {
        uint8_t c = rd->data[rd->pos++];
        if (c == ':') {
            rd->eof = true;
            return false;
        }
        int v = hqx_code_value(c);
        if (v >= 0) {
            *out = (uint8_t)v;
            return true;
        }
    }
    rd->eof = true;
    return false;
}

static bool hqx_decode_byte(hqx_reader *rd, uint8_t *out) {
    uint8_t bits1 = 0, bits2 = 0;
    switch (rd->bytes++ % 3u) {
        case 0:
            if (!hqx_get_bits(rd, &bits1) || !hqx_get_bits(rd, &bits2)) return false;
            rd->prev_bits = bits2;
            *out = (uint8_t)((bits1 << 2u) | (bits2 >> 4u));
            return true;
        case 1:
            bits1 = rd->prev_bits;
            if (!hqx_get_bits(rd, &bits2)) return false;
            rd->prev_bits = bits2;
            *out = (uint8_t)((bits1 << 4u) | (bits2 >> 2u));
            return true;
        default:
            bits1 = rd->prev_bits;
            if (!hqx_get_bits(rd, &bits2)) return false;
            *out = (uint8_t)((bits1 << 6u) | bits2);
            return true;
    }
}

static bool hqx_read_rle(hqx_reader *rd, uint8_t *out) {
    if (rd->rle_num > 0) {
        rd->rle_num--;
        *out = rd->rle_byte;
        return true;
    }

    uint8_t byte = 0;
    if (!hqx_decode_byte(rd, &byte)) return false;
    if (byte != 0x90u) {
        rd->rle_byte = byte;
        *out = byte;
        return true;
    }

    uint8_t count = 0;
    if (!hqx_decode_byte(rd, &count)) return false;
    if (count == 0) {
        rd->rle_byte = 0x90u;
        *out = 0x90u;
        return true;
    }
    if (count == 1) return false;
    rd->rle_num = (unsigned)count - 2u;
    *out = rd->rle_byte;
    return true;
}

static ost_status append_byte(uint8_t **data, size_t *size, size_t *cap, uint8_t b) {
    if (*size == *cap) {
        size_t next = *cap ? *cap * 2u : 4096u;
        uint8_t *p = (uint8_t *)realloc(*data, next);
        if (!p) return OST_ERR_NO_MEMORY;
        *data = p;
        *cap = next;
    }
    (*data)[(*size)++] = b;
    return OST_OK;
}

static void free_partial(ost_binhex_file *file) {
    if (!file) return;
    free(file->name);
    free(file->data_fork.data);
    free(file->resource_fork.data);
    memset(file, 0, sizeof(*file));
}

ost_status ost_binhex_decode(const uint8_t *data, size_t size, ost_binhex_file *out) {
    if (!data || !out) return OST_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));

    size_t start = 0;
    if (!find_hqx_start(data, size, &start)) return OST_ERR_UNSUPPORTED;

    hqx_reader rd;
    memset(&rd, 0, sizeof(rd));
    rd.data = data;
    rd.size = size;
    rd.pos = start;

    uint8_t *decoded = NULL;
    size_t decoded_size = 0, decoded_cap = 0;
    uint8_t b = 0;
    while (hqx_read_rle(&rd, &b)) {
        ost_status st = append_byte(&decoded, &decoded_size, &decoded_cap, b);
        if (st != OST_OK) {
            free(decoded);
            return st;
        }
    }
    if (!rd.eof || decoded_size < 22u) {
        free(decoded);
        return OST_ERR_BAD_FORMAT;
    }

    uint8_t name_len = decoded[0];
    if (name_len < 1u || name_len > 63u || decoded_size < (size_t)name_len + 22u) {
        free(decoded);
        return OST_ERR_BAD_FORMAT;
    }

    out->name = ost_macroman_to_utf8(decoded + 1, name_len);
    if (!out->name) {
        free(decoded);
        return OST_ERR_NO_MEMORY;
    }

    size_t p = 1u + name_len;
    p++; /* version */
    memcpy(out->finder_type, decoded + p, 4);
    p += 4;
    memcpy(out->finder_creator, decoded + p, 4);
    p += 4;
    if (!ost_read_u16_be(decoded, decoded_size, p, &out->finder_flags)) {
        free(decoded);
        free_partial(out);
        return OST_ERR_BAD_FORMAT;
    }
    p += 2;
    uint32_t data_len = 0, rsrc_len = 0;
    if (!ost_read_u32_be(decoded, decoded_size, p, &data_len) || !ost_read_u32_be(decoded, decoded_size, p + 4u, &rsrc_len)) {
        free(decoded);
        free_partial(out);
        return OST_ERR_BAD_FORMAT;
    }
    p += 8u;
    uint16_t stored_crc = 0;
    if (!ost_read_u16_be(decoded, decoded_size, p, &stored_crc)) {
        free(decoded);
        free_partial(out);
        return OST_ERR_BAD_FORMAT;
    }
    if (ost_crc16_ccitt(decoded, p) != stored_crc) {
        free(decoded);
        free_partial(out);
        return OST_ERR_CHECKSUM;
    }
    p += 2u;

    if (p > decoded_size || data_len > decoded_size - p) {
        free(decoded);
        free_partial(out);
        return OST_ERR_BAD_FORMAT;
    }
    if (data_len > 0) {
        out->data_fork.data = (uint8_t *)malloc(data_len);
        if (!out->data_fork.data) {
            free(decoded);
            free_partial(out);
            return OST_ERR_NO_MEMORY;
        }
        memcpy(out->data_fork.data, decoded + p, data_len);
        out->data_fork.size = data_len;
    }
    uint16_t actual_data_crc = ost_crc16_ccitt(decoded + p, data_len);
    p += data_len;
    if (p + 2u > decoded_size) {
        free(decoded);
        free_partial(out);
        return OST_ERR_BAD_FORMAT;
    }
    if (!ost_read_u16_be(decoded, decoded_size, p, &stored_crc)) {
        free(decoded);
        free_partial(out);
        return OST_ERR_BAD_FORMAT;
    }
    if (actual_data_crc != stored_crc) {
        free(decoded);
        free_partial(out);
        return OST_ERR_CHECKSUM;
    }
    p += 2u;

    if (p > decoded_size || rsrc_len > decoded_size - p) {
        free(decoded);
        free_partial(out);
        return OST_ERR_BAD_FORMAT;
    }
    size_t rsrc_start = p;
    if (rsrc_len > 0) {
        out->resource_fork.data = (uint8_t *)malloc(rsrc_len);
        if (!out->resource_fork.data) {
            free(decoded);
            free_partial(out);
            return OST_ERR_NO_MEMORY;
        }
        memcpy(out->resource_fork.data, decoded + p, rsrc_len);
        out->resource_fork.size = rsrc_len;
    }
    p += rsrc_len;
    if (p + 2u > decoded_size) {
        free(decoded);
        free_partial(out);
        return OST_ERR_BAD_FORMAT;
    }
    if (!ost_read_u16_be(decoded, decoded_size, p, &stored_crc)) {
        free(decoded);
        free_partial(out);
        return OST_ERR_BAD_FORMAT;
    }
    if (ost_crc16_ccitt(decoded + rsrc_start, rsrc_len) != stored_crc) {
        free(decoded);
        free_partial(out);
        return OST_ERR_CHECKSUM;
    }

    free(decoded);
    return OST_OK;
}

void ost_binhex_free(ost_binhex_file *file) {
    free_partial(file);
}
