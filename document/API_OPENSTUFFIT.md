# OpenStuffIt C API

This document describes the supported C library surface for `openstuffit`.

## Header

Public consumers include one header:

```c
#include <openstuffit/openstuffit.h>
```

Headers under `src/` are internal implementation headers and are not installed.

## Build And Link

Installed package layout:

```text
bin/openstuffit
lib/libopenstuffit.a
lib/libopenstuffit.so*      Linux and ELF platforms
lib/libopenstuffit*.dylib   macOS
include/openstuffit/openstuffit.h
lib/pkgconfig/openstuffit.pc
```

Dynamic link:

```sh
cc -std=c99 -D_FILE_OFFSET_BITS=64 example.c $(pkg-config --cflags --libs openstuffit)
```

Static link:

```sh
cc -std=c99 -D_FILE_OFFSET_BITS=64 example.c $(pkg-config --cflags --static --libs openstuffit)
```

`Libs.private` contains `-lz`, so `pkg-config --static` includes the zlib dependency.

## Stable API

The ABI-stable public API is limited to symbols exported by the shared library and declared in `openstuffit.h`:

- status and enum string helpers
- file buffer helpers: `ost_read_file`, `ost_buffer_free`, `ost_basename_const`
- format detection: `ost_detect_buffer`
- preferred opaque archive handle helpers: `ost_archive_handle_*`
- low-level archive parse/list/extract/free helpers
- BinHex decode/free helpers
- MacRoman and Unicode normalization helpers

The expected export list is tested by `make test-symbols` against `tests/expected/exported_symbols.txt`.

## Preferred Archive API

Use `ost_archive_handle` for new library code. It is opaque and keeps ownership rules simpler than the low-level struct API.

Typical flow:

```c
ost_parse_options options;
ost_parse_options_init(&options);
options.unicode_normalization = OST_UNICODE_NORMALIZE_NFC;

ost_archive_handle *archive = NULL;
ost_status st = ost_archive_handle_open_file(path, &options, &archive);
if (st != OST_OK) {
    /* handle error */
}

for (size_t i = 0; i < ost_archive_handle_entry_count(archive); i++) {
    const ost_entry *entry = ost_archive_handle_entry(archive, i);
    /* inspect entry */
}

ost_archive_handle_free(archive);
```

`ost_archive_handle_open_file` owns the file buffer internally. `ost_archive_handle_open_buffer` borrows the caller-provided buffer; the caller must keep that buffer alive until `ost_archive_handle_free`.

`ost_archive_handle_extract` delegates extraction to the parsed archive and updates `ost_extract_options` counters.

## Low-Level Archive API

The low-level API remains available for callers that need direct access to `ost_archive` storage:

- call `ost_detect_buffer`
- call `ost_archive_parse_with_options`
- inspect `archive.entries`
- call `ost_archive_extract` if extraction is needed
- call `ost_archive_free`

`ost_archive_parse` remains for source compatibility, but it uses the process-global Unicode normalization state. Prefer `ost_archive_parse_with_options`.

## Function Details

`ost_parse_options_init(options)`
: Initializes parse options. Current default is NFC filename normalization.

`ost_detect_buffer(data, size, name, out)`
: Detects raw SIT/SIT5/SITX and supported Mac wrapper formats. `name` is optional context for extension-sensitive wrapper handling.

`ost_archive_parse_with_options(data, size, det, options, archive)`
: Parses a previously detected archive. The `data` buffer is borrowed and must outlive `archive`.

`ost_archive_handle_open_file(path, options, out)`
: Reads, detects, and parses an archive from disk. The returned handle owns the file buffer.

`ost_archive_handle_open_buffer(data, size, name, options, out)`
: Detects and parses a caller-owned buffer. The returned handle borrows `data`.

`ost_archive_handle_entry(handle, index)`
: Returns a borrowed pointer valid until `ost_archive_handle_free`.

`ost_archive_handle_free(handle)`
: Releases the handle and all archive metadata owned by it.

`ost_archive_extract(archive, options)` and `ost_archive_handle_extract(handle, options)`
: Extract using `ost_extract_options`. `output_dir` must be set by the caller.

`ost_binhex_decode(data, size, out)`
: Decodes a BinHex payload. Release `out` with `ost_binhex_free`.

`ost_normalize_utf8(input, mode, output)`
: Allocates a normalized UTF-8 string. Release it with `free`.

## Ownership

Caller-owned buffers:

- `ost_read_file` fills `ost_buffer`; release it with `ost_buffer_free`.
- `ost_archive_parse` fills `ost_archive`; release it with `ost_archive_free`.
- `ost_binhex_decode` fills `ost_binhex_file`; release it with `ost_binhex_free`.
- `ost_macroman_to_utf8` and `ost_normalize_utf8` return heap strings; release with `free`.

Input pointers are borrowed unless the function documentation or struct ownership says otherwise.

## Thread Safety

Independent archives and buffers may be used concurrently by different threads.

`ost_archive_parse_with_options` and `ost_archive_handle_*` are thread-friendly when each thread uses independent input buffers and handles.

Legacy limitation: `ost_archive_set_unicode_normalization` changes process-global parser state. Do not change that setting concurrently with `ost_archive_parse`. Prefer `ost_parse_options` instead.

## ABI Version Policy

`PROJECT_VERSION` tracks source release version. `ABI_VERSION` tracks shared library ABI compatibility.

Increase `ABI_VERSION` when:

- removing or renaming an exported function
- changing public struct layout incompatibly
- changing enum values in a way that breaks existing binaries
- changing ownership rules or required caller allocation contracts

Do not increase `ABI_VERSION` for additive compatible changes such as adding a new exported function or enum value, as long as existing layout and behavior remain compatible.

## Release Checklist

Before release:

```sh
make test
make test-symbols
make test-install
make test-examples
make test-corpus-matrix
make distcheck
make distcheck-tarball
```

## Examples

`examples/list_archive.c` is built and run by `make test-examples`. It intentionally includes only `<openstuffit/openstuffit.h>`.

## Corpus Testing

`make test-corpus-matrix` runs representative checked-in `.sit`, `.sea`, `.hqx`, and unsupported `.sitx` cases from `tests/fixtures/corpus_matrix.tsv`.

Set `OPENSTUFFIT_CORPUS_DIR=/path/to/files` to add local real-world archives to the same smoke pass.
