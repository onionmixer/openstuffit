# SPEC: StuffIt 5 SIT

Status: implemented subset  
Implementation: `src/ost_archive.c`, `src/ost_sit15.c`, `src/ost_decompress.c`
Public API: `ost_archive_handle_open_file`, `ost_archive_parse_with_options`, `ost_archive_handle_extract` in `document/API_OPENSTUFFIT.md`

## Scope

This document covers StuffIt 5 style archives identified by the text header beginning with `StuffIt (c)1997-`. The implemented scope includes listing, directory hierarchy reconstruction, data fork extraction, resource fork metadata parsing, method 13, method 15 extraction, and password extraction for the observed SIT5 RC4 fixture.

StuffIt X (`StuffIt!` / `.sitx`) is detected but not extracted.

## Archive Header

The StuffIt 5 payload begins with an 80-byte text header.

| Offset | Size | Field | Notes |
| --- | ---: | --- | --- |
| 0 | 80 | text header | begins `StuffIt (c)1997-` |
| 82 | 1 | version | expected `5` in references |
| 83 | 1 | archive flags | retained for future use |
| 84 | 4 | total size | big-endian |
| 92 | 2 | root entry count | big-endian |
| 94 | 4 | first entry offset | big-endian, relative to payload |
| 98 | 2 | archive header CRC | currently not enforced |

Example hexdump from
`reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit`:

```text
000000 53 74 75 66 66 49 74 20 28 63 29 31 39 39 37 2d
000010 32 30 30 31 20 41 6c 61 64 64 69 6e 20 53 79 73
000020 74 65 6d 73 2c 20 49 6e 63 2e 2c 20 68 74 74 70
000030 3a 2f 2f 77 77 77 2e 61 6c 61 64 64 69 6e 73 79
000040 73 2e 63 6f 6d 2f 53 74 75 66 66 49 74 2f 0d 0a
000050 1a 00 05 00 00 00 03 1b 00 00 00 64 00 01 00 00
000060 00 64 f5 e1
```

Interpretation:

| Offset | Bytes | Decoded value |
| ---: | --- | --- |
| `0..79` | ASCII text | StuffIt 5 text header |
| `82` | `05` | version |
| `83` | `00` | archive flags |
| `84..87` | `00 00 03 1b` | total size `795` |
| `92..93` | `00 01` | one root entry |
| `94..97` | `00 00 00 64` | first entry block at offset `100` |

## Entry Block

Each entry block begins with `0xA5A5A5A5`.

| Offset | Size | Field | Notes |
| --- | ---: | --- | --- |
| 0 | 4 | entry id | `0xA5A5A5A5` |
| 4 | 1 | entry version | retained |
| 6 | 2 | header size | variable entry header length |
| 9 | 1 | flags | directory/encrypted/resource bits |
| 10 | 4 | creation time | Mac epoch raw value |
| 14 | 4 | modification time | Mac epoch raw value |
| 18 | 4 | previous offset | sibling metadata |
| 22 | 4 | next offset | sibling metadata |
| 26 | 4 | directory offset | parent directory offset |
| 30 | 2 | name length | bytes |
| 32 | 2 | header CRC | currently not enforced |
| 34 | 4 | data size | uncompressed size |
| 38 | 4 | compressed data size | compressed size |
| 42 | 2 | data CRC16 | not applicable for observed method 15 fixture |
| 46 | variable | method/passlen or child count | depends on directory flag |

Example first entry block at payload offset `0x64`:

```text
000064 a5 a5 a5 a5 03 00 00 37 00 40 dd 6a d2 e1 dd 6a
000074 d2 e1 00 00 00 00 00 00 00 00 00 00 00 00 00 07
000084 43 ce 00 00 00 bb 00 00 01 3f 00 00 02 60 00 03
000094 73 6f 75 72 63 65 73 00 00 95 54
```

Key decoded values:

| Entry offset | Bytes | Decoded value |
| ---: | --- | --- |
| `0..3` | `a5 a5 a5 a5` | entry marker |
| `4` | `03` | entry version |
| `6..7` | `00 37` | header size `55` |
| `9` | `40` | directory flag |
| `30..31` | `00 07` | name length 7 |
| `46..48` | `00 03 73` | child count/name payload area as parsed by implementation |

## Flags

| Flag | Meaning |
| ---: | --- |
| `0x40` | directory |
| `0x20` | encrypted |
| `0x10` | resource fork metadata present |

## Password Handling

SIT5 encrypted archives use a 5-byte archive password hash and 5-byte per-entry keys in the observed fixtures. `openstuffit` derives the archive key as the first five bytes of MD5(password), verifies it by comparing the first five bytes of MD5(archive key) with the stored archive hash, then decrypts each encrypted fork with RC4 using `archive_key || entry_key`.

Observed fixture:

- `reference_repos/stuffit-test-files/build/testfile.stuffit7.win.password.sit`

CLI behavior:

- missing password: exit `4` / `password-required`
- wrong password: exit `4` / `password-bad`
- `--password password`: extracts the observed method 15 data forks

## Compression Methods

| Method | Name | Status |
| ---: | --- | --- |
| 0 | store | supported |
| 13 | LZ+Huffman | supported |
| 14 | raw deflate | supported with generated fixture coverage |
| 15 | Arsenic/BWT | supported |

SIT5 method 14 is handled as a raw deflate stream through zlib `inflateInit2(..., -MAX_WBITS)`, matching `stuffit-rs` generated archives. No original method 14 archive appears in the current local listable fixture set, so coverage is generated rather than source-provenance fixture coverage. Classic StuffIt method 14 is still treated separately as the Installer-related method and is not mapped to SIT5 deflate unless the parser marked the fork as SIT5 deflate.

## Method 15 Notes

Method 15 is an arithmetic-coded BWT/MTF/RLE stream. The implementation is in `src/ost_sit15.c`.

Observed fixture:

- `reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit`

Its data fork CRC16 fields are `0000`; `openstuffit` therefore skips SIT CRC16 comparison for method 15 forks and validates through extract manifests.

Entry dump cross-check for `sources/testfile.txt`:

```text
Entry:
  index: 3
  path: sources/testfile.txt
  kind: file
  header offset: 629
  finder: type=...  creator=.... flags=0x0000
  mac time: create=3061170000 modify=3061170000
  data fork: present=yes offset=721 size=12 compressed=26 method=15/arsenic crc=0000 encrypted=no
  resource fork: present=no offset=721 size=0 compressed=0 method=0/none crc=0000 encrypted=no
```

## Filename Handling

Entry name bytes are decoded as MacRoman to UTF-8 and then normalized. Default output normalization is NFC. `--unicode-normalization nfd` can be used for macOS-oriented output.

## Fixture Coverage

Primary fixture:

- `reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit`

Covered tests:

- `tests/expected/extract_stuffit7_win.tsv`
- `tests/fixtures/list_matrix.tsv`
- `tests/fixtures/extract_matrix.tsv`
- `make test-list-matrix`
- `make test-extract-matrix`
- `make test-xad-compare`
- `make test-fixtures`
- `make test-password`

Implementation map:

| Behavior | Implementation | Test |
| --- | --- | --- |
| SIT5 header and entry walk | `parse_sit5()` in `src/ost_archive.c` | `make test-list-matrix` |
| directory path reconstruction | `offset_path` handling in `src/ost_archive.c` | `testfile.stuffit7.win.sit` matrix case, generated nested method 14 fixture |
| method 15 extraction | `src/ost_sit15.c` | `make test-extract-matrix` |
| SIT5 password RC4 extraction | `src/ost_crypto.c`, `ost_decompress_fork_with_password()` | `make test-password` |
| generated SIT5 method 14 fixtures | `tools/gen_sit5_method14_fixture.py` | flat, nested, resource-fork, and corrupt method 14 variants |
| SIT5 method 14 raw deflate | zlib raw inflate path in `src/ost_decompress.c` | `make test-generated-method-fixtures`, `make test-corrupt`, `make test-fuzz-smoke` |
| SIT5 method 14 dump output | `dump_entry()` in `src/ost_dump.c` | text/JSON `dump --entry` checks in `make test-generated-method-fixtures` |
| SIT5 method 14 resource fork | same method 14 inflate path via `resource_fork` descriptor | generated resource-fork method 14 fixture in `make test-generated-method-fixtures`; verifies `rsrc`, `appledouble`, and `both` output modes; corrupt stream/CRC coverage in `make test-corrupt`; resource-fork mutation coverage in `make test-fuzz-smoke` |
| larger generated SIT5 method 14 payload | same zlib raw inflate path | `make test-large-fixtures` |
| XADMaster data fork parity | `tools/compare_xadmaster.sh` | `make test-xad-compare` |
