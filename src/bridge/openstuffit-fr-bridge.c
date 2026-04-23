#include <openstuffit/openstuffit.h>

#include "openstuffit_fr_bridge_json.h"
#include "ost_write.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(__USE_XOPEN2K8) && !defined(_XOPEN_SOURCE)
extern int mkstemp(char *template);
extern char *mkdtemp(char *template);
#endif

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
            "  openstuffit-fr-bridge extract --output-dir <dir> <archive> [--password <text>] [--overwrite|--skip-existing|--rename-existing] [--forks skip|rsrc|appledouble|both|native] [--finder skip|sidecar] [--unicode-normalization none|nfc|nfd] [--entry <path>]...\n"
            "  openstuffit-fr-bridge create --output <archive.sit> [--follow-links|--no-follow-links] [--type XXXX] [--creator XXXX] [<input>...]\n"
            "  openstuffit-fr-bridge add --base-dir <dir> [--follow-links|--no-follow-links] [--update] --entry <path>... <archive>\n"
            "  openstuffit-fr-bridge delete --entry <path>... <archive>\n");
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
        printf(",\"is_dir\":%s,\"size\":%llu,\"mtime_unix\":%llu,\"encrypted\":%s,\"data_method\":%u,\"resource_method\":%u,\"resource_present\":%s}",
               entry->is_dir ? "true" : "false",
               (unsigned long long)size,
               (unsigned long long)mtime_unix,
               encrypted ? "true" : "false",
               (unsigned)entry->data_fork.method,
               (unsigned)entry->resource_fork.method,
               entry->resource_fork.present ? "true" : "false");
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

static char *dup_cstr(const char *s) {
    size_t len;
    char *out;
    if (!s) return NULL;
    len = strlen(s);
    out = (char *)malloc(len + 1u);
    if (!out) return NULL;
    memcpy(out, s, len + 1u);
    return out;
}

static char *path_join(const char *a, const char *b) {
    size_t la;
    size_t lb;
    char *out;
    if (!a || a[0] == '\0') return dup_cstr(b ? b : "");
    if (!b || b[0] == '\0') return dup_cstr(a);
    la = strlen(a);
    lb = strlen(b);
    out = (char *)malloc(la + 1u + lb + 1u);
    if (!out) return NULL;
    memcpy(out, a, la);
    out[la] = '/';
    memcpy(out + la + 1u, b, lb + 1u);
    return out;
}

static int cmp_strings(const void *pa, const void *pb) {
    const char *a = *(const char *const *)pa;
    const char *b = *(const char *const *)pb;
    return strcmp(a, b);
}

static void free_string_array(char **items, size_t count) {
    if (!items) return;
    for (size_t i = 0; i < count; i++) free(items[i]);
    free(items);
}

static bool path_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static ost_status list_directory_children(const char *dir_path, char ***out_items, size_t *out_count) {
    DIR *dir;
    struct dirent *de;
    char **items = NULL;
    size_t count = 0;
    size_t cap = 0;

    if (!dir_path || !out_items || !out_count) return OST_ERR_INVALID_ARGUMENT;
    *out_items = NULL;
    *out_count = 0;

    dir = opendir(dir_path);
    if (!dir) return OST_ERR_IO;

    while ((de = readdir(dir)) != NULL) {
        char *name;
        char **next;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        name = dup_cstr(de->d_name);
        if (!name) {
            closedir(dir);
            free_string_array(items, count);
            return OST_ERR_NO_MEMORY;
        }
        if (count == cap) {
            size_t next_cap = cap ? cap * 2u : 16u;
            next = (char **)realloc(items, next_cap * sizeof(*items));
            if (!next) {
                free(name);
                closedir(dir);
                free_string_array(items, count);
                return OST_ERR_NO_MEMORY;
            }
            items = next;
            cap = next_cap;
        }
        items[count++] = name;
    }

    if (closedir(dir) != 0) {
        free_string_array(items, count);
        return OST_ERR_IO;
    }

    if (count > 1u) qsort(items, count, sizeof(*items), cmp_strings);
    *out_items = items;
    *out_count = count;
    return OST_OK;
}

static ost_status ensure_directory(const char *path) {
    if (!path || path[0] == '\0') return OST_ERR_INVALID_ARGUMENT;
    if (mkdir(path, 0777) == 0) return OST_OK;
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return OST_OK;
    }
    return OST_ERR_IO;
}

static ost_status ensure_parent_directories(const char *path) {
    char *buf;
    size_t len;
    if (!path) return OST_ERR_INVALID_ARGUMENT;
    len = strlen(path);
    buf = (char *)malloc(len + 1u);
    if (!buf) return OST_ERR_NO_MEMORY;
    memcpy(buf, path, len + 1u);
    for (size_t i = 1; i < len; i++) {
        if (buf[i] != '/') continue;
        buf[i] = '\0';
        if (buf[0] != '\0') {
            ost_status st = ensure_directory(buf);
            if (st != OST_OK) {
                free(buf);
                return st;
            }
        }
        buf[i] = '/';
    }
    free(buf);
    return OST_OK;
}

static ost_status remove_path_recursive(const char *path) {
    DIR *dir;
    if (!path || path[0] == '\0') return OST_ERR_INVALID_ARGUMENT;
    dir = opendir(path);
    if (!dir) {
        if (errno == ENOENT) return OST_OK;
        if (unlink(path) == 0) return OST_OK;
        if (errno == ENOENT) return OST_OK;
        return OST_ERR_IO;
    }

    while (1) {
        struct dirent *de = readdir(dir);
        if (!de) break;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char *child = path_join(path, de->d_name);
        ost_status st;
        if (!child) {
            closedir(dir);
            return OST_ERR_NO_MEMORY;
        }
        st = remove_path_recursive(child);
        free(child);
        if (st != OST_OK) {
            closedir(dir);
            return st;
        }
    }
    closedir(dir);
    if (rmdir(path) != 0 && errno != ENOENT) return OST_ERR_IO;
    return OST_OK;
}

static ost_status copy_regular_file(const char *src, const char *dst) {
    FILE *in;
    FILE *out;
    uint8_t buf[1u << 15];
    ost_status st = OST_OK;

    in = fopen(src, "rb");
    if (!in) return OST_ERR_IO;

    st = ensure_parent_directories(dst);
    if (st != OST_OK) {
        fclose(in);
        return st;
    }

    if (path_exists(dst)) {
        st = remove_path_recursive(dst);
        if (st != OST_OK) {
            fclose(in);
            return st;
        }
    }

    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return OST_ERR_IO;
    }

    while (!feof(in)) {
        size_t n = fread(buf, 1u, sizeof(buf), in);
        if (n > 0u && fwrite(buf, 1u, n, out) != n) {
            st = OST_ERR_IO;
            break;
        }
        if (ferror(in)) {
            st = OST_ERR_IO;
            break;
        }
    }
    fclose(out);
    fclose(in);
    return st;
}

static ost_status copy_path_recursive(const char *src, const char *dst) {
    struct stat st;
    if (!src || !dst) return OST_ERR_INVALID_ARGUMENT;
    if (stat(src, &st) != 0) return OST_ERR_IO;

    if (S_ISDIR(st.st_mode)) {
        char **children = NULL;
        size_t count = 0;
        ost_status ls_st;

        if (path_exists(dst)) {
            struct stat dst_st;
            if (stat(dst, &dst_st) != 0) return OST_ERR_IO;
            if (!S_ISDIR(dst_st.st_mode)) {
                ost_status rm_st = remove_path_recursive(dst);
                if (rm_st != OST_OK) return rm_st;
            }
        }
        if (!path_exists(dst)) {
            ost_status mk_st = ensure_parent_directories(dst);
            if (mk_st != OST_OK) return mk_st;
            if (mkdir(dst, 0777) != 0 && errno != EEXIST) return OST_ERR_IO;
        }

        ls_st = list_directory_children(src, &children, &count);
        if (ls_st != OST_OK) return ls_st;

        for (size_t i = 0; i < count; i++) {
            char *src_child = path_join(src, children[i]);
            char *dst_child = path_join(dst, children[i]);
            ost_status cp_st;
            if (!src_child || !dst_child) {
                free(src_child);
                free(dst_child);
                free_string_array(children, count);
                return OST_ERR_NO_MEMORY;
            }
            cp_st = copy_path_recursive(src_child, dst_child);
            free(src_child);
            free(dst_child);
            if (cp_st != OST_OK) {
                free_string_array(children, count);
                return cp_st;
            }
        }
        free_string_array(children, count);
        return OST_OK;
    }

    if (!S_ISREG(st.st_mode)) return OST_ERR_UNSUPPORTED;
    return copy_regular_file(src, dst);
}

static bool is_safe_relative_entry(const char *entry) {
    const char *p = entry;
    if (!entry || entry[0] == '\0') return false;
    if (entry[0] == '/') return false;
    while (*p) {
        if (*p == '\\' || *p == ':') return false;
        p++;
    }
    p = entry;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 0u || (len == 1u && p[0] == '.') || (len == 2u && p[0] == '.' && p[1] == '.')) return false;
        if (!slash) break;
        p = slash + 1;
    }
    return true;
}

static ost_status ensure_archive_write_supported(const bridge_input *loaded) {
    size_t count;
    if (!loaded || !loaded->handle) return OST_ERR_INVALID_ARGUMENT;
    if (loaded->detection.wrapper != OST_WRAPPER_RAW) return OST_ERR_UNSUPPORTED;
    if (loaded->detection.format != OST_FORMAT_SIT_CLASSIC) return OST_ERR_UNSUPPORTED;
    count = ost_archive_handle_entry_count(loaded->handle);
    for (size_t i = 0; i < count; i++) {
        const ost_entry *e = ost_archive_handle_entry(loaded->handle, i);
        if (!e) continue;
        if (e->resource_fork.present) return OST_ERR_UNSUPPORTED;
        if (e->data_fork.encrypted || e->resource_fork.encrypted) return OST_ERR_UNSUPPORTED;
    }
    return OST_OK;
}

static ost_status extract_archive_to_dir(const bridge_input *loaded, const char *output_dir) {
    ost_extract_options options;
    if (!loaded || !loaded->handle || !output_dir) return OST_ERR_INVALID_ARGUMENT;
    memset(&options, 0, sizeof(options));
    options.output_dir = output_dir;
    options.forks = OST_FORKS_SKIP;
    options.finder = OST_FINDER_SKIP;
    options.collision = OST_COLLISION_OVERWRITE;
    options.preserve_time = true;
    options.verify_crc = true;
    return ost_archive_handle_extract(loaded->handle, &options);
}

static ost_status collect_top_level_input_paths(const char *dir_path, char ***out_paths, size_t *out_count) {
    char **names = NULL;
    char **paths = NULL;
    size_t count = 0;
    ost_status st;

    if (!dir_path || !out_paths || !out_count) return OST_ERR_INVALID_ARGUMENT;
    *out_paths = NULL;
    *out_count = 0;

    st = list_directory_children(dir_path, &names, &count);
    if (st != OST_OK) return st;

    if (count == 0u) {
        free_string_array(names, count);
        return OST_OK;
    }

    paths = (char **)calloc(count, sizeof(*paths));
    if (!paths) {
        free_string_array(names, count);
        return OST_ERR_NO_MEMORY;
    }

    for (size_t i = 0; i < count; i++) {
        paths[i] = path_join(dir_path, names[i]);
        if (!paths[i]) {
            free_string_array(names, count);
            free_string_array(paths, count);
            return OST_ERR_NO_MEMORY;
        }
    }
    free_string_array(names, count);
    *out_paths = paths;
    *out_count = count;
    return OST_OK;
}

static ost_status rewrite_archive_from_directory(const char *archive_path, const char *dir_path, bool follow_links) {
    char **inputs = NULL;
    size_t input_count = 0;
    ost_status st;
    char *tmp_template;
    int fd;

    st = collect_top_level_input_paths(dir_path, &inputs, &input_count);
    if (st != OST_OK) return st;

    tmp_template = (char *)malloc(strlen(archive_path) + 16u);
    if (!tmp_template) {
        free_string_array(inputs, input_count);
        return OST_ERR_NO_MEMORY;
    }
    sprintf(tmp_template, "%s.tmp.XXXXXX", archive_path);
    fd = mkstemp(tmp_template);
    if (fd < 0) {
        free(tmp_template);
        free_string_array(inputs, input_count);
        return OST_ERR_IO;
    }
    close(fd);

    {
        ost_create_options create_opts;
        ost_create_options_init(&create_opts);
        create_opts.output_path = tmp_template;
        create_opts.input_paths = (const char *const *)inputs;
        create_opts.input_path_count = input_count;
        create_opts.follow_links = follow_links;
        st = ost_write_sit_classic(&create_opts);
    }

    if (st == OST_OK && rename(tmp_template, archive_path) != 0) st = OST_ERR_IO;
    if (st != OST_OK) unlink(tmp_template);

    free(tmp_template);
    free_string_array(inputs, input_count);
    return st;
}

static ost_status prepare_temp_root(char out_template[64]) {
    strcpy(out_template, "/tmp/openstuffit_fr_bridge_XXXXXX");
    if (!mkdtemp(out_template)) return OST_ERR_IO;
    return OST_OK;
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

static int cmd_create(int argc, char **argv) {
    ost_create_options options;
    int first_input = -1;
    ost_status st;

    ost_create_options_init(&options);

    for (int i = 0; i < argc; i++) {
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            options.output_path = argv[++i];
        } else if (strcmp(argv[i], "--follow-links") == 0) {
            options.follow_links = true;
        } else if (strcmp(argv[i], "--no-follow-links") == 0) {
            options.follow_links = false;
        } else if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
            const char *code = argv[++i];
            if (strlen(code) != 4u) return 5;
            memcpy(options.default_type, code, 4u);
        } else if (strcmp(argv[i], "--creator") == 0 && i + 1 < argc) {
            const char *code = argv[++i];
            if (strlen(code) != 4u) return 5;
            memcpy(options.default_creator, code, 4u);
        } else if (argv[i][0] == '-') {
            usage(stderr);
            return 5;
        } else {
            first_input = i;
            break;
        }
    }

    if (!options.output_path) {
        usage(stderr);
        return 5;
    }

    if (first_input >= 0) {
        options.input_paths = (const char *const *)(argv + first_input);
        options.input_path_count = (size_t)(argc - first_input);
    }

    st = ost_write_sit_classic(&options);
    if (st != OST_OK) return fail_status("create", options.output_path, st);
    printf("{\"status\":\"ok\",\"archive\":");
    ost_fr_json_print_string(stdout, options.output_path);
    printf(",\"input_count\":%llu}\n", (unsigned long long)options.input_path_count);
    return 0;
}

static int cmd_add(int argc, char **argv) {
    const char *archive_path = NULL;
    const char *base_dir = NULL;
    const char **entries = NULL;
    size_t entry_count = 0;
    size_t entry_cap = 0;
    bool follow_links = true;
    bool update_only = false;
    size_t added_count = 0;
    size_t skipped_count = 0;
    char temp_root[64];
    ost_status st = OST_OK;
    bridge_input loaded;
    bool have_loaded = false;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--base-dir") == 0 && i + 1 < argc) base_dir = argv[++i];
        else if (strcmp(argv[i], "--follow-links") == 0) follow_links = true;
        else if (strcmp(argv[i], "--no-follow-links") == 0) follow_links = false;
        else if (strcmp(argv[i], "--update") == 0) update_only = true;
        else if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc) {
            if (entry_count == entry_cap) {
                size_t next_cap = entry_cap ? entry_cap * 2u : 8u;
                const char **next_entries = (const char **)realloc((void *)entries, next_cap * sizeof(*entries));
                if (!next_entries) {
                    free((void *)entries);
                    return 1;
                }
                entries = next_entries;
                entry_cap = next_cap;
            }
            entries[entry_count++] = argv[++i];
        } else if (argv[i][0] == '-') {
            usage(stderr);
            free((void *)entries);
            return 5;
        } else if (!archive_path) {
            archive_path = argv[i];
        } else {
            usage(stderr);
            free((void *)entries);
            return 5;
        }
    }

    if (!archive_path || !base_dir || entry_count == 0u) {
        usage(stderr);
        free((void *)entries);
        return 5;
    }

    st = prepare_temp_root(temp_root);
    if (st != OST_OK) {
        free((void *)entries);
        return fail_status("add", archive_path, st);
    }

    if (path_exists(archive_path)) {
        st = bridge_input_open(archive_path, OST_UNICODE_NORMALIZE_NFC, &loaded);
        if (st == OST_OK) {
            have_loaded = true;
            st = ensure_archive_write_supported(&loaded);
        }
        if (st == OST_OK) st = extract_archive_to_dir(&loaded, temp_root);
        if (have_loaded) bridge_input_free(&loaded);
        if (st != OST_OK) {
            remove_path_recursive(temp_root);
            free((void *)entries);
            return fail_status("add", archive_path, st);
        }
    }

    for (size_t i = 0; i < entry_count; i++) {
        char *src_path;
        char *dst_path;
        if (!is_safe_relative_entry(entries[i])) {
            st = OST_ERR_INVALID_ARGUMENT;
            break;
        }
        src_path = path_join(base_dir, entries[i]);
        dst_path = path_join(temp_root, entries[i]);
        if (!src_path || !dst_path) {
            free(src_path);
            free(dst_path);
            st = OST_ERR_NO_MEMORY;
            break;
        }
        if (update_only) {
            struct stat src_st;
            struct stat dst_st;
            if (stat(src_path, &src_st) == 0
                && stat(dst_path, &dst_st) == 0
                && S_ISREG(src_st.st_mode)
                && S_ISREG(dst_st.st_mode)
                && dst_st.st_mtime >= src_st.st_mtime)
            {
                st = OST_OK;
                skipped_count++;
                free(src_path);
                free(dst_path);
                continue;
            }
        }
        st = copy_path_recursive(src_path, dst_path);
        free(src_path);
        free(dst_path);
        if (st == OST_OK) added_count++;
        else break;
    }

    if (st == OST_OK) st = rewrite_archive_from_directory(archive_path, temp_root, follow_links);

    remove_path_recursive(temp_root);
    free((void *)entries);
    if (st != OST_OK) return fail_status("add", archive_path, st);
    printf("{\"status\":\"ok\",\"archive\":");
    ost_fr_json_print_string(stdout, archive_path);
    printf(",\"added\":%llu,\"skipped\":%llu}\n",
           (unsigned long long)added_count,
           (unsigned long long)skipped_count);
    return 0;
}

static int cmd_delete(int argc, char **argv) {
    const char *archive_path = NULL;
    const char **entries = NULL;
    size_t entry_count = 0;
    size_t entry_cap = 0;
    char temp_root[64];
    ost_status st = OST_OK;
    bridge_input loaded;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc) {
            if (entry_count == entry_cap) {
                size_t next_cap = entry_cap ? entry_cap * 2u : 8u;
                const char **next_entries = (const char **)realloc((void *)entries, next_cap * sizeof(*entries));
                if (!next_entries) {
                    free((void *)entries);
                    return 1;
                }
                entries = next_entries;
                entry_cap = next_cap;
            }
            entries[entry_count++] = argv[++i];
        } else if (argv[i][0] == '-') {
            usage(stderr);
            free((void *)entries);
            return 5;
        } else if (!archive_path) {
            archive_path = argv[i];
        } else {
            usage(stderr);
            free((void *)entries);
            return 5;
        }
    }

    if (!archive_path || entry_count == 0u) {
        usage(stderr);
        free((void *)entries);
        return 5;
    }
    if (!path_exists(archive_path)) {
        free((void *)entries);
        return fail_status("delete", archive_path, OST_ERR_IO);
    }

    st = bridge_input_open(archive_path, OST_UNICODE_NORMALIZE_NFC, &loaded);
    if (st != OST_OK) {
        free((void *)entries);
        return fail_status("delete", archive_path, st);
    }
    st = ensure_archive_write_supported(&loaded);
    if (st == OST_OK) st = prepare_temp_root(temp_root);
    if (st == OST_OK) st = extract_archive_to_dir(&loaded, temp_root);
    bridge_input_free(&loaded);
    if (st != OST_OK) {
        remove_path_recursive(temp_root);
        free((void *)entries);
        return fail_status("delete", archive_path, st);
    }

    for (size_t i = 0; i < entry_count; i++) {
        char *target;
        if (!is_safe_relative_entry(entries[i])) {
            st = OST_ERR_INVALID_ARGUMENT;
            break;
        }
        target = path_join(temp_root, entries[i]);
        if (!target) {
            st = OST_ERR_NO_MEMORY;
            break;
        }
        st = remove_path_recursive(target);
        free(target);
        if (st != OST_OK) break;
    }

    if (st == OST_OK) st = rewrite_archive_from_directory(archive_path, temp_root, true);
    remove_path_recursive(temp_root);
    free((void *)entries);
    if (st != OST_OK) return fail_status("delete", archive_path, st);
    printf("{\"status\":\"ok\",\"archive\":");
    ost_fr_json_print_string(stdout, archive_path);
    printf(",\"deleted\":%llu}\n", (unsigned long long)entry_count);
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
    if (strcmp(argv[1], "create") == 0) return cmd_create(argc - 2, argv + 2);
    if (strcmp(argv[1], "add") == 0) return cmd_add(argc - 2, argv + 2);
    if (strcmp(argv[1], "delete") == 0) return cmd_delete(argc - 2, argv + 2);
    usage(stderr);
    return 5;
}
