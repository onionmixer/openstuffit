#ifndef OST_ARCHIVE_H
#define OST_ARCHIVE_H

#include "ost.h"

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

#endif
