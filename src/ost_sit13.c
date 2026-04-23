#include "ost_sit13.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef OST_SIT13_TRACE
#include <stdio.h>
#define TRACE_BAD(msg) fprintf(stderr, "sit13: %s\n", (msg))
#else
#define TRACE_BAD(msg) ((void)0)
#endif

/*
 * Method 13 table data is mechanically generated from stuffit-rs
 * reference_repos/stuffit-rs/src/lib.rs. stuffit-rs is MIT OR Apache-2.0.
 */
#include "ost_sit13_tables.inc"

#define HUFF_EMPTY INT_MIN
#define SIT13_WINDOW_SIZE 65536u
#define SIT13_WINDOW_MASK (SIT13_WINDOW_SIZE - 1u)

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint64_t bitbuf;
    unsigned bits;
    bool error;
} sit13_bits;

typedef struct {
    int child[2];
} huff_node;

typedef struct {
    huff_node *nodes;
    size_t count;
    size_t cap;
} huff_tree;

static void bits_init(sit13_bits *br, const uint8_t *data, size_t size) {
    br->data = data;
    br->size = size;
    br->pos = 0;
    br->bitbuf = 0;
    br->bits = 0;
    br->error = false;
}

static bool bits_read_le(sit13_bits *br, unsigned n, uint32_t *out) {
    if (!br || !out || n > 31) return false;
    if (n == 0) {
        *out = 0;
        return true;
    }
    while (br->bits < n) {
        if (br->pos >= br->size) {
            br->error = true;
            return false;
        }
        br->bitbuf |= (uint64_t)br->data[br->pos++] << br->bits;
        br->bits += 8;
    }
    *out = (uint32_t)(br->bitbuf & ((((uint64_t)1) << n) - 1u));
    br->bitbuf >>= n;
    br->bits -= n;
    return true;
}

static int bits_read_bit_le(sit13_bits *br) {
    uint32_t v = 0;
    if (!bits_read_le(br, 1, &v)) return -1;
    return (int)v;
}

static uint8_t bits_read_byte(sit13_bits *br) {
    uint32_t v = 0;
    if (!bits_read_le(br, 8, &v)) return 0;
    return (uint8_t)v;
}

static void huff_free(huff_tree *tree) {
    if (!tree) return;
    free(tree->nodes);
    tree->nodes = NULL;
    tree->count = 0;
    tree->cap = 0;
}

static ost_status huff_reserve_node(huff_tree *tree, int *node_index) {
    if (tree->count == tree->cap) {
        size_t next = tree->cap ? tree->cap * 2u : 64u;
        huff_node *nodes = (huff_node *)realloc(tree->nodes, next * sizeof(*nodes));
        if (!nodes) return OST_ERR_NO_MEMORY;
        tree->nodes = nodes;
        tree->cap = next;
    }
    tree->nodes[tree->count].child[0] = HUFF_EMPTY;
    tree->nodes[tree->count].child[1] = HUFF_EMPTY;
    if (tree->count > (size_t)INT_MAX) return OST_ERR_BAD_FORMAT;
    *node_index = (int)tree->count++;
    return OST_OK;
}

static ost_status huff_init(huff_tree *tree) {
    memset(tree, 0, sizeof(*tree));
    int root = 0;
    return huff_reserve_node(tree, &root);
}

static ost_status huff_clone(huff_tree *dst, const huff_tree *src) {
    memset(dst, 0, sizeof(*dst));
    if (!src || src->count == 0) return OST_ERR_BAD_FORMAT;
    dst->nodes = (huff_node *)malloc(src->count * sizeof(*dst->nodes));
    if (!dst->nodes) return OST_ERR_NO_MEMORY;
    memcpy(dst->nodes, src->nodes, src->count * sizeof(*dst->nodes));
    dst->count = src->count;
    dst->cap = src->count;
    return OST_OK;
}

static ost_status huff_from_lengths(huff_tree *tree, const int8_t *lengths, size_t num_symbols) {
    ost_status st = huff_init(tree);
    if (st != OST_OK) return st;

    uint32_t code = 0;
    for (int length = 1; length <= 32; length++) {
        for (size_t i = 0; i < num_symbols; i++) {
            if ((int)lengths[i] != length) continue;
            int node = 0;
            for (int bit_pos = length - 1; bit_pos >= 0; bit_pos--) {
                int bit = (int)((code >> (unsigned)bit_pos) & 1u);
                if (tree->nodes[node].child[bit] == HUFF_EMPTY) {
                    int new_node = 0;
                    st = huff_reserve_node(tree, &new_node);
                    if (st != OST_OK) return st;
                    tree->nodes[node].child[bit] = new_node;
                }
                node = tree->nodes[node].child[bit];
                if (node < 0 || (size_t)node >= tree->count) return OST_ERR_BAD_FORMAT;
            }
            if (i > (size_t)INT_MAX) return OST_ERR_BAD_FORMAT;
            tree->nodes[node].child[0] = (int)i;
            tree->nodes[node].child[1] = (int)i;
            code++;
        }
        code <<= 1;
    }
    return OST_OK;
}

static ost_status huff_from_explicit(huff_tree *tree, const uint32_t *codes, const int8_t *lengths, size_t num_symbols) {
    ost_status st = huff_init(tree);
    if (st != OST_OK) return st;

    for (size_t i = 0; i < num_symbols; i++) {
        int length = (int)lengths[i];
        if (length <= 0) continue;
        uint32_t code = codes[i];
        int node = 0;
        for (int bit_pos = 0; bit_pos < length; bit_pos++) {
            int bit = (int)((code >> (unsigned)bit_pos) & 1u);
            if (tree->nodes[node].child[bit] == HUFF_EMPTY) {
                int new_node = 0;
                st = huff_reserve_node(tree, &new_node);
                if (st != OST_OK) return st;
                tree->nodes[node].child[bit] = new_node;
            }
            node = tree->nodes[node].child[bit];
            if (node < 0 || (size_t)node >= tree->count) return OST_ERR_BAD_FORMAT;
        }
        if (i > (size_t)INT_MAX) return OST_ERR_BAD_FORMAT;
        tree->nodes[node].child[0] = (int)i;
        tree->nodes[node].child[1] = (int)i;
    }
    return OST_OK;
}

static int huff_decode_le(const huff_tree *tree, sit13_bits *br) {
    if (!tree || !br || tree->count == 0) return -1;
    int node = 0;
    for (;;) {
        if (node < 0 || (size_t)node >= tree->count) return -1;
        int left = tree->nodes[node].child[0];
        int right = tree->nodes[node].child[1];
        if (left == right) return left;
        int bit = bits_read_bit_le(br);
        if (bit < 0) return -1;
        int next = tree->nodes[node].child[bit];
        if (next == HUFF_EMPTY) return -1;
        node = next;
    }
}

static ost_status parse_dynamic_huffman(sit13_bits *br, size_t num_codes, const huff_tree *meta, huff_tree *out) {
    int8_t *lengths = (int8_t *)calloc(num_codes, sizeof(*lengths));
    if (!lengths) return OST_ERR_NO_MEMORY;

    int length = 0;
    size_t i = 0;
    ost_status st = OST_OK;
    while (i < num_codes) {
        int val = huff_decode_le(meta, br);
        if (val < 0) {
            st = OST_ERR_BAD_FORMAT;
            break;
        }
        switch (val) {
            case 31:
                length = -1;
                break;
            case 32:
                length++;
                break;
            case 33:
                length--;
                break;
            case 34: {
                int bit = bits_read_bit_le(br);
                if (bit < 0) {
                    st = OST_ERR_BAD_FORMAT;
                    break;
                }
                if (bit) lengths[i++] = (int8_t)length;
                break;
            }
            case 35: {
                uint32_t count = 0;
                if (!bits_read_le(br, 3, &count)) {
                    st = OST_ERR_BAD_FORMAT;
                    break;
                }
                count += 2u;
                while (count > 0 && i < num_codes) {
                    lengths[i++] = (int8_t)length;
                    count--;
                }
                break;
            }
            case 36: {
                uint32_t count = 0;
                if (!bits_read_le(br, 6, &count)) {
                    st = OST_ERR_BAD_FORMAT;
                    break;
                }
                count += 10u;
                while (count > 0 && i < num_codes) {
                    lengths[i++] = (int8_t)length;
                    count--;
                }
                break;
            }
            default:
                length = val + 1;
                break;
        }
        if (st != OST_OK) break;
        if (i < num_codes) lengths[i++] = (int8_t)length;
    }

    if (st == OST_OK) st = huff_from_lengths(out, lengths, num_codes);
    free(lengths);
    return st;
}

ost_status ost_sit13_decompress(const uint8_t *src, size_t src_size, size_t out_size, uint8_t **out, size_t *actual) {
    if (!src || !out || !actual) return OST_ERR_INVALID_ARGUMENT;
    *out = NULL;
    *actual = 0;
    if (out_size == 0) return OST_OK;

    uint8_t *buf = (uint8_t *)malloc(out_size);
    if (!buf) return OST_ERR_NO_MEMORY;
    uint8_t *window = (uint8_t *)calloc(SIT13_WINDOW_SIZE, 1);
    if (!window) {
        free(buf);
        return OST_ERR_NO_MEMORY;
    }

    sit13_bits br;
    bits_init(&br, src, src_size);

    huff_tree meta;
    huff_tree first;
    huff_tree second;
    huff_tree offset;
    memset(&meta, 0, sizeof(meta));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(&offset, 0, sizeof(offset));

    ost_status st = OST_OK;
    uint8_t first_byte = bits_read_byte(&br);
    if (br.error) {
        st = OST_ERR_BAD_FORMAT;
        goto done;
    }

    size_t code = (size_t)(first_byte >> 4);
    if (code == 0) {
        st = huff_from_explicit(&meta, SIT13_META_CODES, SIT13_META_CODE_LENGTHS, 37);
        if (st != OST_OK) goto done;
        st = parse_dynamic_huffman(&br, 321, &meta, &first);
        if (st != OST_OK) goto done;
        if ((first_byte & 0x08u) != 0) {
            st = huff_clone(&second, &first);
        } else {
            st = parse_dynamic_huffman(&br, 321, &meta, &second);
        }
        if (st != OST_OK) goto done;
        st = parse_dynamic_huffman(&br, (size_t)(first_byte & 0x07u) + 10u, &meta, &offset);
        if (st != OST_OK) goto done;
    } else if (code < 6) {
        size_t idx = code - 1u;
        st = huff_from_lengths(&first, SIT13_FIRST_CODE_LENGTHS[idx], 321);
        if (st != OST_OK) goto done;
        st = huff_from_lengths(&second, SIT13_SECOND_CODE_LENGTHS[idx], 321);
        if (st != OST_OK) goto done;
        st = huff_from_lengths(&offset, SIT13_OFFSET_CODE_LENGTHS[idx], SIT13_OFFSET_CODE_SIZES[idx]);
        if (st != OST_OK) goto done;
    } else {
        st = OST_ERR_BAD_FORMAT;
        goto done;
    }

    huff_tree *current = &first;
    size_t op = 0;
    while (op < out_size) {
        int val = huff_decode_le(current, &br);
        if (val < 0) {
            TRACE_BAD("huffman decode failed");
            st = OST_ERR_BAD_FORMAT;
            goto done;
        }
        if (val < 256) {
            buf[op] = (uint8_t)val;
            window[op & SIT13_WINDOW_MASK] = (uint8_t)val;
            op++;
            current = &first;
        } else if (val < 320) {
            current = &second;
            size_t length = (size_t)(val - 256 + 3);
            if (val == 318) {
                uint32_t extra = 0;
                if (!bits_read_le(&br, 10, &extra)) {
                    TRACE_BAD("short long-length-10");
                    st = OST_ERR_BAD_FORMAT;
                    goto done;
                }
                length = (size_t)extra + 65u;
            } else if (val == 319) {
                uint32_t extra = 0;
                if (!bits_read_le(&br, 15, &extra)) {
                    TRACE_BAD("short long-length-15");
                    st = OST_ERR_BAD_FORMAT;
                    goto done;
                }
                length = (size_t)extra + 65u;
            }

            int bit_len = huff_decode_le(&offset, &br);
            if (bit_len < 0) {
                TRACE_BAD("offset decode failed");
                st = OST_ERR_BAD_FORMAT;
                goto done;
            }
            size_t dist = 0;
            if (bit_len == 0) {
                dist = 1;
            } else if (bit_len == 1) {
                dist = 2;
            } else {
                uint32_t extra = 0;
                if (!bits_read_le(&br, (unsigned)bit_len - 1u, &extra)) {
                    TRACE_BAD("short offset extra bits");
                    st = OST_ERR_BAD_FORMAT;
                    goto done;
                }
                dist = ((size_t)1 << ((unsigned)bit_len - 1u)) + (size_t)extra + 1u;
            }
            if (dist == 0 || dist > SIT13_WINDOW_SIZE) {
                TRACE_BAD("invalid offset distance");
                st = OST_ERR_BAD_FORMAT;
                goto done;
            }
            while (length > 0 && op < out_size) {
                uint8_t b = window[(op - dist) & SIT13_WINDOW_MASK];
                buf[op] = b;
                window[op & SIT13_WINDOW_MASK] = b;
                op++;
                length--;
            }
        } else {
            break;
        }
    }

    if (op != out_size) {
        TRACE_BAD("output size mismatch");
        st = OST_ERR_BAD_FORMAT;
        goto done;
    }
    *out = buf;
    *actual = op;
    buf = NULL;

done:
    free(window);
    free(buf);
    huff_free(&meta);
    huff_free(&first);
    huff_free(&second);
    huff_free(&offset);
    return st;
}
