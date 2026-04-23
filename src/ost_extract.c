#include "ost_extract.h"

#include "ost_crc16.h"
#include "ost_decompress.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <sys/utime.h>
#define mkdir_one(path) _mkdir(path)
#define utime_one(path, times) _utime((path), (times))
#define utimbuf_type _utimbuf
#else
#include <utime.h>
#define mkdir_one(path) mkdir((path), 0777)
#define utime_one(path, times) utime((path), (times))
#define utimbuf_type utimbuf
#endif

static char *dup_cstr(const char *s) {
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static bool path_is_safe(const char *path) {
    if (!path || !*path) return false;
    if (path[0] == '/') return false;
    if (strstr(path, "\\") != NULL) return false;
    if (strstr(path, "..") != NULL) return false;
    if (strchr(path, ':') != NULL) return false;
    return true;
}

static ost_status mkdir_p(const char *path) {
    if (!path || !*path) return OST_OK;
    char *tmp = dup_cstr(path);
    if (!tmp) return OST_ERR_NO_MEMORY;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir_one(tmp) != 0 && errno != EEXIST) {
                free(tmp);
                return OST_ERR_IO;
            }
            *p = '/';
        }
    }
    if (mkdir_one(tmp) != 0 && errno != EEXIST) {
        free(tmp);
        return OST_ERR_IO;
    }
    free(tmp);
    return OST_OK;
}

static char *join_output_path(const char *base, const char *entry_path, const char *suffix) {
    if (!base || !*base) base = ".";
    size_t a = strlen(base);
    size_t b = strlen(entry_path);
    size_t c = suffix ? strlen(suffix) : 0;
    bool slash = a > 0 && base[a - 1] != '/';
    char *out = (char *)malloc(a + (slash ? 1u : 0u) + b + c + 1u);
    if (!out) return NULL;
    memcpy(out, base, a);
    size_t pos = a;
    if (slash) out[pos++] = '/';
    memcpy(out + pos, entry_path, b);
    pos += b;
    if (suffix) {
        memcpy(out + pos, suffix, c);
        pos += c;
    }
    out[pos] = '\0';
    return out;
}

static char *appledouble_path_for_normal(const char *normal) {
    char *slash = strrchr(normal, '/');
    size_t prefix_len = slash ? (size_t)(slash - normal + 1) : 0u;
    const char *name = normal + prefix_len;
    size_t name_len = strlen(name);
    char *out = (char *)malloc(prefix_len + 2u + name_len + 1u);
    if (!out) return NULL;
    memcpy(out, normal, prefix_len);
    out[prefix_len] = '.';
    out[prefix_len + 1u] = '_';
    memcpy(out + prefix_len + 2u, name, name_len + 1u);
    return out;
}

static char *append_suffix_to_path(const char *path, const char *suffix) {
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    char *out = (char *)malloc(path_len + suffix_len + 1u);
    if (!out) return NULL;
    memcpy(out, path, path_len);
    memcpy(out + path_len, suffix, suffix_len + 1u);
    return out;
}

static ost_status ensure_parent_dir(const char *path) {
    char *tmp = dup_cstr(path);
    if (!tmp) return OST_ERR_NO_MEMORY;
    char *slash = strrchr(tmp, '/');
    if (slash) {
        *slash = '\0';
        ost_status st = mkdir_p(tmp);
        free(tmp);
        return st;
    }
    free(tmp);
    return OST_OK;
}

static bool mac_time_to_time_t(uint32_t mac_time, time_t *out) {
    const uint32_t mac_to_unix_delta = 2082844800u;
    if (!out || mac_time < mac_to_unix_delta) return false;
    uint32_t unix_time = mac_time - mac_to_unix_delta;
    *out = (time_t)unix_time;
    return true;
}

static ost_status set_file_mtime(const char *path, uint32_t modify_time_mac) {
    time_t mtime;
    if (!mac_time_to_time_t(modify_time_mac, &mtime)) return OST_OK;
    struct utimbuf_type times;
    times.actime = mtime;
    times.modtime = mtime;
    if (utime_one(path, &times) != 0) return OST_ERR_IO;
    return OST_OK;
}

static bool path_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp) {
        fclose(fp);
        return true;
    }
    return false;
}

static char *make_collision_path(const char *path) {
    size_t path_len = strlen(path);
    for (unsigned i = 1; i < 10000u; i++) {
        char suffix[32];
        int n = snprintf(suffix, sizeof(suffix), ".%u", i);
        if (n <= 0) return NULL;
        size_t suffix_len = (size_t)n;
        char *candidate = (char *)malloc(path_len + suffix_len + 1u);
        if (!candidate) return NULL;
        memcpy(candidate, path, path_len);
        memcpy(candidate + path_len, suffix, suffix_len + 1u);
        if (!path_exists(candidate)) return candidate;
        free(candidate);
    }
    return NULL;
}

static ost_status write_buffer(const uint8_t *data,
                               size_t size,
                               const char *path,
                               ost_collision_mode collision,
                               char **actual_path) {
    if (!path) return OST_ERR_INVALID_ARGUMENT;
    if (actual_path) *actual_path = NULL;
    char *renamed_path = NULL;
    const char *target = path;
    if (collision == OST_COLLISION_SKIP && path_exists(path)) {
        return OST_ERR_SKIPPED;
    }
    if (collision == OST_COLLISION_RENAME && path_exists(path)) {
        renamed_path = make_collision_path(path);
        if (!renamed_path) return OST_ERR_NO_MEMORY;
        target = renamed_path;
    }

    ost_status st = ensure_parent_dir(target);
    if (st != OST_OK) {
        free(renamed_path);
        return st;
    }
    FILE *fp = fopen(target, "wb");
    if (!fp) {
        free(renamed_path);
        return OST_ERR_IO;
    }
    if (size > 0 && fwrite(data, 1, size, fp) != size) {
        fclose(fp);
        free(renamed_path);
        return OST_ERR_IO;
    }
    if (fclose(fp) != 0) {
        free(renamed_path);
        return OST_ERR_IO;
    }
    if (actual_path && renamed_path) {
        *actual_path = renamed_path;
    } else {
        free(renamed_path);
    }
    return OST_OK;
}

static void append_json_escaped(char *out, size_t *pos, const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            out[(*pos)++] = '\\';
            out[(*pos)++] = (char)c;
        } else if (c >= 32 && c < 127) {
            out[(*pos)++] = (char)c;
        } else {
            int n = snprintf(out + *pos, 7u, "\\u%04x", c);
            if (n > 0) *pos += (size_t)n;
        }
    }
}

static void append_fourcc_json(char *out, size_t *pos, const uint8_t v[4]) {
    for (int i = 0; i < 4; i++) {
        unsigned char c = v[i];
        if (c >= 32 && c < 127 && c != '"' && c != '\\') {
            out[(*pos)++] = (char)c;
        } else {
            int n = snprintf(out + *pos, 7u, "\\u%04x", c);
            if (n > 0) *pos += (size_t)n;
        }
    }
}

static ost_status write_finder_sidecar(const ost_entry *entry, const char *outpath, ost_collision_mode collision) {
    const char *suffix = ".finder.json";
    size_t path_len = strlen(outpath);
    size_t suffix_len = strlen(suffix);
    char *meta_path = (char *)malloc(path_len + suffix_len + 1u);
    if (!meta_path) return OST_ERR_NO_MEMORY;
    memcpy(meta_path, outpath, path_len);
    memcpy(meta_path + path_len, suffix, suffix_len + 1u);

    size_t cap = strlen(entry->path) * 6u + 512u;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        free(meta_path);
        return OST_ERR_NO_MEMORY;
    }
    size_t pos = 0;
    pos += (size_t)snprintf(buf + pos, cap - pos, "{\n  \"path\":\"");
    append_json_escaped(buf, &pos, entry->path);
    pos += (size_t)snprintf(buf + pos, cap - pos, "\",\n  \"finder\":{\"type\":\"");
    append_fourcc_json(buf, &pos, entry->file_type);
    pos += (size_t)snprintf(buf + pos, cap - pos, "\",\"creator\":\"");
    append_fourcc_json(buf, &pos, entry->creator);
    pos += (size_t)snprintf(buf + pos,
                            cap - pos,
                            "\",\"flags\":\"%04x\"},\n  \"mac_time\":{\"create\":%u,\"modify\":%u}\n}\n",
                            entry->finder_flags,
                            entry->create_time_mac,
                            entry->modify_time_mac);
    if (pos > cap) {
        free(buf);
        free(meta_path);
        return OST_ERR_BAD_FORMAT;
    }
    ost_status st = write_buffer((const uint8_t *)buf, pos, meta_path, collision, NULL);
    free(buf);
    free(meta_path);
    return st;
}

static bool fork_has_crc16(const ost_fork_info *fork) {
    return (fork->method & 0x0f) != 15u;
}

static ost_status decompress_fork_checked(const ost_archive *archive,
                                          const ost_fork_info *fork,
                                          bool verify_crc,
                                          const char *password,
                                          ost_decompressed *dec) {
    if (!fork->present) return OST_OK;
    ost_status st = ost_decompress_fork_with_password(ost_archive_data(archive), ost_archive_data_size(archive), fork, password, dec);
    if (st != OST_OK) return st;
    if (verify_crc && fork_has_crc16(fork)) {
        uint16_t actual = ost_crc16_ibm(dec->data, dec->size);
        if (actual != fork->crc16) {
            ost_decompressed_free(dec);
            return OST_ERR_CHECKSUM;
        }
    }
    return OST_OK;
}

static ost_status extract_fork(const ost_archive *archive,
                               const ost_fork_info *fork,
                               char **path,
                               ost_collision_mode collision,
                               bool verify_crc,
                               const char *password) {
    if (!fork->present) return OST_OK;
    ost_decompressed dec;
    ost_status st = decompress_fork_checked(archive, fork, verify_crc, password, &dec);
    if (st != OST_OK) return st;
    char *actual_path = NULL;
    st = write_buffer(dec.data, dec.size, *path, collision, &actual_path);
    if (st == OST_OK && actual_path) {
        free(*path);
        *path = actual_path;
    }
    ost_decompressed_free(&dec);
    return st;
}

static void put_u16_be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static ost_status write_appledouble_sidecar(const ost_entry *entry,
                                            const uint8_t *rsrc,
                                            size_t rsrc_size,
                                            const char *path,
                                            ost_collision_mode collision) {
    const size_t header_size = 50u;
    const size_t finder_size = 32u;
    const size_t rsrc_offset = header_size + finder_size;
    if (rsrc_size > SIZE_MAX - rsrc_offset) return OST_ERR_BAD_FORMAT;
    if (rsrc_size > UINT32_MAX) return OST_ERR_BAD_FORMAT;
    size_t total = rsrc_offset + rsrc_size;
    uint8_t *buf = (uint8_t *)calloc(1u, total);
    if (!buf) return OST_ERR_NO_MEMORY;

    put_u32_be(buf + 0, 0x00051607u);
    put_u32_be(buf + 4, 0x00020000u);
    put_u16_be(buf + 24, 2u);
    put_u32_be(buf + 26, 9u);
    put_u32_be(buf + 30, (uint32_t)header_size);
    put_u32_be(buf + 34, (uint32_t)finder_size);
    put_u32_be(buf + 38, 2u);
    put_u32_be(buf + 42, (uint32_t)rsrc_offset);
    put_u32_be(buf + 46, (uint32_t)rsrc_size);

    memcpy(buf + header_size, entry->file_type, 4u);
    memcpy(buf + header_size + 4u, entry->creator, 4u);
    put_u16_be(buf + header_size + 8u, entry->finder_flags);
    if (rsrc_size > 0) memcpy(buf + rsrc_offset, rsrc, rsrc_size);

    ost_status st = write_buffer(buf, total, path, collision, NULL);
    free(buf);
    return st;
}

static ost_status write_native_resource_fork(const uint8_t *rsrc, size_t rsrc_size, const char *outpath, ost_collision_mode collision) {
#ifdef __APPLE__
    const char *suffix = "/..namedfork/rsrc";
    size_t path_len = strlen(outpath);
    size_t suffix_len = strlen(suffix);
    char *native_path = (char *)malloc(path_len + suffix_len + 1u);
    if (!native_path) return OST_ERR_NO_MEMORY;
    memcpy(native_path, outpath, path_len);
    memcpy(native_path + path_len, suffix, suffix_len + 1u);
    ost_status st = write_buffer(rsrc, rsrc_size, native_path, collision, NULL);
    free(native_path);
    return st;
#else
    (void)rsrc;
    (void)rsrc_size;
    (void)outpath;
    (void)collision;
    return OST_ERR_UNSUPPORTED;
#endif
}

ost_status ost_archive_extract(const ost_archive *archive, ost_extract_options *options) {
    if (!archive || !options) return OST_ERR_INVALID_ARGUMENT;
    const char *outdir = options->output_dir ? options->output_dir : ".";
    ost_status st = mkdir_p(outdir);
    if (st != OST_OK) return st;

    bool had_unsupported = false;
    for (size_t i = 0; i < archive->entry_count; i++) {
        const ost_entry *entry = &archive->entries[i];
        if (!path_is_safe(entry->path)) {
            options->skipped_files++;
            return OST_ERR_BAD_FORMAT;
        }

        char *outpath = join_output_path(outdir, entry->path, NULL);
        if (!outpath) return OST_ERR_NO_MEMORY;

        if (entry->is_dir) {
            st = mkdir_p(outpath);
            free(outpath);
            if (st != OST_OK) return st;
            continue;
        }

        if (entry->data_fork.present) {
            st = extract_fork(archive, &entry->data_fork, &outpath, options->collision, options->verify_crc, options->password);
            if (st == OST_OK) {
                options->extracted_files++;
                if (options->preserve_time) {
                    st = set_file_mtime(outpath, entry->modify_time_mac);
                    if (st != OST_OK) {
                        free(outpath);
                        return st;
                    }
                }
            } else if (st == OST_ERR_SKIPPED) {
                fprintf(stderr, "skip existing data fork: %s\n", entry->path);
                options->skipped_files++;
            } else if (st == OST_ERR_UNSUPPORTED) {
                fprintf(stderr, "skip unsupported data fork: %s method=%u\n", entry->path, (unsigned)entry->data_fork.method);
                options->unsupported_files++;
                had_unsupported = true;
            } else if (st == OST_ERR_CHECKSUM) {
                fprintf(stderr, "checksum mismatch data fork: %s expected=%04x\n", entry->path, entry->data_fork.crc16);
                options->checksum_errors++;
                free(outpath);
                return st;
            } else {
                free(outpath);
                return st;
            }
        } else {
            st = ensure_parent_dir(outpath);
            if (st != OST_OK) {
                free(outpath);
                return st;
            }
        }

        if (options->forks != OST_FORKS_SKIP && entry->resource_fork.present) {
            ost_decompressed rsrc;
            st = decompress_fork_checked(archive, &entry->resource_fork, options->verify_crc, options->password, &rsrc);
            if (st == OST_ERR_UNSUPPORTED) {
                fprintf(stderr, "skip unsupported resource fork: %s method=%u\n", entry->path, (unsigned)entry->resource_fork.method);
                options->unsupported_files++;
                had_unsupported = true;
            } else if (st == OST_ERR_CHECKSUM) {
                fprintf(stderr, "checksum mismatch resource fork: %s expected=%04x\n", entry->path, entry->resource_fork.crc16);
                options->checksum_errors++;
                free(outpath);
                return st;
            } else if (st != OST_OK) {
                free(outpath);
                return st;
            } else {
                if (options->forks == OST_FORKS_RSRC || options->forks == OST_FORKS_BOTH) {
                    char *rsrc_path = append_suffix_to_path(outpath, ".rsrc");
                    if (!rsrc_path) {
                        ost_decompressed_free(&rsrc);
                        free(outpath);
                        return OST_ERR_NO_MEMORY;
                    }
                    st = write_buffer(rsrc.data, rsrc.size, rsrc_path, options->collision, NULL);
                    free(rsrc_path);
                    if (st == OST_OK) {
                        options->extracted_files++;
                    } else if (st == OST_ERR_SKIPPED) {
                        fprintf(stderr, "skip existing resource fork sidecar: %s\n", entry->path);
                        options->skipped_files++;
                    } else {
                        ost_decompressed_free(&rsrc);
                        free(outpath);
                        return st;
                    }
                }
                if (options->forks == OST_FORKS_APPLEDOUBLE || options->forks == OST_FORKS_BOTH) {
                    char *ad_path = appledouble_path_for_normal(outpath);
                    if (!ad_path) {
                        ost_decompressed_free(&rsrc);
                        free(outpath);
                        return OST_ERR_NO_MEMORY;
                    }
                    st = write_appledouble_sidecar(entry, rsrc.data, rsrc.size, ad_path, options->collision);
                    free(ad_path);
                    if (st == OST_OK) {
                        options->extracted_files++;
                    } else if (st == OST_ERR_SKIPPED) {
                        fprintf(stderr, "skip existing AppleDouble sidecar: %s\n", entry->path);
                        options->skipped_files++;
                    } else {
                        ost_decompressed_free(&rsrc);
                        free(outpath);
                        return st;
                    }
                }
                if (options->forks == OST_FORKS_NATIVE) {
                    st = write_native_resource_fork(rsrc.data, rsrc.size, outpath, options->collision);
                    if (st == OST_OK) {
                        options->extracted_files++;
                    } else if (st == OST_ERR_SKIPPED) {
                        fprintf(stderr, "skip existing native resource fork: %s\n", entry->path);
                        options->skipped_files++;
                    } else if (st == OST_ERR_UNSUPPORTED) {
                        fprintf(stderr, "skip native resource fork on unsupported platform: %s\n", entry->path);
                        options->unsupported_files++;
                        had_unsupported = true;
                    } else {
                        ost_decompressed_free(&rsrc);
                        free(outpath);
                        return st;
                    }
                }
                ost_decompressed_free(&rsrc);
            }
        }

        if (options->finder == OST_FINDER_SIDECAR) {
            st = write_finder_sidecar(entry, outpath, options->collision);
            if (st == OST_OK) {
                options->extracted_files++;
            } else if (st == OST_ERR_SKIPPED) {
                fprintf(stderr, "skip existing finder metadata: %s\n", entry->path);
                options->skipped_files++;
            } else {
                free(outpath);
                return st;
            }
        }

        free(outpath);
    }

    return had_unsupported ? OST_ERR_UNSUPPORTED : OST_OK;
}
