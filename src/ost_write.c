#include "ost_write.h"

#include "ost_crc16.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if !defined(__USE_XOPEN2K8) && !defined(_XOPEN_SOURCE)
extern int lstat(const char *path, struct stat *buf);
#endif

#define SIT_CLASSIC_HEADER_SIZE 22u
#define SIT_CLASSIC_ENTRY_SIZE 112u
#define SIT_CLASSIC_START_FOLDER 32u
#define SIT_CLASSIC_END_FOLDER 33u

#define MAC_TIME_DELTA 2082844800u

typedef struct {
    FILE *fp;
    const ost_create_options *opts;
    uint32_t entry_count;
} writer_ctx;

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

static void write_be16(uint8_t out[2], uint16_t v) {
    out[0] = (uint8_t)((v >> 8) & 0xffu);
    out[1] = (uint8_t)(v & 0xffu);
}

static void write_be32(uint8_t out[4], uint32_t v) {
    out[0] = (uint8_t)((v >> 24) & 0xffu);
    out[1] = (uint8_t)((v >> 16) & 0xffu);
    out[2] = (uint8_t)((v >> 8) & 0xffu);
    out[3] = (uint8_t)(v & 0xffu);
}

static uint32_t unix_to_mac_time(time_t t) {
    if (t <= 0) return 0;
    uint64_t value = (uint64_t)t + (uint64_t)MAC_TIME_DELTA;
    if (value > 0xffffffffu) return 0xffffffffu;
    return (uint32_t)value;
}

static bool safe_name_char(unsigned char c) {
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    if (c == ' ' || c == '.' || c == '_' || c == '-' || c == '+' || c == '(' || c == ')' || c == '[' || c == ']') return true;
    return false;
}

static size_t encode_name_ascii(const char *name, uint8_t out[63]) {
    size_t n = 0;
    if (!name) return 0;
    while (*name && n < 31u) {
        unsigned char c = (unsigned char)*name++;
        if (safe_name_char(c)) out[n++] = c;
        else out[n++] = '_';
    }
    return n;
}

static char *path_basename_dup(const char *path) {
    size_t len;
    size_t start;
    if (!path) return NULL;
    len = strlen(path);
    while (len > 0 && path[len - 1] == '/') len--;
    if (len == 0) return dup_cstr(path);
    start = len;
    while (start > 0 && path[start - 1] != '/') start--;
    if (start >= len) return dup_cstr(path);
    {
        size_t n = len - start;
        char *out = (char *)malloc(n + 1u);
        if (!out) return NULL;
        memcpy(out, path + start, n);
        out[n] = '\0';
        return out;
    }
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

static uint16_t crc16_ibm_update(uint16_t crc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 1u) != 0u) crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else crc >>= 1;
        }
    }
    return crc;
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

static bool write_entry_header(FILE *fp,
                               uint8_t rmethod,
                               uint8_t dmethod,
                               const char *name,
                               const uint8_t file_type[4],
                               const uint8_t creator[4],
                               uint32_t cdate,
                               uint32_t mdate,
                               uint32_t rlen,
                               uint32_t dlen,
                               uint32_t rclen,
                               uint32_t dclen,
                               uint16_t rcrc,
                               uint16_t dcrc) {
    uint8_t h[SIT_CLASSIC_ENTRY_SIZE];
    uint16_t hcrc;
    size_t namelen;

    memset(h, 0, sizeof(h));
    h[0] = rmethod;
    h[1] = dmethod;
    namelen = encode_name_ascii(name, h + 3);
    h[2] = (uint8_t)namelen;

    memcpy(h + 66, file_type, 4u);
    memcpy(h + 70, creator, 4u);

    write_be32(h + 76, cdate);
    write_be32(h + 80, mdate);
    write_be32(h + 84, rlen);
    write_be32(h + 88, dlen);
    write_be32(h + 92, rclen);
    write_be32(h + 96, dclen);
    write_be16(h + 100, rcrc);
    write_be16(h + 102, dcrc);

    hcrc = ost_crc16_ibm(h, SIT_CLASSIC_ENTRY_SIZE - 2u);
    write_be16(h + 110, hcrc);

    return fwrite(h, 1u, sizeof(h), fp) == sizeof(h);
}

static ost_status copy_file_to_archive(FILE *dst, FILE *src, uint32_t *out_size, uint16_t *out_crc) {
    uint8_t buf[1u << 15];
    uint32_t total = 0;
    uint16_t crc = 0;

    while (!feof(src)) {
        size_t n = fread(buf, 1u, sizeof(buf), src);
        if (n > 0u) {
            if (fwrite(buf, 1u, n, dst) != n) return OST_ERR_IO;
            if (total > UINT32_MAX - (uint32_t)n) return OST_ERR_UNSUPPORTED;
            total += (uint32_t)n;
            crc = crc16_ibm_update(crc, buf, n);
        }
        if (ferror(src)) return OST_ERR_IO;
    }

    *out_size = total;
    *out_crc = crc;
    return OST_OK;
}

static ost_status write_item(writer_ctx *ctx, const char *fs_path, const char *entry_name, uint64_t *out_uncompressed) {
    struct stat st;
    bool is_dir;
    uint32_t cdate;
    uint32_t mdate;

    if (!ctx || !fs_path || !entry_name || !out_uncompressed) return OST_ERR_INVALID_ARGUMENT;

    if (ctx->opts->follow_links) {
        if (stat(fs_path, &st) != 0) return OST_ERR_IO;
    }
    else {
        if (lstat(fs_path, &st) != 0) return OST_ERR_IO;
        if (S_ISLNK(st.st_mode)) return OST_ERR_UNSUPPORTED;
    }

    is_dir = S_ISDIR(st.st_mode);
    cdate = unix_to_mac_time(st.st_ctime);
    mdate = unix_to_mac_time(st.st_mtime);

    if (is_dir) {
        long start_off;
        uint64_t content_uncompressed = 0;
        char **children = NULL;
        size_t child_count = 0;
        ost_status st_list;

        start_off = ftell(ctx->fp);
        if (start_off < 0) return OST_ERR_IO;

        if (!write_entry_header(ctx->fp,
                                SIT_CLASSIC_START_FOLDER,
                                SIT_CLASSIC_START_FOLDER,
                                entry_name,
                                ctx->opts->default_type,
                                ctx->opts->default_creator,
                                cdate,
                                mdate,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0)) {
            return OST_ERR_IO;
        }
        ctx->entry_count++;

        st_list = list_directory_children(fs_path, &children, &child_count);
        if (st_list != OST_OK) return st_list;

        for (size_t i = 0; i < child_count; i++) {
            char *child_fs = path_join(fs_path, children[i]);
            uint64_t child_uncompressed = 0;
            ost_status st_child;
            if (!child_fs) {
                free_string_array(children, child_count);
                return OST_ERR_NO_MEMORY;
            }
            st_child = write_item(ctx, child_fs, children[i], &child_uncompressed);
            free(child_fs);
            if (st_child != OST_OK) {
                free_string_array(children, child_count);
                return st_child;
            }
            content_uncompressed += child_uncompressed;
        }
        free_string_array(children, child_count);

        {
            long end_off = ftell(ctx->fp);
            if (end_off < 0) return OST_ERR_IO;

            if (!write_entry_header(ctx->fp,
                                    SIT_CLASSIC_END_FOLDER,
                                    SIT_CLASSIC_END_FOLDER,
                                    entry_name,
                                    ctx->opts->default_type,
                                    ctx->opts->default_creator,
                                    cdate,
                                    mdate,
                                    0,
                                    0,
                                    0,
                                    0,
                                    0,
                                    0)) {
                return OST_ERR_IO;
            }
            ctx->entry_count++;

            if (fseek(ctx->fp, start_off, SEEK_SET) != 0) return OST_ERR_IO;
            if (!write_entry_header(ctx->fp,
                                    SIT_CLASSIC_START_FOLDER,
                                    SIT_CLASSIC_START_FOLDER,
                                    entry_name,
                                    ctx->opts->default_type,
                                    ctx->opts->default_creator,
                                    cdate,
                                    mdate,
                                    0,
                                    (uint32_t)(content_uncompressed > UINT32_MAX ? UINT32_MAX : content_uncompressed),
                                    0,
                                    (uint32_t)((uint64_t)(end_off - start_off) > UINT32_MAX ? UINT32_MAX : (uint64_t)(end_off - start_off)),
                                    0,
                                    0)) {
                return OST_ERR_IO;
            }
            if (fseek(ctx->fp, 0, SEEK_END) != 0) return OST_ERR_IO;
        }

        *out_uncompressed = SIT_CLASSIC_ENTRY_SIZE + content_uncompressed + SIT_CLASSIC_ENTRY_SIZE;
        return OST_OK;
    }

    if (!S_ISREG(st.st_mode)) return OST_ERR_UNSUPPORTED;

    {
        FILE *in = fopen(fs_path, "rb");
        long hdr_off;
        uint32_t dsize = 0;
        uint16_t dcrc = 0;
        ost_status st_copy;

        if (!in) return OST_ERR_IO;

        hdr_off = ftell(ctx->fp);
        if (hdr_off < 0) {
            fclose(in);
            return OST_ERR_IO;
        }

        {
            uint8_t zero[SIT_CLASSIC_ENTRY_SIZE];
            memset(zero, 0, sizeof(zero));
            if (fwrite(zero, 1u, sizeof(zero), ctx->fp) != sizeof(zero)) {
                fclose(in);
                return OST_ERR_IO;
            }
        }

        st_copy = copy_file_to_archive(ctx->fp, in, &dsize, &dcrc);
        fclose(in);
        if (st_copy != OST_OK) return st_copy;

        if (fseek(ctx->fp, hdr_off, SEEK_SET) != 0) return OST_ERR_IO;
        if (!write_entry_header(ctx->fp,
                                0,
                                0,
                                entry_name,
                                ctx->opts->default_type,
                                ctx->opts->default_creator,
                                cdate,
                                mdate,
                                0,
                                dsize,
                                0,
                                dsize,
                                0,
                                dcrc)) {
            return OST_ERR_IO;
        }
        if (fseek(ctx->fp, 0, SEEK_END) != 0) return OST_ERR_IO;

        ctx->entry_count++;
        *out_uncompressed = SIT_CLASSIC_ENTRY_SIZE + dsize;
        return OST_OK;
    }
}

void ost_create_options_init(ost_create_options *options) {
    if (!options) return;
    memset(options, 0, sizeof(*options));
    options->follow_links = true;
    memcpy(options->default_type, "TEXT", 4u);
    memcpy(options->default_creator, "KAHL", 4u);
}

ost_status ost_write_sit_classic(const ost_create_options *options) {
    writer_ctx ctx;
    uint8_t hdr[SIT_CLASSIC_HEADER_SIZE];

    if (!options || !options->output_path) return OST_ERR_INVALID_ARGUMENT;
    if (options->input_path_count > 0u && !options->input_paths) return OST_ERR_INVALID_ARGUMENT;

    memset(&ctx, 0, sizeof(ctx));
    ctx.opts = options;

    ctx.fp = fopen(options->output_path, "wb+");
    if (!ctx.fp) return OST_ERR_IO;

    memset(hdr, 0, sizeof(hdr));
    if (fwrite(hdr, 1u, sizeof(hdr), ctx.fp) != sizeof(hdr)) {
        fclose(ctx.fp);
        return OST_ERR_IO;
    }

    for (size_t i = 0; i < options->input_path_count; i++) {
        const char *input_path = options->input_paths[i];
        char *name = NULL;
        uint64_t uncompressed = 0;
        ost_status st;

        if (!input_path || input_path[0] == '\0') continue;
        name = path_basename_dup(input_path);
        if (!name) {
            fclose(ctx.fp);
            return OST_ERR_NO_MEMORY;
        }
        st = write_item(&ctx, input_path, name, &uncompressed);
        free(name);
        if (st != OST_OK) {
            fclose(ctx.fp);
            return st;
        }
        (void)uncompressed;
    }

    {
        long end_off = ftell(ctx.fp);
        uint32_t total_len;
        if (end_off < 0) {
            fclose(ctx.fp);
            return OST_ERR_IO;
        }
        total_len = (uint32_t)(((uint64_t)end_off > UINT32_MAX) ? UINT32_MAX : (uint64_t)end_off);

        memset(hdr, 0, sizeof(hdr));
        memcpy(hdr + 0, "SIT!", 4u);
        write_be16(hdr + 4, (uint16_t)(ctx.entry_count > 0xffffu ? 0xffffu : ctx.entry_count));
        write_be32(hdr + 6, total_len);
        memcpy(hdr + 10, "rLau", 4u);
        hdr[14] = 1;

        if (fseek(ctx.fp, 0, SEEK_SET) != 0) {
            fclose(ctx.fp);
            return OST_ERR_IO;
        }
        if (fwrite(hdr, 1u, sizeof(hdr), ctx.fp) != sizeof(hdr)) {
            fclose(ctx.fp);
            return OST_ERR_IO;
        }
    }

    if (fclose(ctx.fp) != 0) return OST_ERR_IO;
    return OST_OK;
}
