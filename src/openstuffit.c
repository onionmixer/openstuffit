#include "ost.h"
#include "ost_archive.h"
#include "ost_binhex.h"
#include "ost_detect.h"
#include "ost_dump.h"
#include "ost_extract.h"
#include "ost_io.h"
#include "ost_unicode.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    ost_buffer raw;
    ost_binhex_file hqx;
    bool is_hqx;
    const uint8_t *data;
    size_t size;
    ost_detection det;
} input_file;

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage:\n"
            "  openstuffit [--version] [--help]\n"
            "  openstuffit identify [--json] [--show-forks] <input>...\n"
            "  openstuffit list [-l|-L] [--json] [--unicode-normalization none|nfc|nfd] <input>\n"
            "  openstuffit extract [-o dir] [--password text] [--overwrite|--skip-existing|--rename-existing] [--preserve-time|--no-preserve-time] [--no-verify-crc] [--forks skip|rsrc|appledouble|both|native] [--finder skip|sidecar] [--unicode-normalization none|nfc|nfd] [--entry path]... <input>\n"
            "  openstuffit dump [--json] (--headers|--forks|--entry index-or-path|--hex offset:length) <input>\n");
}

static int exit_code_for_status(ost_status st) {
    switch (st) {
        case OST_OK: return 0;
        case OST_ERR_UNSUPPORTED: return 2;
        case OST_ERR_BAD_FORMAT:
        case OST_ERR_CHECKSUM: return 3;
        case OST_ERR_PASSWORD_REQUIRED:
        case OST_ERR_PASSWORD_BAD: return 4;
        case OST_ERR_SKIPPED: return 0;
        case OST_ERR_INVALID_ARGUMENT: return 5;
        case OST_ERR_IO:
        case OST_ERR_NO_MEMORY: return 1;
    }
    return 1;
}

static void print_fourcc_json(const uint8_t v[4]) {
    for (int i = 0; i < 4; i++) {
        unsigned char c = v[i];
        if (c >= 32 && c < 127 && c != '"' && c != '\\') putchar((int)c);
        else printf("\\u%04x", c);
    }
}

static void print_fourcc_text(const uint8_t v[4]) {
    for (int i = 0; i < 4; i++) {
        unsigned char c = v[i];
        putchar((c >= 32 && c < 127) ? (int)c : '.');
    }
}

static bool parse_normalization_option(const char *value, ost_unicode_normalization *mode) {
    if (ost_parse_unicode_normalization(value, mode)) return true;
    fprintf(stderr, "unsupported --unicode-normalization mode: %s\n", value ? value : "");
    return false;
}

static void input_file_free(input_file *input) {
    if (!input) return;
    ost_buffer_free(&input->raw);
    if (input->is_hqx) ost_binhex_free(&input->hqx);
    memset(input, 0, sizeof(*input));
}

static ost_status load_input_file(const char *path, input_file *input) {
    if (!path || !input) return OST_ERR_INVALID_ARGUMENT;
    memset(input, 0, sizeof(*input));
    ost_status st = ost_read_file(path, &input->raw);
    if (st != OST_OK) return st;

    st = ost_detect_buffer(input->raw.data, input->raw.size, ost_basename_const(path), &input->det);
    input->data = input->raw.data;
    input->size = input->raw.size;
    if (st == OST_OK) return OST_OK;

    if (input->det.wrapper != OST_WRAPPER_BINHEX) return st;

    ost_binhex_file hqx;
    st = ost_binhex_decode(input->raw.data, input->raw.size, &hqx);
    if (st != OST_OK) return st;
    if (!hqx.data_fork.data || hqx.data_fork.size == 0) {
        ost_binhex_free(&hqx);
        return OST_ERR_UNSUPPORTED;
    }

    ost_detection inner;
    st = ost_detect_buffer(hqx.data_fork.data, hqx.data_fork.size, hqx.name ? hqx.name : ost_basename_const(path), &inner);
    inner.wrapper = OST_WRAPPER_BINHEX;
    inner.has_data_fork = true;
    inner.data_fork_offset = 0;
    inner.data_fork_size = hqx.data_fork.size;
    inner.has_resource_fork = hqx.resource_fork.size > 0;
    inner.resource_fork_offset = 0;
    inner.resource_fork_size = hqx.resource_fork.size;
    memcpy(inner.finder_type, hqx.finder_type, 4);
    memcpy(inner.finder_creator, hqx.finder_creator, 4);
    if (st == OST_OK) inner.supported = true;

    input->hqx = hqx;
    input->is_hqx = true;
    input->data = input->hqx.data_fork.data;
    input->size = input->hqx.data_fork.size;
    input->det = inner;
    return st;
}

static int cmd_identify(int argc, char **argv) {
    bool json = false;
    bool show_forks = false;
    int first_input = 0;
    for (; first_input < argc; first_input++) {
        if (strcmp(argv[first_input], "--json") == 0) json = true;
        else if (strcmp(argv[first_input], "--show-forks") == 0) show_forks = true;
        else break;
    }
    if (first_input >= argc) {
        usage(stderr);
        return 5;
    }

    int worst = 0;
    for (int i = first_input; i < argc; i++) {
        input_file input;
        ost_status st = load_input_file(argv[i], &input);
        ost_detection det = input.det;
        if (st != OST_OK && !input.raw.data) memset(&det, 0, sizeof(det));

        if (json) {
            printf("{\"input\":\"%s\",\"status\":\"%s\",\"wrapper\":\"%s\",\"format\":\"%s\",\"supported\":%s,\"payload_offset\":%llu,\"payload_size\":%llu",
                   argv[i], ost_status_string(st), ost_wrapper_kind_string(det.wrapper),
                   ost_format_kind_string(det.format), det.supported ? "true" : "false",
                   (unsigned long long)det.payload_offset, (unsigned long long)det.payload_size);
            if (show_forks) {
                printf(",\"forks\":{\"data\":{\"present\":%s,\"offset\":%llu,\"size\":%llu},\"resource\":{\"present\":%s,\"offset\":%llu,\"size\":%llu}}",
                       det.has_data_fork ? "true" : "false",
                       (unsigned long long)det.data_fork_offset,
                       (unsigned long long)det.data_fork_size,
                       det.has_resource_fork ? "true" : "false",
                       (unsigned long long)det.resource_fork_offset,
                       (unsigned long long)det.resource_fork_size);
                printf(",\"finder\":{\"type\":\"");
                print_fourcc_json(det.finder_type);
                printf("\",\"creator\":\"");
                print_fourcc_json(det.finder_creator);
                printf("\"}");
            }
            printf("}\n");
        } else {
            printf("%s:\n", argv[i]);
            printf("  status: %s\n", ost_status_string(st));
            printf("  wrapper: %s\n", ost_wrapper_kind_string(det.wrapper));
            printf("  format: %s\n", ost_format_kind_string(det.format));
            printf("  supported: %s\n", det.supported ? "yes" : "no");
            printf("  payload: offset=%llu size=%llu\n",
                   (unsigned long long)det.payload_offset, (unsigned long long)det.payload_size);
            if (det.wrapper == OST_WRAPPER_MACBINARY || det.wrapper == OST_WRAPPER_BINHEX) {
                if (det.wrapper == OST_WRAPPER_BINHEX && input.hqx.name) printf("  binhex name: %s\n", input.hqx.name);
                if (det.wrapper == OST_WRAPPER_MACBINARY) printf("  macbinary version: %d\n", det.macbinary_version);
                printf("  finder: type=");
                print_fourcc_text(det.finder_type);
                printf(" creator=");
                print_fourcc_text(det.finder_creator);
                printf("\n");
            }
            if (show_forks) {
                printf("  data fork: present=%s offset=%llu size=%llu\n",
                       det.has_data_fork ? "yes" : "no",
                       (unsigned long long)det.data_fork_offset,
                       (unsigned long long)det.data_fork_size);
                printf("  resource fork: present=%s offset=%llu size=%llu\n",
                       det.has_resource_fork ? "yes" : "no",
                       (unsigned long long)det.resource_fork_offset,
                       (unsigned long long)det.resource_fork_size);
            }
        }

        int code = exit_code_for_status(st);
        if (code > worst) worst = code;
        input_file_free(&input);
    }
    return worst;
}

static int cmd_dump(int argc, char **argv) {
    bool json = false;
    bool headers = false;
    bool forks = false;
    const char *entry_selector = NULL;
    bool hex = false;
    uint64_t hex_offset = 0;
    uint64_t hex_length = 0;
    ost_unicode_normalization normalization = OST_UNICODE_NORMALIZE_NFC;
    int input = -1;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = true;
        else if (strcmp(argv[i], "--headers") == 0) headers = true;
        else if (strcmp(argv[i], "--forks") == 0) forks = true;
        else if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc) entry_selector = argv[++i];
        else if (strcmp(argv[i], "--hex") == 0 && i + 1 < argc) {
            const char *spec = argv[++i];
            char *range = (char *)malloc(strlen(spec) + 1u);
            if (!range) return 1;
            strcpy(range, spec);
            char *colon = strchr(range, ':');
            char *end = NULL;
            if (!colon || colon == range || colon[1] == '\0') {
                fprintf(stderr, "invalid --hex range, expected offset:length\n");
                free(range);
                return 5;
            }
            *colon = '\0';
            hex_offset = strtoull(range, &end, 0);
            if (!end || *end != '\0') {
                fprintf(stderr, "invalid --hex offset: %s\n", range);
                free(range);
                return 5;
            }
            hex_length = strtoull(colon + 1, &end, 0);
            if (!end || *end != '\0') {
                fprintf(stderr, "invalid --hex length: %s\n", colon + 1);
                free(range);
                return 5;
            }
            free(range);
            hex = true;
        }
        else if (strcmp(argv[i], "--unicode-normalization") == 0 && i + 1 < argc) {
            if (!parse_normalization_option(argv[++i], &normalization)) return 5;
        }
        else input = i;
    }
    if ((!headers && !forks && !entry_selector && !hex) || input < 0) {
        usage(stderr);
        return 5;
    }

    input_file loaded;
    ost_status st = load_input_file(argv[input], &loaded);
    ost_archive_set_unicode_normalization(normalization);
    if (loaded.raw.data) {
        if (headers && (st == OST_OK || st == OST_ERR_UNSUPPORTED)) {
            ost_status dump_st = ost_dump_headers(loaded.data, loaded.size, &loaded.det, json);
            if (st == OST_OK) st = dump_st;
        }
        if (forks && (st == OST_OK || st == OST_ERR_UNSUPPORTED)) {
            ost_status dump_st = ost_dump_forks(&loaded.det, json);
            if (st == OST_OK) st = dump_st;
        }
        if (entry_selector && st == OST_OK) {
            ost_archive archive;
            st = ost_archive_parse(loaded.data, loaded.size, &loaded.det, &archive);
            if (st == OST_OK) {
                st = ost_dump_entry(&archive, entry_selector, json);
                ost_archive_free(&archive);
            }
        }
        if (hex && (st == OST_OK || st == OST_ERR_UNSUPPORTED)) {
            st = ost_dump_hex(loaded.raw.data, loaded.raw.size, hex_offset, hex_length, json);
        }
    }
    input_file_free(&loaded);
    return exit_code_for_status(st);
}

static int cmd_list(int argc, char **argv) {
    ost_list_options opts;
    memset(&opts, 0, sizeof(opts));
    ost_unicode_normalization normalization = OST_UNICODE_NORMALIZE_NFC;
    int input = -1;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) opts.json = true;
        else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--long") == 0) opts.long_format = true;
        else if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--verylong") == 0) {
            opts.long_format = true;
            opts.very_long = true;
        } else if (strcmp(argv[i], "--unicode-normalization") == 0 && i + 1 < argc) {
            if (!parse_normalization_option(argv[++i], &normalization)) return 5;
        } else {
            input = i;
        }
    }
    if (input < 0) {
        usage(stderr);
        return 5;
    }

    input_file loaded;
    ost_archive archive;
    ost_status st = load_input_file(argv[input], &loaded);
    ost_archive_set_unicode_normalization(normalization);
    if (st == OST_OK) st = ost_archive_parse(loaded.data, loaded.size, &loaded.det, &archive);
    if (st == OST_OK) {
        st = ost_archive_print_list(&archive, &opts);
        ost_archive_free(&archive);
    }
    input_file_free(&loaded);
    if (st != OST_OK) fprintf(stderr, "openstuffit list: %s: %s\n", argv[input], ost_status_string(st));
    return exit_code_for_status(st);
}

static int cmd_extract(int argc, char **argv) {
    ost_extract_options opts;
    const char **include_paths = NULL;
    size_t include_count = 0;
    size_t include_cap = 0;
    memset(&opts, 0, sizeof(opts));
    opts.output_dir = ".";
    opts.forks = OST_FORKS_SKIP;
    opts.finder = OST_FINDER_SKIP;
    opts.collision = OST_COLLISION_SKIP;
    opts.preserve_time = true;
    opts.verify_crc = true;
    ost_unicode_normalization normalization = OST_UNICODE_NORMALIZE_NFC;
    int input = -1;

    for (int i = 0; i < argc; i++) {
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            opts.output_dir = argv[++i];
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            opts.password = argv[++i];
        } else if (strcmp(argv[i], "--overwrite") == 0) {
            opts.collision = OST_COLLISION_OVERWRITE;
        } else if (strcmp(argv[i], "--skip-existing") == 0) {
            opts.collision = OST_COLLISION_SKIP;
        } else if (strcmp(argv[i], "--rename-existing") == 0) {
            opts.collision = OST_COLLISION_RENAME;
        } else if (strcmp(argv[i], "--preserve-time") == 0) {
            opts.preserve_time = true;
        } else if (strcmp(argv[i], "--no-preserve-time") == 0) {
            opts.preserve_time = false;
        } else if (strcmp(argv[i], "--no-verify-crc") == 0) {
            opts.verify_crc = false;
        } else if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc) {
            if (include_count == include_cap) {
                size_t next_cap = include_cap ? include_cap * 2u : 8u;
                const char **next_paths = (const char **)realloc((void *)include_paths, sizeof(*include_paths) * next_cap);
                if (!next_paths) {
                    free((void *)include_paths);
                    return 1;
                }
                include_paths = next_paths;
                include_cap = next_cap;
            }
            include_paths[include_count++] = argv[++i];
        } else if (strcmp(argv[i], "--unicode-normalization") == 0 && i + 1 < argc) {
            if (!parse_normalization_option(argv[++i], &normalization)) {
                free((void *)include_paths);
                return 5;
            }
        } else if (strcmp(argv[i], "--forks") == 0 && i + 1 < argc) {
            const char *mode = argv[++i];
            if (strcmp(mode, "skip") == 0) opts.forks = OST_FORKS_SKIP;
            else if (strcmp(mode, "rsrc") == 0) opts.forks = OST_FORKS_RSRC;
            else if (strcmp(mode, "appledouble") == 0) opts.forks = OST_FORKS_APPLEDOUBLE;
            else if (strcmp(mode, "both") == 0) opts.forks = OST_FORKS_BOTH;
            else if (strcmp(mode, "native") == 0) opts.forks = OST_FORKS_NATIVE;
            else {
                fprintf(stderr, "unsupported --forks mode: %s\n", mode);
                free((void *)include_paths);
                return 5;
            }
        } else if (strcmp(argv[i], "--finder") == 0 && i + 1 < argc) {
            const char *mode = argv[++i];
            if (strcmp(mode, "skip") == 0) opts.finder = OST_FINDER_SKIP;
            else if (strcmp(mode, "sidecar") == 0) opts.finder = OST_FINDER_SIDECAR;
            else {
                fprintf(stderr, "unsupported --finder mode: %s\n", mode);
                free((void *)include_paths);
                return 5;
            }
        } else {
            input = i;
        }
    }
    if (input < 0) {
        usage(stderr);
        free((void *)include_paths);
        return 5;
    }
    opts.include_paths = include_paths;
    opts.include_path_count = include_count;

    input_file loaded;
    ost_archive archive;
    ost_status st = load_input_file(argv[input], &loaded);
    ost_archive_set_unicode_normalization(normalization);
    if (st == OST_OK) st = ost_archive_parse(loaded.data, loaded.size, &loaded.det, &archive);
    if (st == OST_OK) {
        st = ost_archive_extract(&archive, &opts);
        ost_archive_free(&archive);
    }
    input_file_free(&loaded);
    free((void *)include_paths);
    if (st == OST_OK || st == OST_ERR_UNSUPPORTED) {
        fprintf(stderr, "extract summary: extracted=%llu skipped=%llu unsupported=%llu checksum_errors=%llu\n",
                (unsigned long long)opts.extracted_files,
                (unsigned long long)opts.skipped_files,
                (unsigned long long)opts.unsupported_files,
                (unsigned long long)opts.checksum_errors);
    } else {
        fprintf(stderr, "openstuffit extract: %s: %s\n", argv[input], ost_status_string(st));
    }
    return exit_code_for_status(st);
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
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
        printf("openstuffit 0.1.0-m1\n");
        return 0;
    }
    if (strcmp(argv[1], "identify") == 0) return cmd_identify(argc - 2, argv + 2);
    if (strcmp(argv[1], "list") == 0) return cmd_list(argc - 2, argv + 2);
    if (strcmp(argv[1], "extract") == 0) return cmd_extract(argc - 2, argv + 2);
    if (strcmp(argv[1], "dump") == 0) return cmd_dump(argc - 2, argv + 2);

    usage(stderr);
    return 5;
}
