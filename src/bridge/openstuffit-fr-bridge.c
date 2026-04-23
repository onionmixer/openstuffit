#include <openstuffit/openstuffit.h>

#include "openstuffit_fr_bridge_json.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    ost_buffer raw;
    ost_binhex_file hqx;
    bool has_hqx;
    ost_detection detection;
    ost_archive_handle *handle;
} bridge_input;

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage:\n"
            "  openstuffit-fr-bridge identify --json <archive>\n"
            "  openstuffit-fr-bridge list --json <archive> [--password <text>] [--unicode-normalization none|nfc|nfd]\n"
            "  openstuffit-fr-bridge extract --output-dir <dir> <archive> [--password <text>] [--overwrite|--skip-existing|--rename-existing] [--forks skip|rsrc|appledouble|both|native] [--finder skip|sidecar] [--unicode-normalization none|nfc|nfd] [--entry <path>]...\n");
}

static int exit_code_for_status(ost_status st) {
    switch (st) {
        case OST_OK: return 0;
        case OST_ERR_UNSUPPORTED: return 2;
        case OST_ERR_BAD_FORMAT:
        case OST_ERR_CHECKSUM: return 3;
        case OST_ERR_PASSWORD_REQUIRED:
        case OST_ERR_PASSWORD_BAD: return 4;
        case OST_ERR_INVALID_ARGUMENT: return 5;
        case OST_ERR_SKIPPED: return 0;
        case OST_ERR_IO:
        case OST_ERR_NO_MEMORY: return 1;
    }
    return 1;
}

static int fail_status(const char *cmd, const char *path, ost_status st) {
    fprintf(stderr, "openstuffit-fr-bridge %s: %s: %s\n", cmd, path ? path : "", ost_status_string(st));
    return exit_code_for_status(st);
}

static bool parse_normalization(const char *text, ost_unicode_normalization *mode) {
    if (ost_parse_unicode_normalization(text, mode)) return true;
    fprintf(stderr, "unsupported --unicode-normalization mode: %s\n", text ? text : "");
    return false;
}

static bool parse_fork_mode(const char *text, ost_fork_mode *mode) {
    if (!text || !mode) return false;
    if (strcmp(text, "skip") == 0) *mode = OST_FORKS_SKIP;
    else if (strcmp(text, "rsrc") == 0) *mode = OST_FORKS_RSRC;
    else if (strcmp(text, "appledouble") == 0) *mode = OST_FORKS_APPLEDOUBLE;
    else if (strcmp(text, "both") == 0) *mode = OST_FORKS_BOTH;
    else if (strcmp(text, "native") == 0) *mode = OST_FORKS_NATIVE;
    else return false;
    return true;
}

static bool parse_finder_mode(const char *text, ost_finder_mode *mode) {
    if (!text || !mode) return false;
    if (strcmp(text, "skip") == 0) *mode = OST_FINDER_SKIP;
    else if (strcmp(text, "sidecar") == 0) *mode = OST_FINDER_SIDECAR;
    else return false;
    return true;
}

static void bridge_input_free(bridge_input *input) {
    if (!input) return;
    ost_archive_handle_free(input->handle);
    input->handle = NULL;
    if (input->has_hqx) {
        ost_binhex_free(&input->hqx);
        input->has_hqx = false;
    }
    ost_buffer_free(&input->raw);
    memset(input, 0, sizeof(*input));
}

static ost_status bridge_input_open(const char *path, ost_unicode_normalization normalization, bridge_input *out) {
    ost_parse_options options;
    const char *name = NULL;
    ost_status st;

    if (!path || !out) return OST_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    ost_parse_options_init(&options);
    options.unicode_normalization = normalization;

    st = ost_read_file(path, &out->raw);
    if (st != OST_OK) return st;

    name = ost_basename_const(path);
    st = ost_detect_buffer(out->raw.data, out->raw.size, name, &out->detection);
    if (st == OST_OK) {
        out->detection.supported = true;
        return ost_archive_handle_open_buffer(out->raw.data, out->raw.size, name, &options, &out->handle);
    }

    if (out->detection.wrapper == OST_WRAPPER_BINHEX) {
        ost_detection inner;
        st = ost_binhex_decode(out->raw.data, out->raw.size, &out->hqx);
        if (st != OST_OK) return st;
        out->has_hqx = true;
        if (!out->hqx.data_fork.data || out->hqx.data_fork.size == 0) return OST_ERR_UNSUPPORTED;

        st = ost_detect_buffer(out->hqx.data_fork.data,
                               out->hqx.data_fork.size,
                               out->hqx.name ? out->hqx.name : name,
                               &inner);
        inner.wrapper = OST_WRAPPER_BINHEX;
        inner.has_data_fork = true;
        inner.data_fork_offset = 0;
        inner.data_fork_size = out->hqx.data_fork.size;
        inner.has_resource_fork = out->hqx.resource_fork.size > 0;
        inner.resource_fork_offset = 0;
        inner.resource_fork_size = out->hqx.resource_fork.size;
        memcpy(inner.finder_type, out->hqx.finder_type, 4u);
        memcpy(inner.finder_creator, out->hqx.finder_creator, 4u);
        out->detection = inner;
        if (st != OST_OK) return st;

        out->detection.supported = true;
        return ost_archive_handle_open_buffer(out->hqx.data_fork.data,
                                              out->hqx.data_fork.size,
                                              out->hqx.name ? out->hqx.name : name,
                                              &options,
                                              &out->handle);
    }

    return st;
}

static uint64_t mtime_mac_to_unix(uint32_t mac_time) {
    const uint32_t delta = 2082844800u;
    if (mac_time < delta) return 0;
    return (uint64_t)(mac_time - delta);
}

static void print_identify_json(const bridge_input *input) {
    size_t count = input && input->handle ? ost_archive_handle_entry_count(input->handle) : 0;
    printf("{\"status\":\"ok\",\"wrapper\":\"%s\",\"format\":\"%s\",\"supported\":%s,\"entry_count\":%llu}\n",
           ost_wrapper_kind_string(input->detection.wrapper),
           ost_format_kind_string(input->detection.format),
           input->detection.supported ? "true" : "false",
           (unsigned long long)count);
}

static void print_list_json(const bridge_input *input) {
    size_t count = input && input->handle ? ost_archive_handle_entry_count(input->handle) : 0;
    printf("{\"status\":\"ok\",\"wrapper\":\"%s\",\"format\":\"%s\",\"entries\":[",
           ost_wrapper_kind_string(input->detection.wrapper),
           ost_format_kind_string(input->detection.format));
    for (size_t i = 0; i < count; i++) {
        const ost_entry *entry = ost_archive_handle_entry(input->handle, i);
        uint64_t mtime_unix = 0;
        uint64_t size = 0;
        bool encrypted = false;
        if (!entry) continue;
        if (i != 0) printf(",");
        mtime_unix = mtime_mac_to_unix(entry->modify_time_mac);
        size = entry->data_fork.uncompressed_size;
        encrypted = entry->data_fork.encrypted || entry->resource_fork.encrypted;
        printf("{\"index\":%llu,\"path\":", (unsigned long long)i);
        ost_fr_json_print_string(stdout, entry->path ? entry->path : "");
        printf(",\"is_dir\":%s,\"size\":%llu,\"mtime_unix\":%llu,\"encrypted\":%s,\"data_method\":%u,\"resource_method\":%u}",
               entry->is_dir ? "true" : "false",
               (unsigned long long)size,
               (unsigned long long)mtime_unix,
               encrypted ? "true" : "false",
               (unsigned)entry->data_fork.method,
               (unsigned)entry->resource_fork.method);
    }
    printf("]}\n");
}

static void print_extract_json(const ost_extract_options *options) {
    printf("{\"status\":\"ok\",\"extracted_files\":%llu,\"skipped_files\":%llu,\"unsupported_files\":%llu,\"checksum_errors\":%llu}\n",
           (unsigned long long)options->extracted_files,
           (unsigned long long)options->skipped_files,
           (unsigned long long)options->unsupported_files,
           (unsigned long long)options->checksum_errors);
}

static int cmd_identify(int argc, char **argv) {
    const char *input = NULL;
    bool json = false;
    bridge_input loaded;
    ost_status st;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = true;
        else if (argv[i][0] == '-') return 5;
        else if (!input) input = argv[i];
        else return 5;
    }
    if (!json || !input) {
        usage(stderr);
        return 5;
    }

    st = bridge_input_open(input, OST_UNICODE_NORMALIZE_NFC, &loaded);
    if (st != OST_OK) {
        bridge_input_free(&loaded);
        return fail_status("identify", input, st);
    }
    print_identify_json(&loaded);
    bridge_input_free(&loaded);
    return 0;
}

static int cmd_list(int argc, char **argv) {
    const char *input = NULL;
    const char *password = NULL;
    bool json = false;
    ost_unicode_normalization normalization = OST_UNICODE_NORMALIZE_NFC;
    bridge_input loaded;
    ost_status st;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = true;
        else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) password = argv[++i];
        else if (strcmp(argv[i], "--unicode-normalization") == 0 && i + 1 < argc) {
            if (!parse_normalization(argv[++i], &normalization)) return 5;
        } else if (argv[i][0] == '-') {
            usage(stderr);
            return 5;
        } else if (!input) {
            input = argv[i];
        } else {
            usage(stderr);
            return 5;
        }
    }
    if (!json || !input) {
        usage(stderr);
        return 5;
    }
    (void)password;

    st = bridge_input_open(input, normalization, &loaded);
    if (st != OST_OK) {
        bridge_input_free(&loaded);
        return fail_status("list", input, st);
    }
    print_list_json(&loaded);
    bridge_input_free(&loaded);
    return 0;
}

static int cmd_extract(int argc, char **argv) {
    const char *input = NULL;
    ost_extract_options options;
    const char **include_paths = NULL;
    size_t include_count = 0;
    size_t include_cap = 0;
    ost_unicode_normalization normalization = OST_UNICODE_NORMALIZE_NFC;
    bridge_input loaded;
    ost_status st;

    memset(&options, 0, sizeof(options));
    options.output_dir = ".";
    options.forks = OST_FORKS_SKIP;
    options.finder = OST_FINDER_SKIP;
    options.collision = OST_COLLISION_SKIP;
    options.preserve_time = true;
    options.verify_crc = true;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) options.output_dir = argv[++i];
        else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) options.password = argv[++i];
        else if (strcmp(argv[i], "--overwrite") == 0) options.collision = OST_COLLISION_OVERWRITE;
        else if (strcmp(argv[i], "--skip-existing") == 0) options.collision = OST_COLLISION_SKIP;
        else if (strcmp(argv[i], "--rename-existing") == 0) options.collision = OST_COLLISION_RENAME;
        else if (strcmp(argv[i], "--forks") == 0 && i + 1 < argc) {
            if (!parse_fork_mode(argv[++i], &options.forks)) {
                fprintf(stderr, "unsupported --forks mode: %s\n", argv[i]);
                free((void *)include_paths);
                return 5;
            }
        } else if (strcmp(argv[i], "--finder") == 0 && i + 1 < argc) {
            if (!parse_finder_mode(argv[++i], &options.finder)) {
                fprintf(stderr, "unsupported --finder mode: %s\n", argv[i]);
                free((void *)include_paths);
                return 5;
            }
        } else if (strcmp(argv[i], "--unicode-normalization") == 0 && i + 1 < argc) {
            if (!parse_normalization(argv[++i], &normalization)) {
                free((void *)include_paths);
                return 5;
            }
        } else if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc) {
            if (include_count == include_cap) {
                size_t next_cap = include_cap ? include_cap * 2u : 8u;
                const char **next_paths = (const char **)realloc((void *)include_paths, sizeof(*include_paths) * next_cap);
                if (!next_paths) {
                    fprintf(stderr, "openstuffit-fr-bridge extract: out of memory\n");
                    free((void *)include_paths);
                    return 1;
                }
                include_paths = next_paths;
                include_cap = next_cap;
            }
            include_paths[include_count++] = argv[++i];
        } else if (argv[i][0] == '-') {
            usage(stderr);
            free((void *)include_paths);
            return 5;
        } else if (!input) {
            input = argv[i];
        } else {
            usage(stderr);
            free((void *)include_paths);
            return 5;
        }
    }
    if (!input) {
        usage(stderr);
        free((void *)include_paths);
        return 5;
    }
    options.include_paths = include_paths;
    options.include_path_count = include_count;

    st = bridge_input_open(input, normalization, &loaded);
    if (st != OST_OK) {
        bridge_input_free(&loaded);
        free((void *)include_paths);
        return fail_status("extract", input, st);
    }

    st = ost_archive_handle_extract(loaded.handle, &options);
    if (st != OST_OK) {
        bridge_input_free(&loaded);
        free((void *)include_paths);
        return fail_status("extract", input, st);
    }

    print_extract_json(&options);
    bridge_input_free(&loaded);
    free((void *)include_paths);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 5;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(stdout);
        return 0;
    }
    if (strcmp(argv[1], "identify") == 0) return cmd_identify(argc - 2, argv + 2);
    if (strcmp(argv[1], "list") == 0) return cmd_list(argc - 2, argv + 2);
    if (strcmp(argv[1], "extract") == 0) return cmd_extract(argc - 2, argv + 2);
    usage(stderr);
    return 5;
}
