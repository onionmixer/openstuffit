#include "ost_unicode.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t composed;
    uint32_t base;
    uint32_t mark;
} compose_entry;

static const compose_entry COMPOSE_TABLE[] = {
    {0x00c0, 'A', 0x0300}, {0x00c1, 'A', 0x0301}, {0x00c2, 'A', 0x0302}, {0x00c3, 'A', 0x0303},
    {0x00c4, 'A', 0x0308}, {0x00c5, 'A', 0x030a}, {0x00c7, 'C', 0x0327}, {0x00c8, 'E', 0x0300},
    {0x00c9, 'E', 0x0301}, {0x00ca, 'E', 0x0302}, {0x00cb, 'E', 0x0308}, {0x00cc, 'I', 0x0300},
    {0x00cd, 'I', 0x0301}, {0x00ce, 'I', 0x0302}, {0x00cf, 'I', 0x0308}, {0x00d1, 'N', 0x0303},
    {0x00d2, 'O', 0x0300}, {0x00d3, 'O', 0x0301}, {0x00d4, 'O', 0x0302}, {0x00d5, 'O', 0x0303},
    {0x00d6, 'O', 0x0308}, {0x00d9, 'U', 0x0300}, {0x00da, 'U', 0x0301}, {0x00db, 'U', 0x0302},
    {0x00dc, 'U', 0x0308}, {0x00dd, 'Y', 0x0301}, {0x00e0, 'a', 0x0300}, {0x00e1, 'a', 0x0301},
    {0x00e2, 'a', 0x0302}, {0x00e3, 'a', 0x0303}, {0x00e4, 'a', 0x0308}, {0x00e5, 'a', 0x030a},
    {0x00e7, 'c', 0x0327}, {0x00e8, 'e', 0x0300}, {0x00e9, 'e', 0x0301}, {0x00ea, 'e', 0x0302},
    {0x00eb, 'e', 0x0308}, {0x00ec, 'i', 0x0300}, {0x00ed, 'i', 0x0301}, {0x00ee, 'i', 0x0302},
    {0x00ef, 'i', 0x0308}, {0x00f1, 'n', 0x0303}, {0x00f2, 'o', 0x0300}, {0x00f3, 'o', 0x0301},
    {0x00f4, 'o', 0x0302}, {0x00f5, 'o', 0x0303}, {0x00f6, 'o', 0x0308}, {0x00f9, 'u', 0x0300},
    {0x00fa, 'u', 0x0301}, {0x00fb, 'u', 0x0302}, {0x00fc, 'u', 0x0308}, {0x00fd, 'y', 0x0301},
    {0x00ff, 'y', 0x0308}, {0x0100, 'A', 0x0304}, {0x0101, 'a', 0x0304}, {0x0102, 'A', 0x0306},
    {0x0103, 'a', 0x0306}, {0x0104, 'A', 0x0328}, {0x0105, 'a', 0x0328}, {0x0106, 'C', 0x0301},
    {0x0107, 'c', 0x0301}, {0x0108, 'C', 0x0302}, {0x0109, 'c', 0x0302}, {0x010a, 'C', 0x0307},
    {0x010b, 'c', 0x0307}, {0x010c, 'C', 0x030c}, {0x010d, 'c', 0x030c}, {0x010e, 'D', 0x030c},
    {0x010f, 'd', 0x030c}, {0x0112, 'E', 0x0304}, {0x0113, 'e', 0x0304}, {0x0114, 'E', 0x0306},
    {0x0115, 'e', 0x0306}, {0x0116, 'E', 0x0307}, {0x0117, 'e', 0x0307}, {0x0118, 'E', 0x0328},
    {0x0119, 'e', 0x0328}, {0x011a, 'E', 0x030c}, {0x011b, 'e', 0x030c}, {0x011c, 'G', 0x0302},
    {0x011d, 'g', 0x0302}, {0x011e, 'G', 0x0306}, {0x011f, 'g', 0x0306}, {0x0120, 'G', 0x0307},
    {0x0121, 'g', 0x0307}, {0x0122, 'G', 0x0327}, {0x0123, 'g', 0x0327}, {0x0124, 'H', 0x0302},
    {0x0125, 'h', 0x0302}, {0x0128, 'I', 0x0303}, {0x0129, 'i', 0x0303}, {0x012a, 'I', 0x0304},
    {0x012b, 'i', 0x0304}, {0x012c, 'I', 0x0306}, {0x012d, 'i', 0x0306}, {0x012e, 'I', 0x0328},
    {0x012f, 'i', 0x0328}, {0x0130, 'I', 0x0307}, {0x0134, 'J', 0x0302}, {0x0135, 'j', 0x0302},
    {0x0139, 'L', 0x0301}, {0x013a, 'l', 0x0301}, {0x013d, 'L', 0x030c}, {0x013e, 'l', 0x030c},
    {0x0143, 'N', 0x0301}, {0x0144, 'n', 0x0301}, {0x0145, 'N', 0x0327}, {0x0146, 'n', 0x0327},
    {0x0147, 'N', 0x030c}, {0x0148, 'n', 0x030c}, {0x014c, 'O', 0x0304}, {0x014d, 'o', 0x0304},
    {0x014e, 'O', 0x0306}, {0x014f, 'o', 0x0306}, {0x0150, 'O', 0x030b}, {0x0151, 'o', 0x030b},
    {0x0154, 'R', 0x0301}, {0x0155, 'r', 0x0301}, {0x0156, 'R', 0x0327}, {0x0157, 'r', 0x0327},
    {0x0158, 'R', 0x030c}, {0x0159, 'r', 0x030c}, {0x015a, 'S', 0x0301}, {0x015b, 's', 0x0301},
    {0x015c, 'S', 0x0302}, {0x015d, 's', 0x0302}, {0x015e, 'S', 0x0327}, {0x015f, 's', 0x0327},
    {0x0160, 'S', 0x030c}, {0x0161, 's', 0x030c}, {0x0162, 'T', 0x0327}, {0x0163, 't', 0x0327},
    {0x0164, 'T', 0x030c}, {0x0165, 't', 0x030c}, {0x0168, 'U', 0x0303}, {0x0169, 'u', 0x0303},
    {0x016a, 'U', 0x0304}, {0x016b, 'u', 0x0304}, {0x016c, 'U', 0x0306}, {0x016d, 'u', 0x0306},
    {0x016e, 'U', 0x030a}, {0x016f, 'u', 0x030a}, {0x0170, 'U', 0x030b}, {0x0171, 'u', 0x030b},
    {0x0172, 'U', 0x0328}, {0x0173, 'u', 0x0328}, {0x0174, 'W', 0x0302}, {0x0175, 'w', 0x0302},
    {0x0176, 'Y', 0x0302}, {0x0177, 'y', 0x0302}, {0x0178, 'Y', 0x0308}, {0x0179, 'Z', 0x0301},
    {0x017a, 'z', 0x0301}, {0x017b, 'Z', 0x0307}, {0x017c, 'z', 0x0307}, {0x017d, 'Z', 0x030c},
    {0x017e, 'z', 0x030c},
};

static bool str_eq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

const char *ost_unicode_normalization_name(ost_unicode_normalization mode) {
    switch (mode) {
        case OST_UNICODE_NORMALIZE_NONE: return "none";
        case OST_UNICODE_NORMALIZE_NFC: return "nfc";
        case OST_UNICODE_NORMALIZE_NFD: return "nfd";
    }
    return "unknown";
}

bool ost_parse_unicode_normalization(const char *name, ost_unicode_normalization *mode) {
    if (!mode) return false;
    if (str_eq(name, "none")) *mode = OST_UNICODE_NORMALIZE_NONE;
    else if (str_eq(name, "nfc")) *mode = OST_UNICODE_NORMALIZE_NFC;
    else if (str_eq(name, "nfd")) *mode = OST_UNICODE_NORMALIZE_NFD;
    else return false;
    return true;
}

static bool utf8_decode_one(const char **p, const char *end, uint32_t *out) {
    const unsigned char *s = (const unsigned char *)*p;
    size_t left = (size_t)(end - *p);
    if (left == 0) return false;
    if (s[0] < 0x80u) {
        *out = s[0];
        *p += 1;
        return true;
    }
    if ((s[0] & 0xe0u) == 0xc0u) {
        if (left < 2u) return false;
        if ((s[1] & 0xc0u) != 0x80u) return false;
        uint32_t cp = ((uint32_t)(s[0] & 0x1fu) << 6u) | (uint32_t)(s[1] & 0x3fu);
        if (cp < 0x80u) return false;
        *out = cp;
        *p += 2;
        return true;
    }
    if ((s[0] & 0xf0u) == 0xe0u) {
        if (left < 3u) return false;
        if ((s[1] & 0xc0u) != 0x80u || (s[2] & 0xc0u) != 0x80u) return false;
        uint32_t cp = ((uint32_t)(s[0] & 0x0fu) << 12u) | ((uint32_t)(s[1] & 0x3fu) << 6u) |
                      (uint32_t)(s[2] & 0x3fu);
        if (cp < 0x800u || (cp >= 0xd800u && cp <= 0xdfffu)) return false;
        *out = cp;
        *p += 3;
        return true;
    }
    if ((s[0] & 0xf8u) == 0xf0u) {
        if (left < 4u) return false;
        if ((s[1] & 0xc0u) != 0x80u || (s[2] & 0xc0u) != 0x80u || (s[3] & 0xc0u) != 0x80u) return false;
        uint32_t cp = ((uint32_t)(s[0] & 0x07u) << 18u) | ((uint32_t)(s[1] & 0x3fu) << 12u) |
                      ((uint32_t)(s[2] & 0x3fu) << 6u) | (uint32_t)(s[3] & 0x3fu);
        if (cp < 0x10000u || cp > 0x10ffffu) return false;
        *out = cp;
        *p += 4;
        return true;
    }
    return false;
}

static size_t utf8_len_cp(uint32_t cp) {
    if (cp < 0x80u) return 1;
    if (cp < 0x800u) return 2;
    if (cp < 0x10000u) return 3;
    return 4;
}

static void utf8_put_cp(char **p, uint32_t cp) {
    if (cp < 0x80u) {
        *(*p)++ = (char)cp;
    } else if (cp < 0x800u) {
        *(*p)++ = (char)(0xc0u | (cp >> 6u));
        *(*p)++ = (char)(0x80u | (cp & 0x3fu));
    } else if (cp < 0x10000u) {
        *(*p)++ = (char)(0xe0u | (cp >> 12u));
        *(*p)++ = (char)(0x80u | ((cp >> 6u) & 0x3fu));
        *(*p)++ = (char)(0x80u | (cp & 0x3fu));
    } else {
        *(*p)++ = (char)(0xf0u | (cp >> 18u));
        *(*p)++ = (char)(0x80u | ((cp >> 12u) & 0x3fu));
        *(*p)++ = (char)(0x80u | ((cp >> 6u) & 0x3fu));
        *(*p)++ = (char)(0x80u | (cp & 0x3fu));
    }
}

static ost_status cps_push(uint32_t **items, size_t *count, size_t *cap, uint32_t cp) {
    if (*count == *cap) {
        size_t next = *cap ? *cap * 2u : 32u;
        uint32_t *p = (uint32_t *)realloc(*items, next * sizeof(*p));
        if (!p) return OST_ERR_NO_MEMORY;
        *items = p;
        *cap = next;
    }
    (*items)[(*count)++] = cp;
    return OST_OK;
}

static ost_status utf8_to_cps(const char *input, uint32_t **items, size_t *count) {
    *items = NULL;
    *count = 0;
    size_t cap = 0;
    const char *p = input;
    const char *end = input + strlen(input);
    while (p < end) {
        uint32_t cp = 0;
        if (!utf8_decode_one(&p, end, &cp)) {
            free(*items);
            *items = NULL;
            *count = 0;
            return OST_ERR_BAD_FORMAT;
        }
        ost_status st = cps_push(items, count, &cap, cp);
        if (st != OST_OK) {
            free(*items);
            *items = NULL;
            *count = 0;
            return st;
        }
    }
    return OST_OK;
}

static ost_status cps_to_utf8(const uint32_t *items, size_t count, char **output) {
    size_t len = 0;
    for (size_t i = 0; i < count; i++) len += utf8_len_cp(items[i]);
    char *out = (char *)malloc(len + 1u);
    if (!out) return OST_ERR_NO_MEMORY;
    char *p = out;
    for (size_t i = 0; i < count; i++) utf8_put_cp(&p, items[i]);
    *p = '\0';
    *output = out;
    return OST_OK;
}

static bool decompose_pair(uint32_t cp, uint32_t *base, uint32_t *mark) {
    for (size_t i = 0; i < sizeof(COMPOSE_TABLE) / sizeof(COMPOSE_TABLE[0]); i++) {
        if (COMPOSE_TABLE[i].composed == cp) {
            *base = COMPOSE_TABLE[i].base;
            *mark = COMPOSE_TABLE[i].mark;
            return true;
        }
    }
    return false;
}

static bool compose_pair(uint32_t base, uint32_t mark, uint32_t *composed) {
    for (size_t i = 0; i < sizeof(COMPOSE_TABLE) / sizeof(COMPOSE_TABLE[0]); i++) {
        if (COMPOSE_TABLE[i].base == base && COMPOSE_TABLE[i].mark == mark) {
            *composed = COMPOSE_TABLE[i].composed;
            return true;
        }
    }
    return false;
}

static bool is_hangul_syllable(uint32_t cp) {
    return cp >= 0xac00u && cp <= 0xd7a3u;
}

static bool is_hangul_l(uint32_t cp) {
    return cp >= 0x1100u && cp <= 0x1112u;
}

static bool is_hangul_v(uint32_t cp) {
    return cp >= 0x1161u && cp <= 0x1175u;
}

static bool is_hangul_t(uint32_t cp) {
    return cp >= 0x11a8u && cp <= 0x11c2u;
}

static bool is_hangul_lv(uint32_t cp) {
    return is_hangul_syllable(cp) && ((cp - 0xac00u) % 28u) == 0;
}

static ost_status normalize_nfd(const uint32_t *in, size_t in_count, uint32_t **out, size_t *out_count) {
    *out = NULL;
    *out_count = 0;
    size_t cap = 0;
    for (size_t i = 0; i < in_count; i++) {
        uint32_t cp = in[i];
        if (is_hangul_syllable(cp)) {
            uint32_t sindex = cp - 0xac00u;
            uint32_t l = 0x1100u + sindex / 588u;
            uint32_t v = 0x1161u + (sindex % 588u) / 28u;
            uint32_t t = sindex % 28u;
            ost_status st = cps_push(out, out_count, &cap, l);
            if (st != OST_OK) return st;
            st = cps_push(out, out_count, &cap, v);
            if (st != OST_OK) return st;
            if (t != 0) {
                st = cps_push(out, out_count, &cap, 0x11a7u + t);
                if (st != OST_OK) return st;
            }
            continue;
        }
        uint32_t base = 0, mark = 0;
        if (decompose_pair(cp, &base, &mark)) {
            ost_status st = cps_push(out, out_count, &cap, base);
            if (st != OST_OK) return st;
            st = cps_push(out, out_count, &cap, mark);
            if (st != OST_OK) return st;
            continue;
        }
        ost_status st = cps_push(out, out_count, &cap, cp);
        if (st != OST_OK) return st;
    }
    return OST_OK;
}

static ost_status normalize_nfc(const uint32_t *in, size_t in_count, uint32_t **out, size_t *out_count) {
    *out = NULL;
    *out_count = 0;
    size_t cap = 0;
    for (size_t i = 0; i < in_count; i++) {
        uint32_t cp = in[i];
        if (*out_count > 0) {
            uint32_t prev = (*out)[*out_count - 1u];
            uint32_t composed = 0;
            if (is_hangul_l(prev) && is_hangul_v(cp)) {
                composed = 0xac00u + (prev - 0x1100u) * 588u + (cp - 0x1161u) * 28u;
                (*out)[*out_count - 1u] = composed;
                continue;
            }
            if (is_hangul_lv(prev) && is_hangul_t(cp)) {
                (*out)[*out_count - 1u] = prev + (cp - 0x11a7u);
                continue;
            }
            if (compose_pair(prev, cp, &composed)) {
                (*out)[*out_count - 1u] = composed;
                continue;
            }
        }
        ost_status st = cps_push(out, out_count, &cap, cp);
        if (st != OST_OK) return st;
    }
    return OST_OK;
}

ost_status ost_normalize_utf8(const char *input, ost_unicode_normalization mode, char **output) {
    if (!input || !output) return OST_ERR_INVALID_ARGUMENT;
    *output = NULL;
    if (mode == OST_UNICODE_NORMALIZE_NONE) {
        size_t len = strlen(input);
        char *copy = (char *)malloc(len + 1u);
        if (!copy) return OST_ERR_NO_MEMORY;
        memcpy(copy, input, len + 1u);
        *output = copy;
        return OST_OK;
    }

    uint32_t *in = NULL, *norm = NULL;
    size_t in_count = 0, norm_count = 0;
    ost_status st = utf8_to_cps(input, &in, &in_count);
    if (st != OST_OK) return st;

    if (mode == OST_UNICODE_NORMALIZE_NFD) st = normalize_nfd(in, in_count, &norm, &norm_count);
    else st = normalize_nfc(in, in_count, &norm, &norm_count);
    free(in);
    if (st != OST_OK) {
        free(norm);
        return st;
    }
    st = cps_to_utf8(norm, norm_count, output);
    free(norm);
    return st;
}
