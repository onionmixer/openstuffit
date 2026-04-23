#include "ost_dump.h"

#include "ost_detect.h"
#include "ost_endian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void print_json_string(const char *s) {
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

static const char *dump_method_name(uint8_t method) {
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

static const char *dump_fork_method_name(const ost_fork_info *fork) {
    if (fork && (fork->method & 0x0f) == 14u && fork->method14_deflate) return "deflate";
    return dump_method_name(fork ? fork->method : 0);
}

static const char *dump_encryption_name(const ost_fork_info *fork) {
    if (!fork || !fork->encrypted) return "none";
    switch (fork->encryption) {
        case OST_ENCRYPTION_NONE: return "unknown";
        case OST_ENCRYPTION_CLASSIC_DES: return "classic-des";
        case OST_ENCRYPTION_SIT5_RC4: return "sit5-rc4";
    }
    return "unknown";
}

static ost_status dump_classic(const uint8_t *data, size_t size, uint64_t off, bool json) {
    if (off > size || size - off < 22) return OST_ERR_BAD_FORMAT;
    uint16_t num = 0;
    uint32_t len = 0;
    ost_read_u16_be(data, size, (size_t)off + 4, &num);
    ost_read_u32_be(data, size, (size_t)off + 6, &len);

    if (json) {
        printf("{\"format\":\"sit-classic\",\"archive_header\":{\"offset\":%llu,\"num_files\":%u,\"archive_length\":%u,\"version\":%u}",
               (unsigned long long)off, (unsigned)num, (unsigned)len, (unsigned)data[off + 14]);
    } else {
        printf("Format: sit-classic\n");
        printf("Archive header:\n");
        printf("  offset: %llu\n", (unsigned long long)off);
        printf("  sig1: %.4s\n", data + off);
        printf("  numFiles: %u\n", (unsigned)num);
        printf("  arcLen: %u\n", (unsigned)len);
        printf("  sig2: %.4s\n", data + off + 10);
        printf("  version: %u\n", (unsigned)data[off + 14]);
    }

    uint64_t fh = off + 22;
    if (fh <= size && size - fh >= 112) {
        uint32_t rlen = 0, dlen = 0, crlen = 0, cdlen = 0;
        uint16_t rcrc = 0, dcrc = 0, hcrc = 0, flags = 0;
        ost_read_u32_be(data, size, (size_t)fh + 84, &rlen);
        ost_read_u32_be(data, size, (size_t)fh + 88, &dlen);
        ost_read_u32_be(data, size, (size_t)fh + 92, &crlen);
        ost_read_u32_be(data, size, (size_t)fh + 96, &cdlen);
        ost_read_u16_be(data, size, (size_t)fh + 100, &rcrc);
        ost_read_u16_be(data, size, (size_t)fh + 102, &dcrc);
        ost_read_u16_be(data, size, (size_t)fh + 110, &hcrc);
        ost_read_u16_be(data, size, (size_t)fh + 74, &flags);
        unsigned namelen = data[fh + 2];
        if (namelen > 31) namelen = 31;

        if (json) {
            printf(",\"first_entry\":{\"offset\":%llu,\"resource_method\":%u,\"data_method\":%u,\"name_length\":%u,\"name\":\"",
                   (unsigned long long)fh, (unsigned)data[fh], (unsigned)data[fh + 1], namelen);
            for (unsigned i = 0; i < namelen; i++) {
                unsigned char c = data[fh + 3 + i];
                if (c == '"' || c == '\\') printf("\\%c", c);
                else if (c >= 32 && c < 127) putchar((int)c);
                else printf("\\u%04x", c);
            }
            printf("\",\"resource_length\":%u,\"data_length\":%u,\"resource_compressed_length\":%u,\"data_compressed_length\":%u,\"resource_crc16\":\"%04x\",\"data_crc16\":\"%04x\",\"header_crc16\":\"%04x\"}}}\n",
                   (unsigned)rlen, (unsigned)dlen, (unsigned)crlen, (unsigned)cdlen, (unsigned)rcrc, (unsigned)dcrc, (unsigned)hcrc);
        } else {
            printf("First entry header:\n");
            printf("  offset: %llu\n", (unsigned long long)fh);
            printf("  resource method: %u\n", (unsigned)data[fh]);
            printf("  data method: %u\n", (unsigned)data[fh + 1]);
            printf("  name: %.*s\n", (int)namelen, data + fh + 3);
            printf("  file type: %.4s\n", data + fh + 66);
            printf("  creator: %.4s\n", data + fh + 70);
            printf("  finder flags: 0x%04x\n", (unsigned)flags);
            printf("  resource length: %u compressed=%u crc=%04x\n", (unsigned)rlen, (unsigned)crlen, (unsigned)rcrc);
            printf("  data length: %u compressed=%u crc=%04x\n", (unsigned)dlen, (unsigned)cdlen, (unsigned)dcrc);
            printf("  header crc: %04x\n", (unsigned)hcrc);
        }
    } else if (json) {
        printf("}\n");
    }

    return OST_OK;
}

static ost_status dump_sit5(const uint8_t *data, size_t size, uint64_t off, bool json) {
    if (off > size || size - off < 100) return OST_ERR_BAD_FORMAT;
    uint32_t total = 0, first = 0;
    uint16_t root = 0;
    ost_read_u32_be(data, size, (size_t)off + 84, &total);
    ost_read_u16_be(data, size, (size_t)off + 92, &root);
    ost_read_u32_be(data, size, (size_t)off + 94, &first);
    if (json) {
        printf("{\"format\":\"sit5\",\"archive_header\":{\"offset\":%llu,\"version\":%u,\"flags\":%u,\"total_size\":%u,\"root_entries\":%u,\"first_entry_offset\":%u}}\n",
               (unsigned long long)off, (unsigned)data[off + 82], (unsigned)data[off + 83], (unsigned)total, (unsigned)root, (unsigned)first);
    } else {
        printf("Format: sit5\n");
        printf("Archive header:\n");
        printf("  offset: %llu\n", (unsigned long long)off);
        printf("  version: %u\n", (unsigned)data[off + 82]);
        printf("  flags: 0x%02x\n", (unsigned)data[off + 83]);
        printf("  total size: %u\n", (unsigned)total);
        printf("  root entries: %u\n", (unsigned)root);
        printf("  first entry offset: %u\n", (unsigned)first);
    }
    return OST_OK;
}

ost_status ost_dump_headers(const uint8_t *data, size_t size, const ost_detection *det, bool json) {
    if (!data || !det) return OST_ERR_INVALID_ARGUMENT;
    if (det->format == OST_FORMAT_SIT_CLASSIC) return dump_classic(data, size, det->payload_offset, json);
    if (det->format == OST_FORMAT_SIT5) return dump_sit5(data, size, det->payload_offset, json);
    if (json) {
        printf("{\"format\":\"%s\",\"supported\":false}\n", ost_format_kind_string(det->format));
    } else {
        printf("Format: %s\n", ost_format_kind_string(det->format));
        printf("No supported header dump for this format yet.\n");
    }
    return OST_ERR_UNSUPPORTED;
}

ost_status ost_dump_forks(const ost_detection *det, bool json) {
    if (!det) return OST_ERR_INVALID_ARGUMENT;
    if (json) {
        printf("{\"wrapper\":\"%s\",\"format\":\"%s\",\"payload\":{\"offset\":%llu,\"size\":%llu},",
               ost_wrapper_kind_string(det->wrapper),
               ost_format_kind_string(det->format),
               (unsigned long long)det->payload_offset,
               (unsigned long long)det->payload_size);
        printf("\"data_fork\":{\"present\":%s,\"offset\":%llu,\"size\":%llu},",
               det->has_data_fork ? "true" : "false",
               (unsigned long long)det->data_fork_offset,
               (unsigned long long)det->data_fork_size);
        printf("\"resource_fork\":{\"present\":%s,\"offset\":%llu,\"size\":%llu},",
               det->has_resource_fork ? "true" : "false",
               (unsigned long long)det->resource_fork_offset,
               (unsigned long long)det->resource_fork_size);
        printf("\"finder\":{\"type\":\"");
        print_fourcc_json(det->finder_type);
        printf("\",\"creator\":\"");
        print_fourcc_json(det->finder_creator);
        printf("\"},\"macbinary_version\":%d}\n", det->macbinary_version);
        return OST_OK;
    }

    printf("Wrapper fork map:\n");
    printf("  wrapper: %s\n", ost_wrapper_kind_string(det->wrapper));
    printf("  format: %s\n", ost_format_kind_string(det->format));
    printf("  payload: offset=%llu size=%llu\n",
           (unsigned long long)det->payload_offset,
           (unsigned long long)det->payload_size);
    printf("  data fork: present=%s offset=%llu size=%llu\n",
           det->has_data_fork ? "yes" : "no",
           (unsigned long long)det->data_fork_offset,
           (unsigned long long)det->data_fork_size);
    printf("  resource fork: present=%s offset=%llu size=%llu\n",
           det->has_resource_fork ? "yes" : "no",
           (unsigned long long)det->resource_fork_offset,
           (unsigned long long)det->resource_fork_size);
    printf("  finder: type=");
    print_fourcc_text(det->finder_type);
    printf(" creator=");
    print_fourcc_text(det->finder_creator);
    printf("\n");
    if (det->wrapper == OST_WRAPPER_MACBINARY) printf("  macbinary version: %d\n", det->macbinary_version);
    return OST_OK;
}

static void dump_fork_json(const char *name, const ost_fork_info *fork) {
    printf("\"%s\":{\"present\":%s,\"offset\":%llu,\"size\":%llu,\"compressed_size\":%llu,\"method\":%u,\"method_name\":\"%s\",\"crc16\":\"%04x\",\"encrypted\":%s,\"encryption\":\"%s\"",
           name,
           fork->present ? "true" : "false",
           (unsigned long long)fork->offset,
           (unsigned long long)fork->uncompressed_size,
           (unsigned long long)fork->compressed_size,
           (unsigned)fork->method,
           dump_fork_method_name(fork),
           (unsigned)fork->crc16,
           fork->encrypted ? "true" : "false",
           dump_encryption_name(fork));
    if (fork->encryption == OST_ENCRYPTION_CLASSIC_DES) {
        printf(",\"classic_padding\":%u", (unsigned)fork->classic_padding);
    }
    printf("}");
}

static void dump_fork_text(const char *name, const ost_fork_info *fork) {
    printf("  %s: present=%s offset=%llu size=%llu compressed=%llu method=%u/%s crc=%04x encrypted=%s encryption=%s",
           name,
           fork->present ? "yes" : "no",
           (unsigned long long)fork->offset,
           (unsigned long long)fork->uncompressed_size,
           (unsigned long long)fork->compressed_size,
           (unsigned)fork->method,
           dump_fork_method_name(fork),
           (unsigned)fork->crc16,
           fork->encrypted ? "yes" : "no",
           dump_encryption_name(fork));
    if (fork->encryption == OST_ENCRYPTION_CLASSIC_DES) {
        printf(" classic_padding=%u", (unsigned)fork->classic_padding);
    }
    putchar('\n');
}

ost_status ost_dump_entry(const ost_archive *archive, const char *selector, bool json) {
    if (!archive || !selector) return OST_ERR_INVALID_ARGUMENT;
    const ost_entry *entry = NULL;
    size_t index = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(selector, &end, 10);
    if (end && *end == '\0') {
        if (parsed >= (unsigned long long)archive->entry_count) return OST_ERR_BAD_FORMAT;
        index = (size_t)parsed;
        entry = &archive->entries[index];
    } else {
        for (size_t i = 0; i < archive->entry_count; i++) {
            if (strcmp(archive->entries[i].path, selector) == 0) {
                index = i;
                entry = &archive->entries[i];
                break;
            }
        }
        if (!entry) return OST_ERR_BAD_FORMAT;
    }

    if (json) {
        printf("{\"index\":%llu,\"path\":", (unsigned long long)index);
        print_json_string(entry->path);
        printf(",\"kind\":\"%s\",\"header_offset\":%llu,\"finder\":{\"type\":\"",
               entry->is_dir ? "directory" : "file",
               (unsigned long long)entry->header_offset);
        print_fourcc_json(entry->file_type);
        printf("\",\"creator\":\"");
        print_fourcc_json(entry->creator);
        printf("\",\"flags\":\"%04x\"},", (unsigned)entry->finder_flags);
        printf("\"mac_time\":{\"create\":%u,\"modify\":%u},",
               entry->create_time_mac,
               entry->modify_time_mac);
        dump_fork_json("data_fork", &entry->data_fork);
        printf(",");
        dump_fork_json("resource_fork", &entry->resource_fork);
        printf("}\n");
        return OST_OK;
    }

    printf("Entry:\n");
    printf("  index: %llu\n", (unsigned long long)index);
    printf("  path: %s\n", entry->path);
    printf("  kind: %s\n", entry->is_dir ? "directory" : "file");
    printf("  header offset: %llu\n", (unsigned long long)entry->header_offset);
    printf("  finder: type=");
    print_fourcc_text(entry->file_type);
    printf(" creator=");
    print_fourcc_text(entry->creator);
    printf(" flags=0x%04x\n", (unsigned)entry->finder_flags);
    printf("  mac time: create=%u modify=%u\n", entry->create_time_mac, entry->modify_time_mac);
    dump_fork_text("data fork", &entry->data_fork);
    dump_fork_text("resource fork", &entry->resource_fork);
    return OST_OK;
}

ost_status ost_dump_hex(const uint8_t *data, size_t size, uint64_t offset, uint64_t length, bool json) {
    if (!data) return OST_ERR_INVALID_ARGUMENT;
    if (offset > (uint64_t)size) return OST_ERR_BAD_FORMAT;
    if (length > (uint64_t)size - offset) return OST_ERR_BAD_FORMAT;
    if (length > (uint64_t)SIZE_MAX) return OST_ERR_BAD_FORMAT;

    const uint8_t *p = data + (size_t)offset;
    size_t len = (size_t)length;
    if (json) {
        printf("{\"offset\":%llu,\"length\":%llu,\"hex\":\"",
               (unsigned long long)offset,
               (unsigned long long)length);
        for (size_t i = 0; i < len; i++) printf("%02x", (unsigned)p[i]);
        printf("\"}\n");
        return OST_OK;
    }

    for (size_t row = 0; row < len; row += 16u) {
        size_t row_len = len - row;
        if (row_len > 16u) row_len = 16u;
        printf("%08llx  ", (unsigned long long)(offset + (uint64_t)row));
        for (size_t i = 0; i < 16u; i++) {
            if (i < row_len) printf("%02x", (unsigned)p[row + i]);
            else printf("  ");
            if (i % 2u == 1u) putchar(' ');
        }
        putchar(' ');
        for (size_t i = 0; i < row_len; i++) {
            unsigned char c = p[row + i];
            putchar((c >= 32 && c < 127) ? (int)c : '.');
        }
        putchar('\n');
    }
    return OST_OK;
}
