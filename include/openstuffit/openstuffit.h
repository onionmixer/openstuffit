#ifndef OPENSTUFFIT_OPENSTUFFIT_H
#define OPENSTUFFIT_OPENSTUFFIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(OPENSTUFFIT_SHARED)
#if defined(OPENSTUFFIT_BUILD)
#define OST_API __declspec(dllexport)
#else
#define OST_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define OST_API __attribute__((visibility("default")))
#else
#define OST_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OST_OK = 0,
    OST_ERR_IO,
    OST_ERR_INVALID_ARGUMENT,
    OST_ERR_UNSUPPORTED,
    OST_ERR_BAD_FORMAT,
    OST_ERR_CHECKSUM,
    OST_ERR_PASSWORD_REQUIRED,
    OST_ERR_PASSWORD_BAD,
    OST_ERR_SKIPPED,
    OST_ERR_NO_MEMORY
} ost_status;

typedef enum {
    OST_WRAPPER_RAW,
    OST_WRAPPER_MACBINARY,
    OST_WRAPPER_APPLESINGLE,
    OST_WRAPPER_APPLEDOUBLE,
    OST_WRAPPER_BINHEX,
    OST_WRAPPER_PE_SFX,
    OST_WRAPPER_UNKNOWN
} ost_wrapper_kind;

typedef enum {
    OST_FORMAT_SIT_CLASSIC,
    OST_FORMAT_SIT5,
    OST_FORMAT_SITX,
    OST_FORMAT_UNKNOWN
} ost_format_kind;

typedef enum {
    OST_UNICODE_NORMALIZE_NONE,
    OST_UNICODE_NORMALIZE_NFC,
    OST_UNICODE_NORMALIZE_NFD
} ost_unicode_normalization;

typedef enum {
    OST_ENCRYPTION_NONE,
    OST_ENCRYPTION_CLASSIC_DES,
    OST_ENCRYPTION_SIT5_RC4
} ost_encryption_kind;

typedef enum {
    OST_FORKS_SKIP,
    OST_FORKS_RSRC,
    OST_FORKS_APPLEDOUBLE,
    OST_FORKS_BOTH,
    OST_FORKS_NATIVE
} ost_fork_mode;

typedef enum {
    OST_FINDER_SKIP,
    OST_FINDER_SIDECAR
} ost_finder_mode;

typedef enum {
    OST_COLLISION_SKIP,
    OST_COLLISION_OVERWRITE,
    OST_COLLISION_RENAME
} ost_collision_mode;

typedef struct {
    uint64_t offset;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint16_t crc16;
    uint8_t method;
    ost_encryption_kind encryption;
    uint8_t classic_mkey[8];
    uint8_t classic_entry_key[16];
    uint8_t classic_padding;
    uint8_t sit5_archive_hash[5];
    uint8_t sit5_entry_key[5];
    bool method14_deflate;
    bool encrypted;
    bool present;
} ost_fork_info;

typedef struct {
    ost_wrapper_kind wrapper;
    ost_format_kind format;
    uint64_t payload_offset;
    uint64_t payload_size;
    uint8_t finder_type[4];
    uint8_t finder_creator[4];
    bool has_data_fork;
    bool has_resource_fork;
    uint64_t data_fork_offset;
    uint64_t data_fork_size;
    uint64_t resource_fork_offset;
    uint64_t resource_fork_size;
    int macbinary_version;
    bool supported;
} ost_detection;

typedef struct {
    char *path;
    bool is_dir;
    ost_fork_info data_fork;
    ost_fork_info resource_fork;
    uint8_t file_type[4];
    uint8_t creator[4];
    uint16_t finder_flags;
    uint32_t create_time_mac;
    uint32_t modify_time_mac;
    uint64_t header_offset;
} ost_entry;

typedef struct {
    ost_detection detection;
    ost_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
    const uint8_t *data;
    size_t data_size;
} ost_archive;

typedef struct {
    uint8_t *data;
    size_t size;
} ost_buffer;

typedef struct {
    char *name;
    uint8_t finder_type[4];
    uint8_t finder_creator[4];
    uint16_t finder_flags;
    ost_buffer data_fork;
    ost_buffer resource_fork;
} ost_binhex_file;

typedef struct {
    bool long_format;
    bool very_long;
    bool json;
} ost_list_options;

typedef struct {
    ost_unicode_normalization unicode_normalization;
} ost_parse_options;

typedef struct {
    const char *output_dir;
    const char *password;
    ost_fork_mode forks;
    ost_finder_mode finder;
    ost_collision_mode collision;
    bool preserve_time;
    bool verify_crc;
    size_t extracted_files;
    size_t skipped_files;
    size_t unsupported_files;
    size_t checksum_errors;
} ost_extract_options;

typedef struct ost_archive_handle ost_archive_handle;

OST_API const char *ost_status_string(ost_status status);
OST_API const char *ost_wrapper_kind_string(ost_wrapper_kind kind);
OST_API const char *ost_format_kind_string(ost_format_kind kind);

OST_API ost_status ost_read_file(const char *path, ost_buffer *out);
OST_API void ost_buffer_free(ost_buffer *buffer);
OST_API const char *ost_basename_const(const char *path);

OST_API ost_status ost_detect_buffer(const uint8_t *data, size_t size, const char *name, ost_detection *out);

OST_API void ost_archive_set_unicode_normalization(ost_unicode_normalization mode);
OST_API ost_unicode_normalization ost_archive_get_unicode_normalization(void);
OST_API void ost_parse_options_init(ost_parse_options *options);
OST_API ost_status ost_archive_parse(const uint8_t *data, size_t size, const ost_detection *det, ost_archive *archive);
OST_API ost_status ost_archive_parse_with_options(const uint8_t *data,
                                                  size_t size,
                                                  const ost_detection *det,
                                                  const ost_parse_options *options,
                                                  ost_archive *archive);
OST_API void ost_archive_free(ost_archive *archive);
OST_API ost_status ost_archive_print_list(const ost_archive *archive, const ost_list_options *options);
OST_API const uint8_t *ost_archive_data(const ost_archive *archive);
OST_API size_t ost_archive_data_size(const ost_archive *archive);
OST_API ost_status ost_archive_extract(const ost_archive *archive, ost_extract_options *options);

OST_API ost_status ost_archive_handle_open_file(const char *path,
                                                const ost_parse_options *options,
                                                ost_archive_handle **out);
OST_API ost_status ost_archive_handle_open_buffer(const uint8_t *data,
                                                  size_t size,
                                                  const char *name,
                                                  const ost_parse_options *options,
                                                  ost_archive_handle **out);
OST_API void ost_archive_handle_free(ost_archive_handle *handle);
OST_API const ost_archive *ost_archive_handle_archive(const ost_archive_handle *handle);
OST_API const ost_detection *ost_archive_handle_detection(const ost_archive_handle *handle);
OST_API size_t ost_archive_handle_entry_count(const ost_archive_handle *handle);
OST_API const ost_entry *ost_archive_handle_entry(const ost_archive_handle *handle, size_t index);
OST_API ost_status ost_archive_handle_extract(const ost_archive_handle *handle, ost_extract_options *options);

OST_API ost_status ost_binhex_decode(const uint8_t *data, size_t size, ost_binhex_file *out);
OST_API void ost_binhex_free(ost_binhex_file *file);

OST_API char *ost_macroman_to_utf8(const uint8_t *data, size_t len);
OST_API ost_status ost_normalize_utf8(const char *input, ost_unicode_normalization mode, char **output);
OST_API const char *ost_unicode_normalization_name(ost_unicode_normalization mode);
OST_API bool ost_parse_unicode_normalization(const char *name, ost_unicode_normalization *mode);

#ifdef __cplusplus
}
#endif

#endif
