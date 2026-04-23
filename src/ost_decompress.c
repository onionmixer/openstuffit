#include "ost_decompress.h"

#include "ost_crypto.h"
#include "ost_sit13.h"
#include "ost_sit15.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint64_t bitbuf;
    unsigned bits;
} bitreader;

typedef struct {
    int parent;
    uint8_t byte;
} lzw_node;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t bitpos;
} be_bitreader;

typedef struct {
    int child[2];
} huff3_node;

static void br_init(bitreader *br, const uint8_t *data, size_t size) {
    br->data = data;
    br->size = size;
    br->pos = 0;
    br->bitbuf = 0;
    br->bits = 0;
}

static bool br_read_le(bitreader *br, unsigned n, uint32_t *out) {
    if (!br || !out || n > 24) return false;
    while (br->bits < n) {
        if (br->pos >= br->size) return false;
        br->bitbuf |= (uint64_t)br->data[br->pos++] << br->bits;
        br->bits += 8;
    }
    *out = (uint32_t)(br->bitbuf & ((((uint64_t)1) << n) - 1u));
    br->bitbuf >>= n;
    br->bits -= n;
    return true;
}

static void br_skip_le(bitreader *br, unsigned n) {
    uint32_t ignored = 0;
    while (n > 0) {
        unsigned chunk = n > 24 ? 24 : n;
        if (!br_read_le(br, chunk, &ignored)) return;
        n -= chunk;
    }
}

static ost_status alloc_output(size_t size, ost_decompressed *out) {
    out->data = NULL;
    out->size = 0;
    if (size == 0) return OST_OK;
    out->data = (uint8_t *)malloc(size);
    if (!out->data) return OST_ERR_NO_MEMORY;
    out->size = size;
    return OST_OK;
}

static ost_status decompress_store(const uint8_t *src, size_t src_size, size_t out_size, ost_decompressed *out) {
    ost_status st = alloc_output(src_size, out);
    if (st != OST_OK) return st;
    if (src_size) memcpy(out->data, src, src_size);
    (void)out_size;
    return OST_OK;
}

static ost_status decompress_rle90(const uint8_t *src, size_t src_size, size_t out_size, ost_decompressed *out) {
    ost_status st = alloc_output(out_size, out);
    if (st != OST_OK) return st;

    size_t ip = 0, op = 0;
    uint8_t repeated = 0;
    while (ip < src_size && op < out_size) {
        uint8_t b = src[ip++];
        if (b != 0x90) {
            repeated = b;
            out->data[op++] = b;
            continue;
        }
        if (ip >= src_size) return OST_ERR_BAD_FORMAT;
        uint8_t count = src[ip++];
        if (count == 0) {
            repeated = 0x90;
            out->data[op++] = 0x90;
        } else {
            if (count == 1) return OST_ERR_BAD_FORMAT;
            for (unsigned i = 0; i < (unsigned)count - 1u && op < out_size; i++) {
                out->data[op++] = repeated;
            }
        }
    }
    out->size = op;
    return op == out_size ? OST_OK : OST_ERR_BAD_FORMAT;
}

static void lzw_clear(lzw_node *nodes, int *num_symbols, int *prev_symbol, unsigned *symbol_size) {
    for (int i = 0; i < 256; i++) {
        nodes[i].parent = -1;
        nodes[i].byte = (uint8_t)i;
    }
    *num_symbols = 257;
    *prev_symbol = -1;
    *symbol_size = 9;
}

static uint8_t lzw_first_byte(const lzw_node *nodes, int symbol) {
    while (symbol >= 0 && nodes[symbol].parent >= 0) symbol = nodes[symbol].parent;
    return symbol >= 0 ? nodes[symbol].byte : 0;
}

static int lzw_output(const lzw_node *nodes, int symbol, uint8_t *out, size_t out_size, size_t *op) {
    uint8_t stack[16384];
    size_t n = 0;
    while (symbol >= 0) {
        if (n >= sizeof(stack)) return -1;
        stack[n++] = nodes[symbol].byte;
        symbol = nodes[symbol].parent;
    }
    while (n > 0) {
        if (*op >= out_size) return -1;
        out[(*op)++] = stack[--n];
    }
    return 0;
}

static ost_status decompress_lzw(const uint8_t *src, size_t src_size, size_t out_size, ost_decompressed *out) {
    ost_status st = alloc_output(out_size, out);
    if (st != OST_OK) return st;

    lzw_node nodes[16384];
    int num_symbols = 0;
    int prev_symbol = -1;
    unsigned symbol_size = 9;
    int symbol_counter = 0;
    bitreader br;
    br_init(&br, src, src_size);
    lzw_clear(nodes, &num_symbols, &prev_symbol, &symbol_size);

    size_t op = 0;
    while (op < out_size) {
        uint32_t code_u = 0;
        if (!br_read_le(&br, symbol_size, &code_u)) break;
        int code = (int)code_u;
        symbol_counter++;

        if (code == 256) {
            unsigned current_size = symbol_size;
            if (symbol_counter % 8) br_skip_le(&br, current_size * (8u - (unsigned)(symbol_counter % 8)));
            lzw_clear(nodes, &num_symbols, &prev_symbol, &symbol_size);
            symbol_counter = 0;
            continue;
        }

        if (prev_symbol < 0) {
            if (code >= num_symbols) return OST_ERR_BAD_FORMAT;
            prev_symbol = code;
            if (lzw_output(nodes, code, out->data, out_size, &op) != 0) return OST_ERR_BAD_FORMAT;
            continue;
        }

        int output_symbol = code;
        uint8_t postfix = 0;
        if (code < num_symbols) {
            postfix = lzw_first_byte(nodes, code);
        } else if (code == num_symbols) {
            postfix = lzw_first_byte(nodes, prev_symbol);
            output_symbol = num_symbols;
            nodes[output_symbol].parent = prev_symbol;
            nodes[output_symbol].byte = postfix;
        } else {
            return OST_ERR_BAD_FORMAT;
        }

        if (code != num_symbols && num_symbols < 16384) {
            nodes[num_symbols].parent = prev_symbol;
            nodes[num_symbols].byte = postfix;
            num_symbols++;
            if (num_symbols < 16384 && (num_symbols & (num_symbols - 1)) == 0 && symbol_size < 14) symbol_size++;
        } else if (code == num_symbols) {
            num_symbols++;
            if (num_symbols < 16384 && (num_symbols & (num_symbols - 1)) == 0 && symbol_size < 14) symbol_size++;
        }

        prev_symbol = output_symbol;
        if (lzw_output(nodes, output_symbol, out->data, out_size, &op) != 0) return OST_ERR_BAD_FORMAT;
    }

    out->size = op;
    return op == out_size ? OST_OK : OST_ERR_BAD_FORMAT;
}

static bool be_read_bit(be_bitreader *br, unsigned *out) {
    if (!br || !out || br->bitpos >= br->size * 8u) return false;
    size_t byte_pos = br->bitpos / 8u;
    unsigned bit_pos = 7u - (unsigned)(br->bitpos % 8u);
    *out = (unsigned)((br->data[byte_pos] >> bit_pos) & 1u);
    br->bitpos++;
    return true;
}

static bool be_read_bits(be_bitreader *br, unsigned n, uint32_t *out) {
    if (!out || n > 24) return false;
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned bit = 0;
        if (!be_read_bit(br, &bit)) return false;
        v = (v << 1) | bit;
    }
    *out = v;
    return true;
}

static ost_status huff3_new_node(huff3_node *nodes, size_t max_nodes, size_t *count, int *index) {
    if (*count >= max_nodes || *count > (size_t)INT_MAX) return OST_ERR_BAD_FORMAT;
    nodes[*count].child[0] = 0;
    nodes[*count].child[1] = 0;
    *index = (int)(*count);
    (*count)++;
    return OST_OK;
}

static ost_status huff3_parse_tree(be_bitreader *br,
                                   huff3_node *nodes,
                                   size_t max_nodes,
                                   size_t *count,
                                   int node,
                                   unsigned depth) {
    if (depth > 512u || node < 0 || (size_t)node >= *count) return OST_ERR_BAD_FORMAT;
    unsigned bit = 0;
    if (!be_read_bit(br, &bit)) return OST_ERR_BAD_FORMAT;
    if (bit) {
        uint32_t val = 0;
        if (!be_read_bits(br, 8, &val)) return OST_ERR_BAD_FORMAT;
        nodes[node].child[0] = -(int)val - 1;
        nodes[node].child[1] = -(int)val - 1;
        return OST_OK;
    }

    int left = 0, right = 0;
    ost_status st = huff3_new_node(nodes, max_nodes, count, &left);
    if (st != OST_OK) return st;
    nodes[node].child[0] = left;
    st = huff3_parse_tree(br, nodes, max_nodes, count, left, depth + 1u);
    if (st != OST_OK) return st;

    st = huff3_new_node(nodes, max_nodes, count, &right);
    if (st != OST_OK) return st;
    nodes[node].child[1] = right;
    return huff3_parse_tree(br, nodes, max_nodes, count, right, depth + 1u);
}

static ost_status huff3_decode_symbol(be_bitreader *br, const huff3_node *nodes, size_t count, uint8_t *out) {
    int node = 0;
    for (;;) {
        if (node < 0 || (size_t)node >= count) return OST_ERR_BAD_FORMAT;
        int left = nodes[node].child[0];
        int right = nodes[node].child[1];
        if (left < 0 && left == right) {
            *out = (uint8_t)(-left - 1);
            return OST_OK;
        }
        unsigned bit = 0;
        if (!be_read_bit(br, &bit)) return OST_ERR_BAD_FORMAT;
        int next = nodes[node].child[bit];
        if (next < 0) {
            *out = (uint8_t)(-next - 1);
            return OST_OK;
        }
        node = next;
    }
}

static ost_status decompress_huffman3(const uint8_t *src, size_t src_size, size_t out_size, ost_decompressed *out) {
    ost_status st = alloc_output(out_size, out);
    if (st != OST_OK) return st;

    be_bitreader br;
    br.data = src;
    br.size = src_size;
    br.bitpos = 0;

    huff3_node nodes[511];
    size_t count = 0;
    int root = 0;
    st = huff3_new_node(nodes, sizeof(nodes) / sizeof(nodes[0]), &count, &root);
    if (st != OST_OK) return st;
    st = huff3_parse_tree(&br, nodes, sizeof(nodes) / sizeof(nodes[0]), &count, root, 0);
    if (st != OST_OK) return st;

    for (size_t i = 0; i < out_size; i++) {
        st = huff3_decode_symbol(&br, nodes, count, &out->data[i]);
        if (st != OST_OK) return st;
    }
    out->size = out_size;
    return OST_OK;
}

static ost_status decompress_plain(const uint8_t *src, size_t src_size, const ost_fork_info *fork, ost_decompressed *out) {
    size_t out_size = (size_t)fork->uncompressed_size;
    switch (fork->method & 0x0f) {
        case 0: return decompress_store(src, src_size, out_size, out);
        case 1: return decompress_rle90(src, src_size, out_size, out);
        case 2: return decompress_lzw(src, src_size, out_size, out);
        case 3: return decompress_huffman3(src, src_size, out_size, out);
        case 13:
            return ost_sit13_decompress(src, src_size, out_size, &out->data, &out->size);
        case 14:
            if (!fork->method14_deflate) return OST_ERR_UNSUPPORTED;
            {
                ost_status st = alloc_output(out_size, out);
                if (st != OST_OK) return st;
                z_stream zs;
                memset(&zs, 0, sizeof(zs));
                zs.next_in = (Bytef *)src;
                zs.avail_in = (uInt)src_size;
                zs.next_out = out->data;
                zs.avail_out = (uInt)out_size;
                if ((size_t)zs.avail_in != src_size || (size_t)zs.avail_out != out_size) {
                    free(out->data);
                    out->data = NULL;
                    out->size = 0;
                    return OST_ERR_UNSUPPORTED;
                }
                int zr = inflateInit2(&zs, -MAX_WBITS);
                if (zr != Z_OK) {
                    free(out->data);
                    out->data = NULL;
                    out->size = 0;
                    return OST_ERR_BAD_FORMAT;
                }
                zr = inflate(&zs, Z_FINISH);
                inflateEnd(&zs);
                if (zr != Z_STREAM_END || zs.total_out != out_size) {
                    free(out->data);
                    out->data = NULL;
                    out->size = 0;
                    return OST_ERR_BAD_FORMAT;
                }
                out->size = out_size;
                return OST_OK;
            }
        case 15:
            return ost_sit15_decompress(src, src_size, out_size, &out->data, &out->size);
        default: return OST_ERR_UNSUPPORTED;
    }
}

ost_status ost_decompress_fork_with_password(const uint8_t *data,
                                             size_t data_size,
                                             const ost_fork_info *fork,
                                             const char *password,
                                             ost_decompressed *out) {
    if (!data || !fork || !out) return OST_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!fork->present) return OST_OK;
    if (fork->offset > data_size || fork->compressed_size > data_size - fork->offset) return OST_ERR_BAD_FORMAT;

    const uint8_t *src = data + fork->offset;
    size_t src_size = (size_t)fork->compressed_size;
    if (!fork->encrypted) return decompress_plain(src, src_size, fork, out);
    if (!password) return OST_ERR_PASSWORD_REQUIRED;
    if (fork->encryption == OST_ENCRYPTION_CLASSIC_DES) {
        if ((src_size & 7u) != 0) return OST_ERR_BAD_FORMAT;
        if ((size_t)fork->classic_padding > src_size) return OST_ERR_BAD_FORMAT;
        uint8_t classic_key[16];
        if (!ost_classic_des_key((const uint8_t *)password,
                                 strlen(password),
                                 fork->classic_mkey,
                                 fork->classic_entry_key,
                                 classic_key)) {
            return OST_ERR_PASSWORD_BAD;
        }
        uint8_t *decrypted = (uint8_t *)malloc(src_size ? src_size : 1u);
        if (!decrypted) return OST_ERR_NO_MEMORY;
        ost_classic_des_decrypt_payload(classic_key, src, src_size, decrypted);
        ost_status st = decompress_plain(decrypted, src_size - (size_t)fork->classic_padding, fork, out);
        free(decrypted);
        return st;
    }
    if (fork->encryption != OST_ENCRYPTION_SIT5_RC4) return OST_ERR_UNSUPPORTED;

    uint8_t archive_key[5];
    uint8_t archive_hash[5];
    ost_md5_5((const uint8_t *)password, strlen(password), archive_key);
    ost_md5_5(archive_key, sizeof(archive_key), archive_hash);
    if (memcmp(archive_hash, fork->sit5_archive_hash, sizeof(archive_hash)) != 0) return OST_ERR_PASSWORD_BAD;

    uint8_t rc4_key[10];
    memcpy(rc4_key, archive_key, sizeof(archive_key));
    memcpy(rc4_key + sizeof(archive_key), fork->sit5_entry_key, sizeof(fork->sit5_entry_key));
    uint8_t *decrypted = (uint8_t *)malloc(src_size ? src_size : 1u);
    if (!decrypted) return OST_ERR_NO_MEMORY;
    ost_rc4_crypt(rc4_key, sizeof(rc4_key), src, src_size, decrypted);
    ost_status st = decompress_plain(decrypted, src_size, fork, out);
    free(decrypted);
    return st;
}

ost_status ost_decompress_fork(const uint8_t *data,
                               size_t data_size,
                               const ost_fork_info *fork,
                               ost_decompressed *out) {
    return ost_decompress_fork_with_password(data, data_size, fork, NULL, out);
}

void ost_decompressed_free(ost_decompressed *out) {
    if (!out) return;
    free(out->data);
    out->data = NULL;
    out->size = 0;
}
