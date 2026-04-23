#include "ost_crypto.h"

#include <string.h>

typedef struct {
    uint32_t h[4];
    uint64_t len;
    uint8_t buf[64];
    size_t buf_len;
} md5_ctx;

static uint32_t rol32(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32u - n));
}

static uint32_t ror32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static uint32_t load_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void store_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void md5_transform(md5_ctx *ctx, const uint8_t block[64]) {
    static const uint32_t k[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
    };
    static const uint32_t s[64] = {
        7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u,
        5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u,
        4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u,
        6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u
    };
    uint32_t m[16];
    for (size_t i = 0; i < 16u; i++) m[i] = load_le32(block + i * 4u);

    uint32_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3];
    for (uint32_t i = 0; i < 64u; i++) {
        uint32_t f, g;
        if (i < 16u) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32u) {
            f = (d & b) | ((~d) & c);
            g = (5u * i + 1u) & 15u;
        } else if (i < 48u) {
            f = b ^ c ^ d;
            g = (3u * i + 5u) & 15u;
        } else {
            f = c ^ (b | (~d));
            g = (7u * i) & 15u;
        }
        uint32_t tmp = d;
        d = c;
        c = b;
        b += rol32(a + f + k[i] + m[g], s[i]);
        a = tmp;
    }
    ctx->h[0] += a;
    ctx->h[1] += b;
    ctx->h[2] += c;
    ctx->h[3] += d;
}

static void md5_init(md5_ctx *ctx) {
    ctx->h[0] = 0x67452301u;
    ctx->h[1] = 0xefcdab89u;
    ctx->h[2] = 0x98badcfeu;
    ctx->h[3] = 0x10325476u;
    ctx->len = 0;
    ctx->buf_len = 0;
}

static void md5_update(md5_ctx *ctx, const uint8_t *data, size_t size) {
    ctx->len += (uint64_t)size * 8u;
    while (size > 0) {
        size_t n = 64u - ctx->buf_len;
        if (n > size) n = size;
        memcpy(ctx->buf + ctx->buf_len, data, n);
        ctx->buf_len += n;
        data += n;
        size -= n;
        if (ctx->buf_len == 64u) {
            md5_transform(ctx, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

static void md5_final(md5_ctx *ctx, uint8_t out[16]) {
    uint8_t pad[64] = {0x80};
    uint8_t len_le[8];
    for (size_t i = 0; i < 8u; i++) len_le[i] = (uint8_t)(ctx->len >> (8u * i));
    size_t pad_len = ctx->buf_len < 56u ? 56u - ctx->buf_len : 120u - ctx->buf_len;
    md5_update(ctx, pad, pad_len);
    md5_update(ctx, len_le, 8u);
    for (size_t i = 0; i < 4u; i++) store_le32(out + i * 4u, ctx->h[i]);
}

void ost_md5_5(const uint8_t *data, size_t size, uint8_t out[5]) {
    uint8_t full[16];
    md5_ctx ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, size);
    md5_final(&ctx, full);
    memcpy(out, full, 5u);
}

void ost_rc4_crypt(const uint8_t *key, size_t key_size, const uint8_t *src, size_t size, uint8_t *dst) {
    uint8_t s[256];
    uint8_t i = 0, j = 0;
    for (unsigned n = 0; n < 256u; n++) s[n] = (uint8_t)n;
    for (unsigned n = 0; n < 256u; n++) {
        j = (uint8_t)(j + s[n] + key[n % key_size]);
        uint8_t tmp = s[n];
        s[n] = s[j];
        s[j] = tmp;
    }
    i = 0;
    j = 0;
    for (size_t n = 0; n < size; n++) {
        i = (uint8_t)(i + 1u);
        j = (uint8_t)(j + s[i]);
        uint8_t tmp = s[i];
        s[i] = s[j];
        s[j] = tmp;
        dst[n] = (uint8_t)(src[n] ^ s[(uint8_t)(s[i] + s[j])]);
    }
}

typedef struct {
    uint32_t subkeys[16][2];
} classic_des_schedule;

static uint32_t reverse_bits32(uint32_t v) {
    uint32_t out = 0;
    for (unsigned i = 0; i < 32u; i++) {
        out = (out << 1) | (v & 1u);
        v >>= 1;
    }
    return out;
}

static uint8_t reverse_bits6(uint8_t v) {
    uint8_t out = 0;
    for (unsigned i = 0; i < 6u; i++) {
        out = (uint8_t)(((unsigned)out << 1) | ((unsigned)v & 1u));
        v >>= 1;
    }
    return out;
}

static uint8_t classic_nibble(const uint8_t key[8], int n) {
    return (uint8_t)((key[(n & 0x0f) >> 1] >> (((n ^ 1) & 1) << 2)) & 0x0f);
}

static void classic_des_set_key(const uint8_t key[8], classic_des_schedule *ks) {
    for (int i = 0; i < 16; i++) {
        uint32_t subkey1 = (uint32_t)((classic_nibble(key, i) >> 2) | (classic_nibble(key, i + 13) << 2));
        subkey1 |= (uint32_t)((classic_nibble(key, i + 11) >> 2) | (classic_nibble(key, i + 6) << 2)) << 8;
        subkey1 |= (uint32_t)((classic_nibble(key, i + 3) >> 2) | (classic_nibble(key, i + 10) << 2)) << 16;
        subkey1 |= (uint32_t)((classic_nibble(key, i + 8) >> 2) | (classic_nibble(key, i + 1) << 2)) << 24;

        uint32_t subkey0 = (uint32_t)((classic_nibble(key, i + 9) | (classic_nibble(key, i) << 4)) & 0x3f);
        subkey0 |= (uint32_t)((classic_nibble(key, i + 2) | (classic_nibble(key, i + 11) << 4)) & 0x3f) << 8;
        subkey0 |= (uint32_t)((classic_nibble(key, i + 14) | (classic_nibble(key, i + 3) << 4)) & 0x3f) << 16;
        subkey0 |= (uint32_t)((classic_nibble(key, i + 5) | (classic_nibble(key, i + 8) << 4)) & 0x3f) << 24;

        ks->subkeys[i][0] = reverse_bits32(subkey1);
        ks->subkeys[i][1] = reverse_bits32(subkey0);
    }
}

static uint32_t classic_des_sptrans(unsigned box, uint8_t idx) {
    static const uint8_t sbox[8][64] = {
        {14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7, 0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8, 4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0, 15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13},
        {15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10, 3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5, 0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15, 13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9},
        {10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8, 13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1, 13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7, 1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12},
        {7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15, 13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9, 10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4, 3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14},
        {2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9, 14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6, 4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14, 11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3},
        {12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11, 10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8, 9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6, 4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13},
        {4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1, 13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6, 1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2, 6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12},
        {13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7, 1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2, 7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8, 2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11}
    };
    static const uint32_t masks[8][4] = {
        {0x00000002u, 0x02000000u, 0x00080000u, 0x00000800u},
        {0x00100000u, 0x00000010u, 0x40000000u, 0x00008000u},
        {0x00000100u, 0x00000001u, 0x00040000u, 0x04000000u},
        {0x00000008u, 0x00001000u, 0x00400000u, 0x10000000u},
        {0x00000020u, 0x08000000u, 0x00010000u, 0x00000400u},
        {0x00200000u, 0x00002000u, 0x80000000u, 0x00000040u},
        {0x00000200u, 0x01000000u, 0x00004000u, 0x00000004u},
        {0x00800000u, 0x00020000u, 0x20000000u, 0x00000080u}
    };
    uint8_t x = reverse_bits6(idx);
    unsigned row = (unsigned)(((x & 0x20u) >> 4) | (x & 1u));
    unsigned col = (unsigned)((x >> 1) & 0x0fu);
    uint8_t val = sbox[box][row * 16u + col];
    uint32_t out = 0;
    for (unsigned i = 0; i < 4u; i++) {
        if (val & (uint8_t)(1u << i)) out |= masks[box][i];
    }
    return out;
}

static void classic_des_round(uint32_t *left, uint32_t right, const uint32_t subkey[2]) {
    uint32_t u = right ^ subkey[0];
    uint32_t t = ror32(right, 4u) ^ subkey[1];
    *left ^= classic_des_sptrans(0, (uint8_t)((u >> 2) & 0x3fu)) ^
             classic_des_sptrans(2, (uint8_t)((u >> 10) & 0x3fu)) ^
             classic_des_sptrans(4, (uint8_t)((u >> 18) & 0x3fu)) ^
             classic_des_sptrans(6, (uint8_t)((u >> 26) & 0x3fu)) ^
             classic_des_sptrans(1, (uint8_t)((t >> 2) & 0x3fu)) ^
             classic_des_sptrans(3, (uint8_t)((t >> 10) & 0x3fu)) ^
             classic_des_sptrans(5, (uint8_t)((t >> 18) & 0x3fu)) ^
             classic_des_sptrans(7, (uint8_t)((t >> 26) & 0x3fu));
}

static void classic_des_crypt_block(uint8_t data[8], const classic_des_schedule *ks, bool enc) {
    uint32_t left = reverse_bits32(load_be32(data));
    uint32_t right = reverse_bits32(load_be32(data + 4));
    right = ror32(right, 29u);
    left = ror32(left, 29u);

    if (enc) {
        for (int i = 0; i < 16; i += 2) {
            classic_des_round(&left, right, ks->subkeys[i]);
            classic_des_round(&right, left, ks->subkeys[i + 1]);
        }
    } else {
        for (int i = 15; i > 0; i -= 2) {
            classic_des_round(&left, right, ks->subkeys[i]);
            classic_des_round(&right, left, ks->subkeys[i - 1]);
        }
    }

    left = ror32(left, 3u);
    right = ror32(right, 3u);
    store_be32(data, reverse_bits32(right));
    store_be32(data + 4, reverse_bits32(left));
}

bool ost_classic_des_key(const uint8_t *password,
                         size_t password_size,
                         const uint8_t mkey[8],
                         const uint8_t entry_key[16],
                         uint8_t out_key[16]) {
    if (!password || !mkey || !entry_key || !out_key) return false;
    classic_des_schedule ks;
    uint8_t archive_iv[8];
    uint8_t archive_key[8] = {0x01u, 0x23u, 0x45u, 0x67u, 0x89u, 0xabu, 0xcdu, 0xefu};

    classic_des_set_key(archive_key, &ks);
    size_t pos = 0;
    do {
        uint8_t passblock[8] = {0};
        if (pos < password_size) {
            size_t copybytes = password_size - pos >= 8u ? 8u : password_size - pos;
            memcpy(passblock, password + pos, copybytes);
        }
        for (size_t i = 0; i < 8u; i++) archive_key[i] ^= passblock[i] & 0x7fu;
        classic_des_crypt_block(archive_key, &ks, true);
        pos += 8u;
    } while (pos < password_size);

    memcpy(archive_iv, mkey, 8u);
    classic_des_set_key(archive_key, &ks);
    classic_des_crypt_block(archive_iv, &ks, false);

    uint8_t verify[8] = {0, 0, 0, 0, 0, 0, 0, 4};
    memcpy(verify, archive_iv, 4u);
    classic_des_set_key(archive_key, &ks);
    classic_des_crypt_block(verify, &ks, true);
    if (memcmp(verify + 4, archive_iv + 4, 4u) != 0) return false;

    uint8_t file_key[8];
    uint8_t file_iv[8];
    memcpy(file_key, entry_key, 8u);
    memcpy(file_iv, entry_key + 8, 8u);
    classic_des_set_key(archive_key, &ks);
    classic_des_crypt_block(file_key, &ks, false);
    for (size_t i = 0; i < 8u; i++) file_key[i] ^= archive_iv[i];
    classic_des_set_key(file_key, &ks);
    classic_des_crypt_block(file_iv, &ks, false);

    memcpy(out_key, file_key, 8u);
    memcpy(out_key + 8, file_iv, 8u);
    return true;
}

void ost_classic_des_decrypt_payload(const uint8_t key[16], const uint8_t *src, size_t size, uint8_t *dst) {
    uint32_t a = load_be32(key);
    uint32_t b = load_be32(key + 4);
    uint32_t c = load_be32(key + 8);
    uint32_t d = load_be32(key + 12);
    size_t pos = 0;
    while (pos + 8u <= size) {
        uint32_t left = load_be32(src + pos);
        uint32_t right = load_be32(src + pos + 4u);
        store_be32(dst + pos, left ^ a ^ c);
        store_be32(dst + pos + 4u, right ^ b ^ d);
        c = d;
        d = ror32(left ^ right ^ a ^ b ^ d, 1u);
        pos += 8u;
    }
}
