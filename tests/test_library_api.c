#include <openstuffit/openstuffit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int check_unicode_api(void) {
    char *out = NULL;
    ost_unicode_normalization mode = OST_UNICODE_NORMALIZE_NONE;

    if (strcmp(ost_status_string(OST_OK), "ok") != 0) return 1;
    if (strcmp(ost_wrapper_kind_string(OST_WRAPPER_RAW), "raw") != 0) return 1;
    if (strcmp(ost_format_kind_string(OST_FORMAT_SIT_CLASSIC), "sit-classic") != 0) return 1;
    if (!ost_parse_unicode_normalization("nfc", &mode) || mode != OST_UNICODE_NORMALIZE_NFC) return 1;
    if (strcmp(ost_unicode_normalization_name(OST_UNICODE_NORMALIZE_NFD), "nfd") != 0) return 1;
    if (ost_normalize_utf8("Cafe\xcc\x81", OST_UNICODE_NORMALIZE_NFC, &out) != OST_OK) return 1;
    if (!out || strcmp(out, "Caf\xc3\xa9") != 0) {
        free(out);
        return 1;
    }
    free(out);
    return 0;
}

static int check_macroman_api(void) {
    const uint8_t name[] = {'C', 'a', 'f', 0x8e};
    char *utf8 = ost_macroman_to_utf8(name, sizeof(name));
    if (!utf8) return 1;
    if (strcmp(utf8, "Caf\xc3\xa9") != 0) {
        free(utf8);
        return 1;
    }
    free(utf8);
    return 0;
}

static int check_binhex_api(void) {
    const char *path = "reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx";
    ost_buffer buf;
    ost_binhex_file hqx;
    ost_status st;

    memset(&buf, 0, sizeof(buf));
    memset(&hqx, 0, sizeof(hqx));
    st = ost_read_file(path, &buf);
    if (st != OST_OK) return 1;
    st = ost_binhex_decode(buf.data, buf.size, &hqx);
    ost_buffer_free(&buf);
    if (st != OST_OK) return 1;
    if (!hqx.name || hqx.data_fork.size == 0 || memcmp(hqx.finder_type, "SITD", 4) != 0) {
        ost_binhex_free(&hqx);
        return 1;
    }
    ost_binhex_free(&hqx);
    return 0;
}

int main(void) {
    const char *path = "reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit";
    const char *out_dir = "/tmp/openstuffit_api_extract";
    const char *out_file = "/tmp/openstuffit_api_extract/testfile.txt";
    ost_buffer buf;
    ost_detection det;
    ost_archive archive;
    ost_archive_handle *handle = NULL;
    ost_parse_options parse_options;
    ost_extract_options extract_options;
    ost_status st;

    if (check_unicode_api() != 0) {
        fprintf(stderr, "unicode API check failed\n");
        return 1;
    }
    if (check_macroman_api() != 0) {
        fprintf(stderr, "MacRoman API check failed\n");
        return 1;
    }
    if (check_binhex_api() != 0) {
        fprintf(stderr, "BinHex API check failed\n");
        return 1;
    }

    memset(&buf, 0, sizeof(buf));
    memset(&det, 0, sizeof(det));
    memset(&archive, 0, sizeof(archive));
    memset(&extract_options, 0, sizeof(extract_options));
    ost_parse_options_init(&parse_options);
    parse_options.unicode_normalization = OST_UNICODE_NORMALIZE_NFC;

    st = ost_read_file(path, &buf);
    if (st != OST_OK) {
        fprintf(stderr, "ost_read_file failed: %s\n", ost_status_string(st));
        return 1;
    }

    st = ost_detect_buffer(buf.data, buf.size, ost_basename_const(path), &det);
    if (st != OST_OK) {
        fprintf(stderr, "ost_detect_buffer failed: %s\n", ost_status_string(st));
        ost_buffer_free(&buf);
        return 1;
    }
    if (det.wrapper != OST_WRAPPER_RAW || det.format != OST_FORMAT_SIT_CLASSIC) {
        fprintf(stderr, "unexpected detection wrapper=%s format=%s\n",
                ost_wrapper_kind_string(det.wrapper),
                ost_format_kind_string(det.format));
        ost_buffer_free(&buf);
        return 1;
    }

    st = ost_archive_parse(buf.data, buf.size, &det, &archive);
    if (st != OST_OK) {
        fprintf(stderr, "ost_archive_parse failed: %s\n", ost_status_string(st));
        ost_buffer_free(&buf);
        return 1;
    }
    if (archive.entry_count == 0 || archive.entries[0].path == NULL) {
        fprintf(stderr, "archive parse returned no entries\n");
        ost_archive_free(&archive);
        ost_buffer_free(&buf);
        return 1;
    }

    st = ost_archive_handle_open_file(path, &parse_options, &handle);
    if (st != OST_OK || !handle || ost_archive_handle_entry_count(handle) == 0) {
        fprintf(stderr, "ost_archive_handle_open_file failed: %s\n", ost_status_string(st));
        ost_archive_free(&archive);
        ost_buffer_free(&buf);
        return 1;
    }
    if (!ost_archive_handle_detection(handle) ||
        ost_archive_handle_detection(handle)->format != OST_FORMAT_SIT_CLASSIC ||
        !ost_archive_handle_entry(handle, 0)) {
        fprintf(stderr, "archive handle accessors failed\n");
        ost_archive_handle_free(handle);
        ost_archive_free(&archive);
        ost_buffer_free(&buf);
        return 1;
    }
    ost_archive_handle_free(handle);
    handle = NULL;

    if (system("rm -rf /tmp/openstuffit_api_extract") != 0) {
        fprintf(stderr, "failed to clean API extract output\n");
        ost_archive_free(&archive);
        ost_buffer_free(&buf);
        return 1;
    }
    extract_options.output_dir = out_dir;
    extract_options.forks = OST_FORKS_SKIP;
    extract_options.finder = OST_FINDER_SKIP;
    extract_options.collision = OST_COLLISION_OVERWRITE;
    extract_options.preserve_time = false;
    extract_options.verify_crc = true;
    st = ost_archive_extract(&archive, &extract_options);
    if (st != OST_OK || extract_options.extracted_files == 0 || !file_exists(out_file)) {
        fprintf(stderr, "ost_archive_extract failed: %s extracted=%zu\n",
                ost_status_string(st), extract_options.extracted_files);
        ost_archive_free(&archive);
        ost_buffer_free(&buf);
        return 1;
    }

    st = ost_archive_handle_open_buffer(buf.data, buf.size, ost_basename_const(path), &parse_options, &handle);
    if (st != OST_OK || !handle) {
        fprintf(stderr, "ost_archive_handle_open_buffer failed: %s\n", ost_status_string(st));
        ost_archive_free(&archive);
        ost_buffer_free(&buf);
        return 1;
    }
    ost_archive_handle_free(handle);

    ost_archive_free(&archive);
    ost_buffer_free(&buf);
    return 0;
}
