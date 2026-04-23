# SPEC: Classic StuffIt SIT!

Status: implemented subset  
Implementation: `src/ost_archive.c`, `src/ost_decompress.c`, `src/ost_extract.c`
Public API: `ost_archive_handle_open_file`, `ost_archive_parse_with_options`, `ost_archive_handle_extract` in `document/API_OPENSTUFFIT.md`

## Scope

This document covers classic StuffIt archives identified by the `SIT!` signature and the secondary `rLau` signature. It describes the subset implemented by `openstuffit`: listing, data/resource fork extraction, Finder metadata reporting, CRC16 verification, and compression methods 0, 1, 2, 3, and 13.

StuffIt X (`.sitx`) is out of scope.

## Archive Header

The classic archive header is 22 bytes at the payload offset.

| Offset | Size | Field | Notes |
| --- | ---: | --- | --- |
| 0 | 4 | signature | ASCII `SIT!` |
| 4 | 2 | entry count | big-endian |
| 6 | 4 | archive length | big-endian |
| 10 | 4 | signature2 | ASCII `rLau` |
| 14 | 1 | version | classic version byte |
| 15 | 7 | reserved | skipped |

Detection requires both `SIT!` at offset 0 and `rLau` at offset 10.

Example hexdump from
`reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit`:

```text
000000 53 49 54 21 00 06 00 00 0a f4 72 4c 61 75 02 ec
000010 00 00 00 16 d8 4a
```

Interpretation:

| Bytes | Meaning |
| --- | --- |
| `53 49 54 21` | `SIT!` signature |
| `00 06` | six archive entries |
| `00 00 0a f4` | archive length `2804` |
| `72 4c 61 75` | `rLau` secondary signature |
| `02` | classic version byte observed in fixture |

## Entry Header

Each classic entry header is 112 bytes. Fork payload data follows the header.

| Offset | Size | Field | Notes |
| --- | ---: | --- | --- |
| 0 | 1 | resource method | high bit may indicate encryption |
| 1 | 1 | data method | high bit may indicate encryption |
| 2 | 1 | name length | current implementation caps at 31 bytes |
| 3 | 31 | name bytes | MacRoman decoded to UTF-8 |
| 34 | 2 | name CRC | currently not enforced |
| 48 | 2 | child count | folder metadata candidate |
| 50 | 4 | previous offset | hierarchy metadata |
| 54 | 4 | next offset | hierarchy metadata |
| 58 | 4 | parent offset | hierarchy metadata |
| 62 | 4 | child offset | hierarchy metadata |
| 66 | 4 | Finder type | stored as raw fourcc |
| 70 | 4 | Finder creator | stored as raw fourcc |
| 74 | 2 | Finder flags | big-endian |
| 76 | 4 | creation time | Mac epoch raw value |
| 80 | 4 | modification time | Mac epoch raw value |
| 84 | 4 | resource uncompressed size | big-endian |
| 88 | 4 | data uncompressed size | big-endian |
| 92 | 4 | resource compressed size | big-endian |
| 96 | 4 | data compressed size | big-endian |
| 100 | 2 | resource CRC16 | IBM CRC16 over decompressed fork |
| 102 | 2 | data CRC16 | IBM CRC16 over decompressed fork |
| 110 | 2 | header CRC16 | currently not enforced |

Example first entry header from the same fixture starts at offset `0x16`:

```text
000016 d8 4a 0d 00 0a 54 65 73 74 20 49 6d 61 67 65 4a
000026 3d d4 db 04 3d ec e7 f4 00 00 00 00 00 00 00 00
000036 3d df a3 10 93 d1 09 86 00 00 00 00 00 00 00 00
000046 00 00 80 00 00 00 00 00 00 00 01 8f 00 00 00 00
000056 ff ff ff ff 3f 3f 3f 3f 3f 3f 3f 3f 05 00 e0 03
000066 3e 65 e0 03 3e 65 00 00 23 ae 00 00 00 00 00 00
000076 01 09 00 00 00 00 b0 7b 00 00 00 00 00 00 00 00
```

Key decoded values:

| Header offset | Bytes | Decoded value |
| ---: | --- | --- |
| `0` | `d8` | resource method raw; low bits method `13`, high bit encrypted flag not set in this fixture |
| `1` | `4a` | data method raw; no data fork for this entry |
| `2` | `0d` | name length 13 |
| `3..15` | `00 0a 54...65` | stored name bytes observed for `Test Image` |
| `66..69` | `3f 3f 3f 3f` | Finder type `????` |
| `70..73` | `3f 3f 3f 3f` | Finder creator `????` |
| `84..87` | `00 00 23 ae` | resource fork size `9134` |
| `92..95` | `00 00 01 09` | compressed resource size `265` |
| `100..101` | `b0 7b` | resource CRC16 |

Entry dump cross-check for `testfile.txt` in the same fixture:

```text
Entry:
  index: 5
  path: testfile.txt
  kind: file
  header offset: 2627
  finder: type=TEXT creator=ttxt flags=0x0100
  mac time: create=3061152000 modify=3758308646
  data fork: present=yes offset=2792 size=12 compressed=12 method=0/none crc=b6c1 encrypted=no
  resource fork: present=yes offset=2739 size=332 compressed=53 method=13/lz+huffman crc=0e18 encrypted=no
```

## Folder Markers

Classic folder entries are represented through method marker values.

| Value | Meaning |
| ---: | --- |
| 32 | start folder |
| 33 | end folder |

`openstuffit` maintains a folder path stack while listing and extracting.

## Compression Methods

| Method | Name | Status |
| ---: | --- | --- |
| 0 | store | supported |
| 1 | RLE90 | supported |
| 2 | Compress/LZW | supported, generated fixture coverage |
| 3 | Huffman | supported, synthetic unit and generated archive coverage |
| 13 | LZ+Huffman | supported, fixture coverage |
| 14 | Installer | unsupported |
| 15 | Arsenic/BWT | supported for SIT5; not observed in classic fixtures |

Encrypted forks without a password return password-required. Classic encrypted forks require the archive resource fork `MKey` plus the per-entry 16-byte trailer key. Raw `.sit` samples that lost the resource fork are therefore listed with encrypted metadata but remain unsupported even when `--password` is supplied. For MacBinary/AppleSingle samples that preserve the resource fork, `openstuffit` parses `MKey`, removes the 16-byte trailer key from the displayed compressed payload size, exposes `encryption=classic-des` plus `classic_padding` through `dump --entry`, and decrypts the fork payload with `--password`.

The implemented password block derivation processes the password in 8-byte blocks and does not process an extra empty block when the password length is exactly 8 bytes. This matches the observed StuffIt 4.5 password fixture where the documented password is `password`. XADMaster's current code path appears to reject that fixture with the documented password.

## Filename Handling

Name bytes are decoded as MacRoman and stored internally as UTF-8. After decoding, `openstuffit` applies Unicode normalization according to `--unicode-normalization`; default is NFC.

Path safety is checked after decoding and normalization. Absolute paths, `..`, backslash, and colon are rejected for extraction.

## Fixture Coverage

Primary fixture:

- `reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit`

Covered tests:

- `make test`
- `make test-fixtures`
- `make test-list-matrix`
- `make test-extract-matrix`
- `make test-xad-compare`
- `make test-generated-method-fixtures`
- `tools/check_extract_manifest.sh`
- `tests/expected/extract_stuffit45_mac9.tsv`

Implementation map:

| Behavior | Implementation | Test |
| --- | --- | --- |
| archive/header parsing | `parse_classic()` in `src/ost_archive.c` | `make test-fixtures` |
| method 2 generated fixture | `thecloudexpanse/sit` generated LZW archive | `make test-generated-method-fixtures` |
| method 3 generated fixture | minimal generated Huffman archive with known `ABAB` output | `make test-generated-method-fixtures` |
| classic encrypted extract | resource fork `MKey`, 16-byte entry trailer accounting, `dump --entry` encryption/padding reporting, modified DES/MKey payload decrypt | `make test-password` |
| method 13 extraction | `src/ost_sit13.c` via `src/ost_decompress.c` | `make test-extract-matrix` |
| data fork SHA256 parity with XADMaster | `tools/compare_xadmaster.sh` | `make test-xad-compare` |
| CRC mismatch error code `3` | `src/ost_extract.c` | `make test-cli`, `make test-corrupt` |
