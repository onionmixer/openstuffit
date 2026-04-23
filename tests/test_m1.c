#include "ost.h"
#include "ost_archive.h"
#include "ost_crc16.h"
#include "ost_decompress.h"
#include "ost_detect.h"
#include "ost_endian.h"
#include "ost_io.h"
#include "ost_macroman.h"
#include "ost_unicode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check_failed(const char *file, int line, const char *expr) {
    fprintf(stderr, "CHECK failed at %s:%d: %s\n", file, line, expr);
    failures++;
}

#define CHECK(expr) do { \
    if (!(expr)) check_failed(__FILE__, __LINE__, #expr); \
} while (0)

#define REQUIRE(expr) do { \
    if (!(expr)) { \
        check_failed(__FILE__, __LINE__, #expr); \
        return; \
    } \
} while (0)

static void test_endian(void) {
    uint8_t data[] = {0x12, 0x34, 0x56, 0x78};
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    CHECK(ost_read_u16_be(data, sizeof(data), 0, &u16) && u16 == 0x1234);
    CHECK(ost_read_u32_be(data, sizeof(data), 0, &u32) && u32 == 0x12345678);
    CHECK(ost_read_u16_le(data, sizeof(data), 0, &u16) && u16 == 0x3412);
    CHECK(!ost_read_u32_be(data, sizeof(data), 1, &u32));
}

static void test_crc(void) {
    const uint8_t sample[] = "123456789";
    CHECK(ost_crc16_ibm(sample, 9) == 0xbb3d);
}

static void test_rle90(void) {
    const uint8_t compressed[] = {'A', 0x90, 4, 0x90, 0, 'B'};
    ost_fork_info fork;
    memset(&fork, 0, sizeof(fork));
    fork.present = true;
    fork.offset = 0;
    fork.compressed_size = sizeof(compressed);
    fork.uncompressed_size = 6;
    fork.method = 1;
    ost_decompressed out;
    ost_status st = ost_decompress_fork(compressed, sizeof(compressed), &fork, &out);
    CHECK(st == OST_OK);
    if (st == OST_OK) {
        CHECK(out.size == 6);
        CHECK(memcmp(out.data, "AAAA\x90" "B", 6) == 0);
        ost_decompressed_free(&out);
    }
}

static void bw_put_bit(uint8_t *buf, size_t *bitpos, unsigned bit) {
    if (bit) buf[*bitpos / 8u] |= (uint8_t)(1u << (7u - (unsigned)(*bitpos % 8u)));
    (*bitpos)++;
}

static void bw_put_byte(uint8_t *buf, size_t *bitpos, uint8_t byte) {
    uint32_t v = byte;
    for (unsigned i = 0; i < 8u; i++) bw_put_bit(buf, bitpos, (unsigned)((v >> (7u - i)) & 1u));
}

static void test_huffman3(void) {
    uint8_t compressed[8];
    memset(compressed, 0, sizeof(compressed));
    size_t bitpos = 0;

    bw_put_bit(compressed, &bitpos, 0);      /* root is branch */
    bw_put_bit(compressed, &bitpos, 1);      /* left leaf */
    bw_put_byte(compressed, &bitpos, 'A');
    bw_put_bit(compressed, &bitpos, 1);      /* right leaf */
    bw_put_byte(compressed, &bitpos, 'B');
    bw_put_bit(compressed, &bitpos, 0);      /* A */
    bw_put_bit(compressed, &bitpos, 1);      /* B */
    bw_put_bit(compressed, &bitpos, 0);      /* A */
    bw_put_bit(compressed, &bitpos, 1);      /* B */

    ost_fork_info fork;
    memset(&fork, 0, sizeof(fork));
    fork.present = true;
    fork.offset = 0;
    fork.compressed_size = (bitpos + 7u) / 8u;
    fork.uncompressed_size = 4;
    fork.method = 3;
    ost_decompressed out;
    ost_status st = ost_decompress_fork(compressed, sizeof(compressed), &fork, &out);
    CHECK(st == OST_OK);
    if (st == OST_OK) {
        CHECK(out.size == 4);
        CHECK(memcmp(out.data, "ABAB", 4) == 0);
        ost_decompressed_free(&out);
    }
}

static void test_detect_fixture(const char *path, ost_wrapper_kind wrapper, ost_format_kind format) {
    ost_buffer buf;
    ost_detection det;
    ost_status st = ost_read_file(path, &buf);
    CHECK(st == OST_OK);
    if (st != OST_OK) return;
    st = ost_detect_buffer(buf.data, buf.size, ost_basename_const(path), &det);
    if (format == OST_FORMAT_SIT_CLASSIC || format == OST_FORMAT_SIT5) CHECK(st == OST_OK);
    else CHECK(st == OST_ERR_UNSUPPORTED);
    CHECK(det.wrapper == wrapper);
    CHECK(det.format == format);
    if (format == OST_FORMAT_SIT_CLASSIC || format == OST_FORMAT_SIT5) CHECK(det.supported);
    ost_buffer_free(&buf);
}

static void test_list_fixture(const char *path, size_t min_entries) {
    ost_buffer buf;
    ost_detection det;
    ost_archive archive;
    ost_status st = ost_read_file(path, &buf);
    CHECK(st == OST_OK);
    if (st != OST_OK) return;
    st = ost_detect_buffer(buf.data, buf.size, ost_basename_const(path), &det);
    CHECK(st == OST_OK);
    if (st == OST_OK) st = ost_archive_parse(buf.data, buf.size, &det, &archive);
    CHECK(st == OST_OK);
    if (st == OST_OK) {
        CHECK(archive.entry_count >= min_entries);
        CHECK(archive.entries[0].path != NULL);
        ost_archive_free(&archive);
    }
    ost_buffer_free(&buf);
}

static void test_macroman_to_utf8(void) {
    const uint8_t name[] = {'C', 'a', 'f', 0x8e, ' ', 0xdb};
    char *utf8 = ost_macroman_to_utf8(name, sizeof(name));
    REQUIRE(utf8 != NULL);
    CHECK(strcmp(utf8, "Caf\xc3\xa9 \xe2\x82\xac") == 0);
    free(utf8);
}

static void test_unicode_normalization(void) {
    char *out = NULL;
    CHECK(ost_normalize_utf8("Cafe\xcc\x81", OST_UNICODE_NORMALIZE_NFC, &out) == OST_OK);
    REQUIRE(out != NULL);
    CHECK(strcmp(out, "Caf\xc3\xa9") == 0);
    free(out);

    out = NULL;
    CHECK(ost_normalize_utf8("Caf\xc3\xa9", OST_UNICODE_NORMALIZE_NFD, &out) == OST_OK);
    REQUIRE(out != NULL);
    CHECK(strcmp(out, "Cafe\xcc\x81") == 0);
    free(out);

    out = NULL;
    CHECK(ost_normalize_utf8("\xe1\x84\x80\xe1\x85\xa1", OST_UNICODE_NORMALIZE_NFC, &out) == OST_OK);
    REQUIRE(out != NULL);
    CHECK(strcmp(out, "\xea\xb0\x80") == 0);
    free(out);

    out = NULL;
    CHECK(ost_normalize_utf8("\xea\xb0\x81", OST_UNICODE_NORMALIZE_NFD, &out) == OST_OK);
    REQUIRE(out != NULL);
    CHECK(strcmp(out, "\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8") == 0);
    free(out);

    ost_unicode_normalization mode = OST_UNICODE_NORMALIZE_NONE;
    CHECK(ost_parse_unicode_normalization("nfd", &mode) && mode == OST_UNICODE_NORMALIZE_NFD);
    CHECK(!ost_parse_unicode_normalization("bad", &mode));
}

int main(void) {
    test_endian();
    test_crc();
    test_macroman_to_utf8();
    test_unicode_normalization();
    test_rle90();
    test_huffman3();
    test_detect_fixture("reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit",
                        OST_WRAPPER_RAW, OST_FORMAT_SIT_CLASSIC);
    test_detect_fixture("reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea",
                        OST_WRAPPER_RAW, OST_FORMAT_SIT_CLASSIC);
    test_detect_fixture("reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin",
                        OST_WRAPPER_MACBINARY, OST_FORMAT_SIT_CLASSIC);
    test_detect_fixture("reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit",
                        OST_WRAPPER_RAW, OST_FORMAT_SIT5);
    test_detect_fixture("reference_repos/stuffit-test-files/build/testfile.stuffit7_dlx.mac9.sitx",
                        OST_WRAPPER_RAW, OST_FORMAT_SITX);
    test_list_fixture("reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit", 1);
    test_list_fixture("reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin", 1);
    test_list_fixture("reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit", 1);

    if (failures) {
        fprintf(stderr, "%d test failure(s)\n", failures);
        return 1;
    }
    puts("test_m1: ok");
    return 0;
}
