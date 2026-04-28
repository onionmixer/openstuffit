#include "ost_archive.h"

#include "ost_detect.h"
#include "ost_endian.h"
#include "ost_extract.h"
#include "ost_io.h"
#include "ost_macroman.h"
#include "ost_unicode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIT_CLASSIC_HEADER_SIZE 22u
#define SIT_CLASSIC_ENTRY_SIZE 112u
#define SIT_CLASSIC_FLAG_ENCRYPTED 0x80u
#define SIT_CLASSIC_METHOD_MASK 0x7fu
#define SIT_CLASSIC_FOLDER_MASK 0x6fu
#define SIT_CLASSIC_START_FOLDER 32u
#define SIT_CLASSIC_END_FOLDER 33u
#define RESOURCE_TYPE_MKEY 0x4d4b6579u

#define SIT5_ID 0xa5a5a5a5u
#define SIT5_FLAG_DIRECTORY 0x40u
#define SIT5_FLAG_CRYPTED 0x20u
#define SIT5_ARCHIVE_FLAG_CRYPTED 0x80u
#define SIT5_ARCHIVE_FLAG_14BYTES 0x10u
#define SIT5_ARCHIVE_FLAG_20 0x20u
#define SIT5_ARCHIVE_FLAG_40 0x40u
#define SIT5_KEY_LENGTH 5u

typedef struct {
    uint64_t offset;
    char *path;
} offset_path;

struct ost_archive_handle {
    ost_buffer owned_buffer;
    bool owns_buffer;
    ost_detection detection;
    ost_archive archive;
};

static ost_unicode_normalization g_unicode_normalization = OST_UNICODE_NORMALIZE_NFC;

void ost_archive_set_unicode_normalization(ost_unicode_normalization mode) {
    g_unicode_normalization = mode;
}

ost_unicode_normalization ost_archive_get_unicode_normalization(void) {
    return g_unicode_normalization;
}

void ost_parse_options_init(ost_parse_options *options) {
    if (!options) return;
    options->unicode_normalization = OST_UNICODE_NORMALIZE_NFC;
}

static ost_unicode_normalization parse_options_normalization(const ost_parse_options *options) {
    return options ? options->unicode_normalization : OST_UNICODE_NORMALIZE_NFC;
}

static char *decode_name_with_mode(const uint8_t *data, size_t len, ost_unicode_normalization mode) {
    char *decoded = ost_macroman_to_utf8(data, len);
    if (!decoded) return NULL;
    char *normalized = NULL;
    ost_status st = ost_normalize_utf8(decoded, mode, &normalized);
    free(decoded);
    if (st != OST_OK) return NULL;
    return normalized;
}

static char *ost_strdup_c(const char *s) {
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static char *join_path(const char *prefix, const char *name) {
    if (!prefix || prefix[0] == '\0') return ost_strdup_c(name);
    size_t a = strlen(prefix);
    size_t b = strlen(name);
    char *out = (char *)malloc(a + 1 + b + 1);
    if (!out) return NULL;
    memcpy(out, prefix, a);
    out[a] = '/';
    memcpy(out + a + 1, name, b + 1);
    return out;
}

static ost_status add_entry(ost_archive *archive, const ost_entry *entry) {
    if (archive->entry_count == archive->entry_capacity) {
        size_t next = archive->entry_capacity ? archive->entry_capacity * 2 : 16;
        ost_entry *items = (ost_entry *)realloc(archive->entries, next * sizeof(*items));
        if (!items) return OST_ERR_NO_MEMORY;
        archive->entries = items;
        archive->entry_capacity = next;
    }
    archive->entries[archive->entry_count++] = *entry;
    return OST_OK;
}

static void copy_fourcc(uint8_t dest[4], const uint8_t *src) {
    memcpy(dest, src, 4);
}

static void json_string(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            putchar('\\');
            putchar((int)c);
        } else if ((c >= 32 && c < 127) || c >= 128) {
            putchar((int)c);
        } else {
            printf("\\u%04x", c);
        }
    }
    putchar('"');
}

static void print_fourcc_json(const uint8_t v[4]) {
    putchar('"');
    for (int i = 0; i < 4; i++) {
        unsigned char c = v[i];
        if (c >= 32 && c < 127 && c != '"' && c != '\\') putchar((int)c);
        else printf("\\u%04x", c);
    }
    putchar('"');
}

static void print_fourcc_text(const uint8_t v[4]) {
    for (int i = 0; i < 4; i++) {
        unsigned char c = v[i];
        putchar((c >= 32 && c < 127) ? (int)c : '?');
    }
}

static const char *method_name(uint8_t method) {
    switch (method & 0x0f) {
        case 0: return "none";
        case 1: return "rle";
        case 2: return "compress";
        case 3: return "huffman";
        case 5: return "lzah";
        case 6: return "fixed-huffman";
        case 8: return "mw";
        case 13: return "lz+huffman";
        case 14: return "installer";
        case 15: return "arsenic";
        default: return "unknown";
    }
}

static const char *fork_method_name(const ost_fork_info *fork) {
    if (fork && (fork->method & 0x0f) == 14u && fork->method14_deflate) return "deflate";
    return method_name(fork ? fork->method : 0);
}

static bool resource_fork_find_mkey(const uint8_t *data, size_t size, uint64_t off, uint64_t len, uint8_t out[8]) {
    if (!data || !out || off > size || len > size - off || len < 16u) return false;
    uint32_t data_off = 0, map_off = 0, data_len = 0, map_len = 0;
    if (!ost_read_u32_be(data, size, (size_t)off, &data_off) ||
        !ost_read_u32_be(data, size, (size_t)off + 4u, &map_off) ||
        !ost_read_u32_be(data, size, (size_t)off + 8u, &data_len) ||
        !ost_read_u32_be(data, size, (size_t)off + 12u, &map_len)) return false;
    if (data_off > len || data_len > len - data_off || map_off > len || map_len > len - map_off) return false;

    uint64_t map_abs = off + map_off;
    uint64_t data_abs = off + data_off;
    if (map_len < 30u) return false;

    uint16_t type_list_off_u = 0, name_list_off_u = 0, type_count_u = 0;
    if (!ost_read_u16_be(data, size, (size_t)map_abs + 24u, &type_list_off_u) ||
        !ost_read_u16_be(data, size, (size_t)map_abs + 26u, &name_list_off_u)) return false;
    (void)name_list_off_u;
    uint64_t type_list = map_abs + type_list_off_u;
    if (type_list < map_abs || type_list + 2u > map_abs + map_len) return false;
    if (!ost_read_u16_be(data, size, (size_t)type_list, &type_count_u)) return false;
    uint32_t type_count = (uint32_t)type_count_u + 1u;
    for (uint32_t i = 0; i < type_count; i++) {
        uint64_t type_entry = type_list + 2u + (uint64_t)i * 8u;
        if (type_entry + 8u > map_abs + map_len) return false;
        uint32_t type = 0;
        uint16_t count_u = 0, refs_off_u = 0;
        if (!ost_read_u32_be(data, size, (size_t)type_entry, &type) ||
            !ost_read_u16_be(data, size, (size_t)type_entry + 4u, &count_u) ||
            !ost_read_u16_be(data, size, (size_t)type_entry + 6u, &refs_off_u)) return false;
        if (type != RESOURCE_TYPE_MKEY) continue;
        uint32_t count = (uint32_t)count_u + 1u;
        uint64_t refs = type_list + refs_off_u;
        for (uint32_t j = 0; j < count; j++) {
            uint64_t ref = refs + (uint64_t)j * 12u;
            if (ref + 12u > map_abs + map_len) return false;
            uint16_t ident_u = 0;
            uint32_t attrs_off = 0;
            if (!ost_read_u16_be(data, size, (size_t)ref, &ident_u) ||
                !ost_read_u32_be(data, size, (size_t)ref + 4u, &attrs_off)) return false;
            if ((int16_t)ident_u != 0) continue;
            uint32_t res_data_off = attrs_off & 0x00ffffffu;
            if (res_data_off > data_len || data_len - res_data_off < 12u) return false;
            uint64_t res = data_abs + res_data_off;
            uint32_t res_len = 0;
            if (!ost_read_u32_be(data, size, (size_t)res, &res_len)) return false;
            if (res_len != 8u || res + 4u + res_len > data_abs + data_len) return false;
            memcpy(out, data + res + 4u, 8u);
            return true;
        }
    }
    return false;
}

static ost_status parse_classic(const uint8_t *data,
                                size_t size,
                                const ost_detection *det,
                                ost_unicode_normalization normalization,
                                ost_archive *archive) {
    uint64_t base = det->payload_offset;
    if (base > size || size - base < SIT_CLASSIC_HEADER_SIZE) return OST_ERR_BAD_FORMAT;

    uint16_t num_files = 0;
    uint32_t archive_len = 0;
    if (!ost_read_u16_be(data, size, (size_t)base + 4, &num_files) ||
        !ost_read_u32_be(data, size, (size_t)base + 6, &archive_len)) return OST_ERR_BAD_FORMAT;

    uint64_t limit = base + archive_len;
    if (limit > size || archive_len < SIT_CLASSIC_HEADER_SIZE) limit = size;

    char *stack[64];
    size_t stack_len = 0;
    memset(stack, 0, sizeof(stack));
    uint8_t mkey[8] = {0};
    bool has_mkey = false;
    if (det->has_resource_fork) {
        has_mkey = resource_fork_find_mkey(data, size, det->resource_fork_offset, det->resource_fork_size, mkey);
    }

    uint64_t off = base + SIT_CLASSIC_HEADER_SIZE;
    while (off <= limit && limit - off >= SIT_CLASSIC_ENTRY_SIZE) {
        size_t h = (size_t)off;
        uint8_t rmethod_raw = data[h + 0];
        uint8_t dmethod_raw = data[h + 1];
        uint8_t rmethod = (uint8_t)(rmethod_raw & SIT_CLASSIC_METHOD_MASK);
        uint8_t dmethod = (uint8_t)(dmethod_raw & SIT_CLASSIC_METHOD_MASK);
        uint8_t folder_probe = (uint8_t)(dmethod_raw & SIT_CLASSIC_FOLDER_MASK);
        if (folder_probe != SIT_CLASSIC_START_FOLDER && folder_probe != SIT_CLASSIC_END_FOLDER) {
            folder_probe = (uint8_t)(rmethod_raw & SIT_CLASSIC_FOLDER_MASK);
        }

        uint32_t rlen = 0, dlen = 0, rclen = 0, dclen = 0, ctime = 0, mtime = 0;
        uint16_t rcrc = 0, dcrc = 0, flags = 0;
        ost_read_u32_be(data, size, h + 76, &ctime);
        ost_read_u32_be(data, size, h + 80, &mtime);
        ost_read_u32_be(data, size, h + 84, &rlen);
        ost_read_u32_be(data, size, h + 88, &dlen);
        ost_read_u32_be(data, size, h + 92, &rclen);
        ost_read_u32_be(data, size, h + 96, &dclen);
        ost_read_u16_be(data, size, h + 100, &rcrc);
        ost_read_u16_be(data, size, h + 102, &dcrc);
        ost_read_u16_be(data, size, h + 74, &flags);

        unsigned namelen = data[h + 2];
        if (namelen > 31) namelen = 31;
        char *name = decode_name_with_mode(data + h + 3, namelen, normalization);
        if (!name) return OST_ERR_NO_MEMORY;

        if (folder_probe == SIT_CLASSIC_END_FOLDER) {
            free(name);
            if (stack_len > 0) {
                free(stack[--stack_len]);
                stack[stack_len] = NULL;
            }
            off += SIT_CLASSIC_ENTRY_SIZE;
            continue;
        }

        const char *prefix = stack_len ? stack[stack_len - 1] : "";
        char *path = join_path(prefix, name);
        free(name);
        if (!path) return OST_ERR_NO_MEMORY;

        ost_entry entry;
        memset(&entry, 0, sizeof(entry));
        entry.path = path;
        entry.header_offset = off;
        entry.create_time_mac = ctime;
        entry.modify_time_mac = mtime;
        entry.finder_flags = flags;
        copy_fourcc(entry.file_type, data + h + 66);
        copy_fourcc(entry.creator, data + h + 70);

        uint64_t data_start = off + SIT_CLASSIC_ENTRY_SIZE;
        if (folder_probe == SIT_CLASSIC_START_FOLDER) {
            entry.is_dir = true;
            ost_status st = add_entry(archive, &entry);
            if (st != OST_OK) {
                free(path);
                return st;
            }
            if (stack_len < sizeof(stack) / sizeof(stack[0])) {
                stack[stack_len] = ost_strdup_c(path);
                if (!stack[stack_len]) return OST_ERR_NO_MEMORY;
                stack_len++;
            }
            off += SIT_CLASSIC_ENTRY_SIZE;
            continue;
        }

        entry.resource_fork.present = rlen != 0;
        entry.resource_fork.offset = data_start;
        entry.resource_fork.uncompressed_size = rlen;
        entry.resource_fork.compressed_size = rclen;
        entry.resource_fork.crc16 = rcrc;
        entry.resource_fork.method = rmethod;
        entry.resource_fork.encrypted = (rmethod_raw & SIT_CLASSIC_FLAG_ENCRYPTED) != 0;
        if (entry.resource_fork.encrypted && rclen >= 16u && has_mkey && data_start <= size && rclen <= size - data_start) {
            entry.resource_fork.encryption = OST_ENCRYPTION_CLASSIC_DES;
            entry.resource_fork.compressed_size = rclen - 16u;
            entry.resource_fork.classic_padding = data[h + 104u];
            memcpy(entry.resource_fork.classic_mkey, mkey, 8u);
            memcpy(entry.resource_fork.classic_entry_key, data + data_start + rclen - 16u, 16u);
        }

        entry.data_fork.present = dlen != 0 || rlen == 0;
        entry.data_fork.offset = data_start + rclen;
        entry.data_fork.uncompressed_size = dlen;
        entry.data_fork.compressed_size = dclen;
        entry.data_fork.crc16 = dcrc;
        entry.data_fork.method = dmethod;
        entry.data_fork.encrypted = (dmethod_raw & SIT_CLASSIC_FLAG_ENCRYPTED) != 0;
        if (entry.data_fork.encrypted && dclen >= 16u && has_mkey && entry.data_fork.offset <= size && dclen <= size - entry.data_fork.offset) {
            entry.data_fork.encryption = OST_ENCRYPTION_CLASSIC_DES;
            entry.data_fork.compressed_size = dclen - 16u;
            entry.data_fork.classic_padding = data[h + 105u];
            memcpy(entry.data_fork.classic_mkey, mkey, 8u);
            memcpy(entry.data_fork.classic_entry_key, data + entry.data_fork.offset + dclen - 16u, 16u);
        }

        ost_status st = add_entry(archive, &entry);
        if (st != OST_OK) {
            free(path);
            return st;
        }

        off = data_start + rclen + dclen;
    }

    for (size_t i = 0; i < stack_len; i++) free(stack[i]);
    (void)num_files;
    return OST_OK;
}

static const char *lookup_dir(offset_path *dirs, size_t count, uint64_t offset) {
    for (size_t i = 0; i < count; i++) {
        if (dirs[i].offset == offset) return dirs[i].path;
    }
    return "";
}

static ost_status add_dir_map(offset_path **dirs, size_t *count, size_t *cap, uint64_t offset, const char *path) {
    if (*count == *cap) {
        size_t next = *cap ? *cap * 2 : 16;
        offset_path *items = (offset_path *)realloc(*dirs, next * sizeof(*items));
        if (!items) return OST_ERR_NO_MEMORY;
        *dirs = items;
        *cap = next;
    }
    (*dirs)[*count].offset = offset;
    (*dirs)[*count].path = ost_strdup_c(path);
    if (!(*dirs)[*count].path) return OST_ERR_NO_MEMORY;
    (*count)++;
    return OST_OK;
}

static ost_status parse_sit5(const uint8_t *data,
                             size_t size,
                             const ost_detection *det,
                             ost_unicode_normalization normalization,
                             ost_archive *archive) {
    uint64_t base = det->payload_offset;
    if (base > size || size - base < 100) return OST_ERR_BAD_FORMAT;

    uint16_t numentries = 0;
    uint32_t firstoffs = 0;
    if (!ost_read_u16_be(data, size, (size_t)base + 92, &numentries) ||
        !ost_read_u32_be(data, size, (size_t)base + 94, &firstoffs)) return OST_ERR_BAD_FORMAT;
    uint8_t archive_flags = data[(size_t)base + 83u];
    bool has_archive_hash = false;
    uint8_t archive_hash[SIT5_KEY_LENGTH] = {0};
    size_t archive_cursor = (size_t)base + 100u;
    if (archive_flags & SIT5_ARCHIVE_FLAG_14BYTES) archive_cursor += 14u;
    if (archive_flags & SIT5_ARCHIVE_FLAG_20) {
        if (archive_cursor + 4u > size) return OST_ERR_BAD_FORMAT;
        archive_cursor += 4u;
    }
    if (archive_flags & SIT5_ARCHIVE_FLAG_CRYPTED) {
        if (archive_cursor + 1u > size) return OST_ERR_BAD_FORMAT;
        uint8_t hash_size = data[archive_cursor++];
        if (hash_size != SIT5_KEY_LENGTH || archive_cursor + hash_size > size) return OST_ERR_BAD_FORMAT;
        memcpy(archive_hash, data + archive_cursor, SIT5_KEY_LENGTH);
        has_archive_hash = true;
        archive_cursor += hash_size;
    }
    if (archive_flags & SIT5_ARCHIVE_FLAG_40) {
        if (archive_cursor + 2u > size) return OST_ERR_BAD_FORMAT;
    }

    uint64_t off = base + firstoffs;
    size_t remaining_entries = numentries;
    offset_path *dirs = NULL;
    size_t dir_count = 0, dir_cap = 0;

    for (size_t i = 0; i < remaining_entries; i++) {
        if (off > size || size - off < 48) {
            free(dirs);
            return OST_ERR_BAD_FORMAT;
        }
        uint32_t id = 0, ctime = 0, mtime = 0, diroffs = 0, dlen = 0, dclen = 0;
        uint16_t headersize = 0, namelen = 0, dcrc = 0, finder_flags = 0;
        if (!ost_read_u32_be(data, size, (size_t)off, &id) || id != SIT5_ID ||
            !ost_read_u16_be(data, size, (size_t)off + 6, &headersize) ||
            !ost_read_u32_be(data, size, (size_t)off + 10, &ctime) ||
            !ost_read_u32_be(data, size, (size_t)off + 14, &mtime) ||
            !ost_read_u32_be(data, size, (size_t)off + 26, &diroffs) ||
            !ost_read_u16_be(data, size, (size_t)off + 30, &namelen) ||
            !ost_read_u32_be(data, size, (size_t)off + 34, &dlen) ||
            !ost_read_u32_be(data, size, (size_t)off + 38, &dclen) ||
            !ost_read_u16_be(data, size, (size_t)off + 42, &dcrc)) {
            free(dirs);
            return OST_ERR_BAD_FORMAT;
        }
        if (headersize < 48 || off + headersize > size) {
            free(dirs);
            return OST_ERR_BAD_FORMAT;
        }

        uint8_t version = data[off + 4];
        uint8_t flags = data[off + 9];
        bool is_dir = (flags & SIT5_FLAG_DIRECTORY) != 0;
        uint8_t dmethod = 0;
        uint8_t dkey[SIT5_KEY_LENGTH] = {0};
        bool has_dkey = false;
        uint16_t child_count = 0;
        size_t cursor = (size_t)off + 46;
        if (is_dir) {
            if (!ost_read_u16_be(data, size, cursor, &child_count)) {
                free(dirs);
                return OST_ERR_BAD_FORMAT;
            }
            cursor += 2;
            if (dlen == 0xffffffffu) {
                remaining_entries++;
                off += headersize;
                continue;
            }
        } else {
            dmethod = data[cursor++];
            uint8_t passlen = data[cursor++];
            if (cursor + passlen > size) {
                free(dirs);
                return OST_ERR_BAD_FORMAT;
            }
            if ((flags & SIT5_FLAG_CRYPTED) && dlen != 0) {
                if (passlen != SIT5_KEY_LENGTH) {
                    free(dirs);
                    return OST_ERR_UNSUPPORTED;
                }
                memcpy(dkey, data + cursor, SIT5_KEY_LENGTH);
                has_dkey = true;
            } else if (passlen != 0) {
                free(dirs);
                return OST_ERR_UNSUPPORTED;
            }
            cursor += passlen;
        }

        if (cursor + namelen > size) {
            free(dirs);
            return OST_ERR_BAD_FORMAT;
        }
        char *name = decode_name_with_mode(data + cursor, namelen, normalization);
        if (!name) {
            free(dirs);
            return OST_ERR_NO_MEMORY;
        }
        cursor += namelen;

        if (cursor < off + headersize) {
            uint16_t comment_size = 0;
            if (!ost_read_u16_be(data, size, cursor, &comment_size)) {
                free(name);
                free(dirs);
                return OST_ERR_BAD_FORMAT;
            }
            cursor += 4u + comment_size;
            if (cursor > size) {
                free(name);
                free(dirs);
                return OST_ERR_BAD_FORMAT;
            }
        }

        if (cursor + 14 > size) {
            free(name);
            free(dirs);
            return OST_ERR_BAD_FORMAT;
        }
        uint16_t meta = 0;
        uint32_t file_type = 0, creator = 0;
        ost_read_u16_be(data, size, cursor, &meta);
        cursor += 4;
        ost_read_u32_be(data, size, cursor, &file_type);
        cursor += 4;
        ost_read_u32_be(data, size, cursor, &creator);
        cursor += 4;
        ost_read_u16_be(data, size, cursor, &finder_flags);
        cursor += 2;
        cursor += version == 1 ? 22u : 18u;

        uint32_t rlen = 0, rclen = 0;
        uint16_t rcrc = 0;
        uint8_t rmethod = 0;
        uint8_t rkey[SIT5_KEY_LENGTH] = {0};
        bool has_rkey = false;
        bool has_resource = (meta & 0x01u) != 0;
        if (has_resource) {
            if (cursor + 12 > size ||
                !ost_read_u32_be(data, size, cursor, &rlen) ||
                !ost_read_u32_be(data, size, cursor + 4, &rclen) ||
                !ost_read_u16_be(data, size, cursor + 8, &rcrc)) {
                free(name);
                free(dirs);
                return OST_ERR_BAD_FORMAT;
            }
            rmethod = data[cursor + 12];
            uint8_t passlen = data[cursor + 13];
            cursor += 14u;
            if (cursor + passlen > size) {
                free(name);
                free(dirs);
                return OST_ERR_BAD_FORMAT;
            }
            if ((flags & SIT5_FLAG_CRYPTED) && rlen != 0) {
                if (passlen != SIT5_KEY_LENGTH) {
                    free(name);
                    free(dirs);
                    return OST_ERR_UNSUPPORTED;
                }
                memcpy(rkey, data + cursor, SIT5_KEY_LENGTH);
                has_rkey = true;
            } else if (passlen != 0) {
                free(name);
                free(dirs);
                return OST_ERR_UNSUPPORTED;
            }
            cursor += passlen;
        }

        uint64_t data_start = cursor;
        const char *parent = lookup_dir(dirs, dir_count, diroffs);
        char *path = join_path(parent, name);
        free(name);
        if (!path) {
            free(dirs);
            return OST_ERR_NO_MEMORY;
        }

        ost_entry entry;
        memset(&entry, 0, sizeof(entry));
        entry.path = path;
        entry.is_dir = is_dir;
        entry.header_offset = off;
        entry.create_time_mac = ctime;
        entry.modify_time_mac = mtime;
        entry.finder_flags = finder_flags;
        entry.file_type[0] = (uint8_t)(file_type >> 24);
        entry.file_type[1] = (uint8_t)(file_type >> 16);
        entry.file_type[2] = (uint8_t)(file_type >> 8);
        entry.file_type[3] = (uint8_t)file_type;
        entry.creator[0] = (uint8_t)(creator >> 24);
        entry.creator[1] = (uint8_t)(creator >> 16);
        entry.creator[2] = (uint8_t)(creator >> 8);
        entry.creator[3] = (uint8_t)creator;

        if (is_dir) {
            ost_status st = add_entry(archive, &entry);
            if (st != OST_OK) {
                free(path);
                free(dirs);
                return st;
            }
            st = add_dir_map(&dirs, &dir_count, &dir_cap, off - base, path);
            if (st != OST_OK) {
                free(dirs);
                return st;
            }
            remaining_entries += child_count;
            off = data_start;
        } else {
            entry.resource_fork.present = has_resource;
            entry.resource_fork.offset = data_start;
            entry.resource_fork.uncompressed_size = rlen;
            entry.resource_fork.compressed_size = rclen;
            entry.resource_fork.crc16 = rcrc;
            entry.resource_fork.method = rmethod;
            entry.resource_fork.method14_deflate = (rmethod & 0x0f) == 14u;
            entry.resource_fork.encrypted = (flags & SIT5_FLAG_CRYPTED) != 0;
            if (entry.resource_fork.encrypted && has_archive_hash && has_rkey) {
                entry.resource_fork.encryption = OST_ENCRYPTION_SIT5_RC4;
                memcpy(entry.resource_fork.sit5_archive_hash, archive_hash, SIT5_KEY_LENGTH);
                memcpy(entry.resource_fork.sit5_entry_key, rkey, SIT5_KEY_LENGTH);
            }

            entry.data_fork.present = dlen != 0 || !has_resource;
            entry.data_fork.offset = data_start + rclen;
            entry.data_fork.uncompressed_size = dlen;
            entry.data_fork.compressed_size = dclen;
            entry.data_fork.crc16 = dcrc;
            entry.data_fork.method = dmethod;
            entry.data_fork.method14_deflate = (dmethod & 0x0f) == 14u;
            entry.data_fork.encrypted = (flags & SIT5_FLAG_CRYPTED) != 0;
            if (entry.data_fork.encrypted && has_archive_hash && has_dkey) {
                entry.data_fork.encryption = OST_ENCRYPTION_SIT5_RC4;
                memcpy(entry.data_fork.sit5_archive_hash, archive_hash, SIT5_KEY_LENGTH);
                memcpy(entry.data_fork.sit5_entry_key, dkey, SIT5_KEY_LENGTH);
            }

            ost_status st = add_entry(archive, &entry);
            if (st != OST_OK) {
                free(path);
                free(dirs);
                return st;
            }
            off = data_start + rclen + dclen;
        }
    }

    for (size_t i = 0; i < dir_count; i++) free(dirs[i].path);
    free(dirs);
    return OST_OK;
}

ost_status ost_archive_parse(const uint8_t *data, size_t size, const ost_detection *det, ost_archive *archive) {
    ost_parse_options options;
    options.unicode_normalization = g_unicode_normalization;
    return ost_archive_parse_with_options(data, size, det, &options, archive);
}

ost_status ost_archive_parse_with_options(const uint8_t *data,
                                          size_t size,
                                          const ost_detection *det,
                                          const ost_parse_options *options,
                                          ost_archive *archive) {
    if (!data || !det || !archive) return OST_ERR_INVALID_ARGUMENT;
    memset(archive, 0, sizeof(*archive));
    archive->detection = *det;
    archive->data = data;
    archive->data_size = size;
    ost_unicode_normalization normalization = parse_options_normalization(options);
    if (det->format == OST_FORMAT_SIT_CLASSIC) return parse_classic(data, size, det, normalization, archive);
    if (det->format == OST_FORMAT_SIT5) return parse_sit5(data, size, det, normalization, archive);
    return OST_ERR_UNSUPPORTED;
}

const uint8_t *ost_archive_data(const ost_archive *archive) {
    return archive ? archive->data : NULL;
}

size_t ost_archive_data_size(const ost_archive *archive) {
    return archive ? archive->data_size : 0;
}

void ost_archive_free(ost_archive *archive) {
    if (!archive) return;
    for (size_t i = 0; i < archive->entry_count; i++) free(archive->entries[i].path);
    free(archive->entries);
    memset(archive, 0, sizeof(*archive));
}

ost_status ost_archive_handle_open_buffer(const uint8_t *data,
                                          size_t size,
                                          const char *name,
                                          const ost_parse_options *options,
                                          ost_archive_handle **out) {
    if (!data || !out) return OST_ERR_INVALID_ARGUMENT;
    *out = NULL;
    ost_archive_handle *handle = (ost_archive_handle *)calloc(1, sizeof(*handle));
    if (!handle) return OST_ERR_NO_MEMORY;

    ost_status st = ost_detect_buffer(data, size, name ? name : "", &handle->detection);
    if (st == OST_OK) st = ost_archive_parse_with_options(data, size, &handle->detection, options, &handle->archive);
    if (st != OST_OK) {
        free(handle);
        return st;
    }
    *out = handle;
    return OST_OK;
}

ost_status ost_archive_handle_open_file(const char *path,
                                        const ost_parse_options *options,
                                        ost_archive_handle **out) {
    if (!path || !out) return OST_ERR_INVALID_ARGUMENT;
    *out = NULL;
    ost_archive_handle *handle = (ost_archive_handle *)calloc(1, sizeof(*handle));
    if (!handle) return OST_ERR_NO_MEMORY;

    ost_status st = ost_read_file(path, &handle->owned_buffer);
    if (st == OST_OK) {
        handle->owns_buffer = true;
        st = ost_detect_buffer(handle->owned_buffer.data, handle->owned_buffer.size, ost_basename_const(path), &handle->detection);
    }
    if (st == OST_OK) {
        st = ost_archive_parse_with_options(handle->owned_buffer.data,
                                            handle->owned_buffer.size,
                                            &handle->detection,
                                            options,
                                            &handle->archive);
    }
    if (st != OST_OK) {
        ost_buffer_free(&handle->owned_buffer);
        free(handle);
        return st;
    }
    *out = handle;
    return OST_OK;
}

void ost_archive_handle_free(ost_archive_handle *handle) {
    if (!handle) return;
    ost_archive_free(&handle->archive);
    if (handle->owns_buffer) ost_buffer_free(&handle->owned_buffer);
    free(handle);
}

const ost_archive *ost_archive_handle_archive(const ost_archive_handle *handle) {
    return handle ? &handle->archive : NULL;
}

const ost_detection *ost_archive_handle_detection(const ost_archive_handle *handle) {
    return handle ? &handle->detection : NULL;
}

size_t ost_archive_handle_entry_count(const ost_archive_handle *handle) {
    return handle ? handle->archive.entry_count : 0;
}

const ost_entry *ost_archive_handle_entry(const ost_archive_handle *handle, size_t index) {
    if (!handle || index >= handle->archive.entry_count) return NULL;
    return &handle->archive.entries[index];
}

ost_status ost_archive_handle_extract(const ost_archive_handle *handle, ost_extract_options *options) {
    if (!handle) return OST_ERR_INVALID_ARGUMENT;
    return ost_archive_extract(&handle->archive, options);
}

ost_status ost_archive_print_list(const ost_archive *archive, const ost_list_options *options) {
    if (!archive || !options) return OST_ERR_INVALID_ARGUMENT;
    if (options->json) {
        printf("{\"format\":\"%s\",\"wrapper\":\"%s\",\"entries\":[",
               ost_format_kind_string(archive->detection.format),
               ost_wrapper_kind_string(archive->detection.wrapper));
        for (size_t i = 0; i < archive->entry_count; i++) {
            const ost_entry *e = &archive->entries[i];
            if (i) printf(",");
            printf("{\"index\":%llu,\"path\":", (unsigned long long)i);
            json_string(e->path);
            printf(",\"kind\":\"%s\"", e->is_dir ? "directory" : "file");
            printf(",\"data_fork\":{\"present\":%s,\"size\":%llu,\"compressed_size\":%llu,\"method\":%u,\"method_name\":\"%s\",\"crc16\":\"%04x\",\"encrypted\":%s}",
                   e->data_fork.present ? "true" : "false",
                   (unsigned long long)e->data_fork.uncompressed_size,
                   (unsigned long long)e->data_fork.compressed_size,
                   (unsigned)e->data_fork.method,
                   fork_method_name(&e->data_fork),
                   (unsigned)e->data_fork.crc16,
                   e->data_fork.encrypted ? "true" : "false");
            printf(",\"resource_fork\":{\"present\":%s,\"size\":%llu,\"compressed_size\":%llu,\"method\":%u,\"method_name\":\"%s\",\"crc16\":\"%04x\",\"encrypted\":%s}",
                   e->resource_fork.present ? "true" : "false",
                   (unsigned long long)e->resource_fork.uncompressed_size,
                   (unsigned long long)e->resource_fork.compressed_size,
                   (unsigned)e->resource_fork.method,
                   fork_method_name(&e->resource_fork),
                   (unsigned)e->resource_fork.crc16,
                   e->resource_fork.encrypted ? "true" : "false");
            printf(",\"finder\":{\"type\":");
            print_fourcc_json(e->file_type);
            printf(",\"creator\":");
            print_fourcc_json(e->creator);
            printf(",\"flags\":\"%04x\"}", (unsigned)e->finder_flags);
            if (options->very_long) {
                printf(",\"offsets\":{\"header\":%llu,\"data\":%llu,\"resource\":%llu}",
                       (unsigned long long)e->header_offset,
                       (unsigned long long)e->data_fork.offset,
                       (unsigned long long)e->resource_fork.offset);
            }
            printf("}");
        }
        printf("]}\n");
        return OST_OK;
    }

    if (options->long_format || options->very_long) {
        printf("%-5s %-42s %-4s %-10s %-10s %-4s %-4s %-12s\n",
               "Index", "Name", "Kind", "Data", "Resource", "DMet", "RMet", "Type/Creator");
        for (size_t i = 0; i < archive->entry_count; i++) {
            const ost_entry *e = &archive->entries[i];
            printf("%-5llu %-42s %-4s %-10llu %-10llu %-4u %-4u ",
                   (unsigned long long)i,
                   e->path,
                   e->is_dir ? "dir" : "file",
                   (unsigned long long)e->data_fork.uncompressed_size,
                   (unsigned long long)e->resource_fork.uncompressed_size,
                   (unsigned)e->data_fork.method,
                   (unsigned)e->resource_fork.method);
            print_fourcc_text(e->file_type);
            putchar('/');
            print_fourcc_text(e->creator);
            if (options->very_long) {
                printf(" header=%llu data_off=%llu rsrc_off=%llu crc=%04x/%04x",
                       (unsigned long long)e->header_offset,
                       (unsigned long long)e->data_fork.offset,
                       (unsigned long long)e->resource_fork.offset,
                       (unsigned)e->data_fork.crc16,
                       (unsigned)e->resource_fork.crc16);
            }
            putchar('\n');
        }
    } else {
        for (size_t i = 0; i < archive->entry_count; i++) {
            printf("%s%s\n", archive->entries[i].path, archive->entries[i].is_dir ? "/" : "");
        }
    }
    return OST_OK;
}
