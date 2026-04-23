#include "ost_sit15.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * Method 15 "Arsenic" randomization table is mechanically generated from
 * reference_repos/stuffit-rs/src/lib.rs. stuffit-rs is MIT OR Apache-2.0.
 */
#include "ost_sit15_tables.inc"

#define ARITH_BITS 26u
#define ARITH_ONE (1u << (ARITH_BITS - 1u))
#define ARITH_HALF (1u << (ARITH_BITS - 2u))
#define SIT15_MAX_MODEL_SYMBOLS 128u
#define SIT15_MAX_BLOCK_BITS 24u

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t bitpos;
    bool error;
} sit15_bits;

typedef struct {
    uint16_t first_symbol;
    size_t num_symbols;
    uint16_t frequencies[SIT15_MAX_MODEL_SYMBOLS];
    uint32_t total_frequency;
    uint16_t increment;
    uint32_t limit;
} sit15_model;

typedef struct {
    sit15_bits reader;
    uint32_t range;
    uint32_t code;
    bool error;
} sit15_arith;

static unsigned bits_read_be(sit15_bits *br) {
    if (!br || br->bitpos >= br->size * 8u) {
        if (br) br->error = true;
        return 0;
    }
    size_t byte_pos = br->bitpos / 8u;
    unsigned bit_pos = 7u - (unsigned)(br->bitpos % 8u);
    unsigned bit = (unsigned)((br->data[byte_pos] >> bit_pos) & 1u);
    br->bitpos++;
    return bit;
}

static void model_init(sit15_model *model,
                       uint16_t first_symbol,
                       size_t num_symbols,
                       uint16_t increment,
                       uint32_t limit) {
    model->first_symbol = first_symbol;
    model->num_symbols = num_symbols;
    model->increment = increment;
    model->limit = limit;
    model->total_frequency = 0;
    for (size_t i = 0; i < SIT15_MAX_MODEL_SYMBOLS; i++) model->frequencies[i] = 0;
    for (size_t i = 0; i < num_symbols && i < SIT15_MAX_MODEL_SYMBOLS; i++) {
        model->frequencies[i] = increment;
        model->total_frequency += increment;
    }
}

static void model_reset(sit15_model *model) {
    model->total_frequency = 0;
    for (size_t i = 0; i < model->num_symbols; i++) {
        model->frequencies[i] = model->increment;
        model->total_frequency += model->increment;
    }
}

static void model_update(sit15_model *model, size_t sym_idx) {
    if (sym_idx >= model->num_symbols) return;
    model->frequencies[sym_idx] = (uint16_t)(model->frequencies[sym_idx] + model->increment);
    model->total_frequency += model->increment;
    if (model->total_frequency > model->limit) {
        model->total_frequency = 0;
        for (size_t i = 0; i < model->num_symbols; i++) {
            model->frequencies[i] = (uint16_t)((model->frequencies[i] + 1u) >> 1u);
            if (model->frequencies[i] == 0) model->frequencies[i] = 1;
            model->total_frequency += model->frequencies[i];
        }
    }
}

static void arith_init(sit15_arith *dec, const uint8_t *src, size_t src_size) {
    dec->reader.data = src;
    dec->reader.size = src_size;
    dec->reader.bitpos = 0;
    dec->reader.error = false;
    dec->range = ARITH_ONE;
    dec->code = 0;
    dec->error = false;
    for (unsigned i = 0; i < ARITH_BITS; i++) {
        dec->code = (dec->code << 1u) | bits_read_be(&dec->reader);
    }
    if (dec->reader.error) dec->error = true;
}

static uint16_t arith_next_symbol(sit15_arith *dec, sit15_model *model) {
    if (!dec || !model || model->num_symbols == 0 || model->num_symbols > SIT15_MAX_MODEL_SYMBOLS ||
        model->total_frequency == 0) {
        if (dec) dec->error = true;
        return 0;
    }

    uint32_t renorm_factor = dec->range / model->total_frequency;
    if (renorm_factor == 0) {
        dec->error = true;
        return 0;
    }
    uint32_t freq = dec->code / renorm_factor;
    if (freq >= model->total_frequency) freq = model->total_frequency - 1u;

    uint32_t cumulative = 0;
    size_t n = 0;
    while (n < model->num_symbols - 1u) {
        uint32_t next = cumulative + model->frequencies[n];
        if (next > freq) break;
        cumulative = next;
        n++;
    }

    uint32_t sym_size = model->frequencies[n];
    uint32_t sym_total = model->total_frequency;
    uint32_t low_incr = renorm_factor * cumulative;
    dec->code -= low_incr;
    if (cumulative + sym_size == sym_total) {
        dec->range -= low_incr;
    } else {
        dec->range = sym_size * renorm_factor;
    }

    while (dec->range <= ARITH_HALF) {
        dec->range <<= 1u;
        dec->code = (dec->code << 1u) | bits_read_be(&dec->reader);
    }
    if (dec->reader.error) dec->error = true;

    uint16_t res = (uint16_t)(model->first_symbol + n);
    model_update(model, n);
    return res;
}

static uint32_t arith_read_bit_string(sit15_arith *dec, sit15_model *model, uint32_t n) {
    uint32_t res = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (arith_next_symbol(dec, model) != 0) res |= 1u << i;
    }
    return res;
}

static void mtf_reset(uint8_t mtf[256]) {
    for (unsigned i = 0; i < 256u; i++) mtf[i] = (uint8_t)i;
}

static uint8_t mtf_decode(uint8_t mtf[256], size_t symbol) {
    uint8_t val = mtf[symbol];
    if (symbol > 0) {
        memmove(&mtf[1], &mtf[0], symbol);
        mtf[0] = val;
    }
    return val;
}

static ost_status block_push(uint8_t *block, size_t block_size, size_t *block_len, uint8_t value) {
    if (*block_len >= block_size) return OST_ERR_BAD_FORMAT;
    block[(*block_len)++] = value;
    return OST_OK;
}

static ost_status block_push_repeat(uint8_t *block, size_t block_size, size_t *block_len, uint8_t value, size_t count) {
    if (count > block_size || *block_len > block_size - count) return OST_ERR_BAD_FORMAT;
    memset(block + *block_len, value, count);
    *block_len += count;
    return OST_OK;
}

static ost_status inverse_bwt_rle(const uint8_t *block,
                                  size_t block_len,
                                  size_t transform_index,
                                  bool randomized,
                                  uint8_t *out,
                                  size_t out_size,
                                  size_t *op) {
    if (block_len == 0 || transform_index >= block_len) return OST_ERR_BAD_FORMAT;
    size_t *transform = (size_t *)malloc(block_len * sizeof(*transform));
    if (!transform) return OST_ERR_NO_MEMORY;

    size_t counts[256] = {0};
    size_t starts[256] = {0};
    for (size_t i = 0; i < block_len; i++) counts[block[i]]++;
    size_t sum = 0;
    for (size_t i = 0; i < 256u; i++) {
        starts[i] = sum;
        sum += counts[i];
    }
    size_t pos[256];
    memcpy(pos, starts, sizeof(pos));
    for (size_t i = 0; i < block_len; i++) {
        uint8_t b = block[i];
        transform[pos[b]++] = i;
    }

    size_t byte_count = 0;
    size_t idx = transform_index;
    unsigned run_count = 0;
    uint8_t last = 0;
    size_t repeat = 0;
    size_t rand_idx = 0;
    size_t rand_val = SIT15_RANDOMIZATION_TABLE[0];

    while ((byte_count < block_len || repeat > 0) && *op < out_size) {
        if (repeat > 0) {
            out[(*op)++] = last;
            repeat--;
            continue;
        }

        idx = transform[idx];
        uint8_t b = block[idx];
        if (randomized && rand_val == byte_count) {
            b ^= 1u;
            rand_idx = (rand_idx + 1u) & 255u;
            rand_val += SIT15_RANDOMIZATION_TABLE[rand_idx];
        }
        byte_count++;

        if (run_count == 4u) {
            run_count = 0;
            if (b == 0) continue;
            repeat = (size_t)b - 1u;
            out[(*op)++] = last;
        } else {
            if (b == last) {
                run_count++;
            } else {
                run_count = 1;
                last = b;
            }
            out[(*op)++] = b;
        }
    }

    free(transform);
    return OST_OK;
}

ost_status ost_sit15_decompress(const uint8_t *src, size_t src_size, size_t out_size, uint8_t **out, size_t *actual) {
    if (!out || !actual) return OST_ERR_INVALID_ARGUMENT;
    *out = NULL;
    *actual = 0;
    if (!src && src_size > 0) return OST_ERR_INVALID_ARGUMENT;
    if (out_size == 0) return OST_OK;

    uint8_t *dst = (uint8_t *)malloc(out_size);
    if (!dst) return OST_ERR_NO_MEMORY;

    sit15_arith dec;
    arith_init(&dec, src, src_size);
    sit15_model initial;
    model_init(&initial, 0, 2, 1, 256);

    if (arith_read_bit_string(&dec, &initial, 8) != 'A' || arith_read_bit_string(&dec, &initial, 8) != 's' ||
        dec.error) {
        free(dst);
        return OST_ERR_BAD_FORMAT;
    }

    uint32_t block_bits = arith_read_bit_string(&dec, &initial, 4) + 9u;
    if (dec.error || block_bits > SIT15_MAX_BLOCK_BITS || block_bits >= (sizeof(size_t) * CHAR_BIT)) {
        free(dst);
        return OST_ERR_BAD_FORMAT;
    }
    size_t block_size = (size_t)1u << block_bits;
    uint8_t *block = (uint8_t *)malloc(block_size);
    if (!block) {
        free(dst);
        return OST_ERR_NO_MEMORY;
    }

    sit15_model selector;
    sit15_model mtf_models[7];
    model_init(&selector, 0, 11, 8, 1024);
    model_init(&mtf_models[0], 2, 2, 8, 1024);
    model_init(&mtf_models[1], 4, 4, 4, 1024);
    model_init(&mtf_models[2], 8, 8, 4, 1024);
    model_init(&mtf_models[3], 16, 16, 4, 1024);
    model_init(&mtf_models[4], 32, 32, 2, 1024);
    model_init(&mtf_models[5], 64, 64, 2, 1024);
    model_init(&mtf_models[6], 128, 128, 1, 1024);

    size_t op = 0;
    ost_status st = OST_OK;
    while (op < out_size) {
        if (arith_next_symbol(&dec, &initial) != 0) break;
        bool randomized = arith_next_symbol(&dec, &initial) != 0;
        size_t transform_index = (size_t)arith_read_bit_string(&dec, &initial, block_bits);
        if (dec.error) {
            st = OST_ERR_BAD_FORMAT;
            break;
        }

        size_t block_len = 0;
        uint8_t mtf[256];
        mtf_reset(mtf);

        for (;;) {
            uint16_t sel = arith_next_symbol(&dec, &selector);
            if (dec.error) {
                st = OST_ERR_BAD_FORMAT;
                break;
            }

            if (sel <= 1u) {
                size_t zero_state = 1;
                size_t zero_count = 0;
                uint16_t current_sel = sel;
                while (current_sel < 2u) {
                    if (zero_state > block_size || zero_count > block_size) {
                        st = OST_ERR_BAD_FORMAT;
                        break;
                    }
                    zero_count += current_sel == 0 ? zero_state : zero_state * 2u;
                    zero_state *= 2u;
                    current_sel = arith_next_symbol(&dec, &selector);
                    if (dec.error) {
                        st = OST_ERR_BAD_FORMAT;
                        break;
                    }
                }
                if (st != OST_OK) break;
                st = block_push_repeat(block, block_size, &block_len, mtf[0], zero_count);
                if (st != OST_OK) break;
                if (current_sel == 10u) break;
                if (current_sel > 9u) {
                    st = OST_ERR_BAD_FORMAT;
                    break;
                }
                size_t symbol = current_sel == 2u ? 1u : arith_next_symbol(&dec, &mtf_models[current_sel - 3u]);
                if (dec.error || symbol >= 256u) {
                    st = OST_ERR_BAD_FORMAT;
                    break;
                }
                st = block_push(block, block_size, &block_len, mtf_decode(mtf, symbol));
                if (st != OST_OK) break;
            } else if (sel == 10u) {
                break;
            } else {
                if (sel > 9u) {
                    st = OST_ERR_BAD_FORMAT;
                    break;
                }
                size_t symbol = sel == 2u ? 1u : arith_next_symbol(&dec, &mtf_models[sel - 3u]);
                if (dec.error || symbol >= 256u) {
                    st = OST_ERR_BAD_FORMAT;
                    break;
                }
                st = block_push(block, block_size, &block_len, mtf_decode(mtf, symbol));
                if (st != OST_OK) break;
            }
        }
        if (st != OST_OK) break;

        model_reset(&selector);
        for (size_t i = 0; i < 7u; i++) model_reset(&mtf_models[i]);
        st = inverse_bwt_rle(block, block_len, transform_index, randomized, dst, out_size, &op);
        if (st != OST_OK) break;
    }

    free(block);
    if (st != OST_OK || dec.error || op != out_size) {
        free(dst);
        return st != OST_OK ? st : OST_ERR_BAD_FORMAT;
    }
    *out = dst;
    *actual = op;
    return OST_OK;
}
