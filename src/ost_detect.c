#include "ost_detect.h"

#include "ost_crc16.h"
#include "ost_endian.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static uint64_t round_up_128(uint64_t value) {
    return (value + 127u) & ~127u;
}

const char *ost_status_string(ost_status status) {
    switch (status) {
        case OST_OK: return "ok";
        case OST_ERR_IO: return "io";
        case OST_ERR_INVALID_ARGUMENT: return "invalid-argument";
        case OST_ERR_UNSUPPORTED: return "unsupported";
        case OST_ERR_BAD_FORMAT: return "bad-format";
        case OST_ERR_CHECKSUM: return "checksum";
        case OST_ERR_PASSWORD_REQUIRED: return "password-required";
        case OST_ERR_PASSWORD_BAD: return "password-bad";
        case OST_ERR_SKIPPED: return "skipped";
        case OST_ERR_NO_MEMORY: return "no-memory";
    }
    return "unknown";
}

const char *ost_wrapper_kind_string(ost_wrapper_kind kind) {
    switch (kind) {
        case OST_WRAPPER_RAW: return "raw";
        case OST_WRAPPER_MACBINARY: return "macbinary";
        case OST_WRAPPER_APPLESINGLE: return "applesingle";
        case OST_WRAPPER_APPLEDOUBLE: return "appledouble";
        case OST_WRAPPER_BINHEX: return "binhex";
        case OST_WRAPPER_PE_SFX: return "pe-sfx";
        case OST_WRAPPER_UNKNOWN: return "unknown";
    }
    return "unknown";
}

const char *ost_format_kind_string(ost_format_kind kind) {
    switch (kind) {
        case OST_FORMAT_SIT_CLASSIC: return "sit-classic";
        case OST_FORMAT_SIT5: return "sit5";
        case OST_FORMAT_SITX: return "sitx";
        case OST_FORMAT_UNKNOWN: return "unknown";
    }
    return "unknown";
}

bool ost_is_sit_classic_at(const uint8_t *data, size_t size, size_t off) {
    if (!data || off > size || size - off < 14) return false;
    return memcmp(data + off, "SIT!", 4) == 0 && memcmp(data + off + 10, "rLau", 4) == 0;
}

bool ost_is_sit5_at(const uint8_t *data, size_t size, size_t off) {
    if (!data || off > size || size - off < 100) return false;
    return memcmp(data + off, "StuffIt (c)1997-", 16) == 0;
}

bool ost_is_sitx_at(const uint8_t *data, size_t size, size_t off) {
    if (!data || off > size || size - off < 8) return false;
    return memcmp(data + off, "StuffIt!", 8) == 0;
}

bool ost_is_pe_sfx_at(const uint8_t *data, size_t size, size_t off) {
    if (!data || off > size || size - off < 2) return false;
    return data[off] == 'M' && data[off + 1] == 'Z';
}

static bool has_suffix_ci(const char *name, const char *suffix) {
    if (!name || !suffix) return false;
    size_t n = strlen(name);
    size_t s = strlen(suffix);
    if (s > n) return false;
    name += n - s;
    for (size_t i = 0; i < s; i++) {
        if (tolower((unsigned char)name[i]) != tolower((unsigned char)suffix[i])) return false;
    }
    return true;
}

static bool contains_bytes(const uint8_t *haystack, size_t haystack_len, const char *needle) {
    size_t needle_len = strlen(needle);
    if (!haystack || needle_len == 0 || needle_len > haystack_len) return false;
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static bool detect_apple_single_or_double(const uint8_t *data, size_t size, ost_detection *out) {
    uint32_t magic = 0, version = 0;
    if (!ost_read_u32_be(data, size, 0, &magic) || !ost_read_u32_be(data, size, 4, &version)) return false;
    if (version != 0x00020000u && version != 0x00000200u) return false;

    if (magic == 0x00051600u) out->wrapper = OST_WRAPPER_APPLESINGLE;
    else if (magic == 0x00051607u) out->wrapper = OST_WRAPPER_APPLEDOUBLE;
    else if (magic == 0x00160500u) out->wrapper = OST_WRAPPER_APPLESINGLE;
    else if (magic == 0x07160500u) out->wrapper = OST_WRAPPER_APPLEDOUBLE;
    else return false;

    out->format = OST_FORMAT_UNKNOWN;
    out->supported = false;

    bool big = magic == 0x00051600u || magic == 0x00051607u;
    uint16_t count = 0;
    if (big) {
        if (!ost_read_u16_be(data, size, 24, &count)) return true;
    } else {
        if (!ost_read_u16_le(data, size, 24, &count)) return true;
    }

    for (uint16_t i = 0; i < count; i++) {
        size_t entry_off = 26u + (size_t)i * 12u;
        uint32_t id = 0, offset = 0, length = 0;
        bool ok = big
            ? (ost_read_u32_be(data, size, entry_off, &id) &&
               ost_read_u32_be(data, size, entry_off + 4, &offset) &&
               ost_read_u32_be(data, size, entry_off + 8, &length))
            : (ost_read_u32_le(data, size, entry_off, &id) &&
               ost_read_u32_le(data, size, entry_off + 4, &offset) &&
               ost_read_u32_le(data, size, entry_off + 8, &length));
        if (!ok) break;
        if (id == 1) {
            out->has_data_fork = true;
            out->data_fork_offset = offset;
            out->data_fork_size = length;
            if (offset <= size && length <= size - offset) {
                if (ost_is_sit_classic_at(data, size, offset)) {
                    out->format = OST_FORMAT_SIT_CLASSIC;
                    out->payload_offset = offset;
                    out->payload_size = length;
                    out->supported = true;
                } else if (ost_is_sit5_at(data, size, offset)) {
                    out->format = OST_FORMAT_SIT5;
                    out->payload_offset = offset;
                    out->payload_size = length;
                    out->supported = true;
                } else if (ost_is_sitx_at(data, size, offset)) {
                    out->format = OST_FORMAT_SITX;
                    out->payload_offset = offset;
                    out->payload_size = length;
                    out->supported = false;
                }
            }
        } else if (id == 2) {
            out->has_resource_fork = true;
            out->resource_fork_offset = offset;
            out->resource_fork_size = length;
        }
    }

    return true;
}

static bool detect_macbinary(const uint8_t *data, size_t size, ost_detection *out) {
    if (!data || size < 128) return false;
    if (data[0] != 0 || data[74] != 0 || data[82] != 0) return false;
    if (data[1] == 0 || data[1] > 63) return false;
    for (uint8_t i = 0; i < data[1]; i++) {
        if (data[2 + i] == 0) return false;
    }

    uint32_t data_len = 0, rsrc_len = 0, sig = 0;
    if (!ost_read_u32_be(data, size, 83, &data_len) || !ost_read_u32_be(data, size, 87, &rsrc_len)) return false;
    (void)ost_read_u32_be(data, size, 102, &sig);

    uint64_t data_off = 128;
    uint64_t rsrc_off = data_off + round_up_128(data_len);
    if (data_off > size || data_len > size - data_off) return false;

    out->wrapper = OST_WRAPPER_MACBINARY;
    out->has_data_fork = data_len > 0;
    out->has_resource_fork = rsrc_len > 0;
    out->data_fork_offset = data_off;
    out->data_fork_size = data_len;
    out->resource_fork_offset = rsrc_off;
    out->resource_fork_size = rsrc_len;
    out->macbinary_version = sig == 0x6d42494eu ? 3 : 1;
    memcpy(out->finder_type, data + 65, 4);
    memcpy(out->finder_creator, data + 69, 4);

    uint16_t stored_crc = 0;
    if (ost_read_u16_be(data, size, 124, &stored_crc)) {
        uint16_t crc = ost_crc16_ccitt(data, 124);
        if (crc == stored_crc && out->macbinary_version < 2) out->macbinary_version = 2;
    }

    out->format = OST_FORMAT_UNKNOWN;
    out->payload_offset = data_off;
    out->payload_size = data_len;
    out->supported = false;

    if (ost_is_sit_classic_at(data, size, (size_t)data_off)) {
        out->format = OST_FORMAT_SIT_CLASSIC;
        out->supported = true;
    } else if (ost_is_sit5_at(data, size, (size_t)data_off)) {
        out->format = OST_FORMAT_SIT5;
        out->supported = true;
    } else if (ost_is_sitx_at(data, size, (size_t)data_off)) {
        out->format = OST_FORMAT_SITX;
    }

    return true;
}

ost_status ost_detect_buffer(const uint8_t *data, size_t size, const char *name, ost_detection *out) {
    if (!out) return OST_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->wrapper = OST_WRAPPER_UNKNOWN;
    out->format = OST_FORMAT_UNKNOWN;

    if (!data || size == 0) return OST_ERR_BAD_FORMAT;

    if (ost_is_sit_classic_at(data, size, 0)) {
        out->wrapper = OST_WRAPPER_RAW;
        out->format = OST_FORMAT_SIT_CLASSIC;
        out->payload_offset = 0;
        out->payload_size = size;
        out->has_data_fork = true;
        out->supported = true;
        return OST_OK;
    }
    if (ost_is_sit5_at(data, size, 0)) {
        out->wrapper = OST_WRAPPER_RAW;
        out->format = OST_FORMAT_SIT5;
        out->payload_offset = 0;
        out->payload_size = size;
        out->has_data_fork = true;
        out->supported = true;
        return OST_OK;
    }
    if (ost_is_sitx_at(data, size, 0) || has_suffix_ci(name, ".sitx")) {
        out->wrapper = OST_WRAPPER_RAW;
        out->format = OST_FORMAT_SITX;
        out->payload_offset = 0;
        out->payload_size = size;
        out->has_data_fork = true;
        out->supported = false;
        return OST_ERR_UNSUPPORTED;
    }
    if (detect_macbinary(data, size, out)) return out->supported ? OST_OK : OST_ERR_UNSUPPORTED;
    if (detect_apple_single_or_double(data, size, out)) return out->supported ? OST_OK : OST_ERR_UNSUPPORTED;
    if (ost_is_pe_sfx_at(data, size, 0)) {
        out->wrapper = OST_WRAPPER_PE_SFX;
        out->format = OST_FORMAT_UNKNOWN;
        out->payload_offset = 0;
        out->payload_size = size;
        out->supported = false;
        return OST_ERR_UNSUPPORTED;
    }
    if (size >= 45 && contains_bytes(data, size < 256 ? size : 256, "(This file must be converted with BinHex")) {
        out->wrapper = OST_WRAPPER_BINHEX;
        out->supported = false;
        return OST_ERR_UNSUPPORTED;
    }

    return OST_ERR_UNSUPPORTED;
}
