# SPEC: StuffIt SEA Handling

Status: implemented subset  
Implementation: `src/ost_detect.c`, `src/openstuffit.c`
Public API: `ost_detect_buffer`, `ost_archive_handle_open_file`, `ost_archive_handle_open_buffer` in `document/API_OPENSTUFFIT.md`

## Scope

`.sea` is treated as a self-extracting wrapper class, not as an independent compression format. For StuffIt SEA files, the useful archive payload is normally a StuffIt archive in the data fork. The executable extraction code may live in a resource fork or executable wrapper.

`openstuffit` never executes SEA code. It locates a StuffIt payload and passes it to the SIT parser.

## Supported SEA Forms

| Form | Detection | Status |
| --- | --- | --- |
| raw SEA data fork | payload starts with `SIT!` or StuffIt 5 header | supported |
| MacBinary SEA | MacBinary data fork contains StuffIt payload | supported |
| AppleSingle SEA | entry id 1 data fork contains StuffIt payload | supported |
| BinHex SEA | BinHex data fork contains StuffIt payload | supported |

Wrapper examples:

| Fixture | Wrapper | Test |
| --- | --- | --- |
| `testfile.stuffit45_dlx.mac9.sea` | raw data fork starts with `SIT!` | `make test-fixtures` |
| `testfile.stuffit45_dlx.mac9.sea.bin` | MacBinary application wrapper | `make test-fixtures`, `make test-xad-compare` |
| `testfile.stuffit45_dlx.mac9.sea.AS` | AppleSingle | `make test-fixtures` |
| `testfile.stuffit45_dlx.mac9.sea.hqx` | BinHex 4.0 | `make test-fixtures` |

## Payload Discovery

The current discovery order is:

1. Raw `SIT!` classic signature at offset 0.
2. Raw StuffIt 5 text header at offset 0.
3. Raw StuffIt X signature at offset 0, unsupported.
4. MacBinary wrapper, then inspect data fork offset 128.
5. AppleSingle/AppleDouble wrapper, then inspect data fork entry.
6. BinHex marker detection, decode data fork, then inspect decoded data fork.

## MacBinary SEA

For MacBinary SEA, Finder type/creator are useful wrapper metadata.

Example fixture:

- `reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin`

Observed values:

| Field | Value |
| --- | --- |
| wrapper | `macbinary` |
| Finder type | `APPL` |
| Finder creator | `aust` |
| data fork offset | `128` |
| data fork format | classic `SIT!` |

MacBinary SEA hexdump:

```text
000000 00 0b 73 6f 75 72 63 65 73 2e 73 65 61 00 00 00
000040 00 41 50 50 4c 61 75 73 74 21 00 00 40 00 81 00
000050 00 00 00 00 00 0a f4 00 00 62 02 e0 07 81 b9 e0
000080 53 49 54 21 00 06 00 00 0a f4 72 4c 61 75 02 ec
```

Interpretation:

| Offset | Bytes | Meaning |
| ---: | --- | --- |
| `1` | `0b` | MacBinary filename length 11 |
| `2..12` | `sources.sea` | wrapper filename |
| `65..68` | `41 50 50 4c` | Finder type `APPL` |
| `69..72` | `61 75 73 74` | Finder creator `aust` |
| `83..86` | `00 00 0a f4` | data fork length `2804` |
| `87..90` | `00 00 62 02` | resource fork length `25090` |
| `128..` | `53 49 54 21` | internal classic StuffIt payload |

## BinHex SEA

BinHex `.sea.hqx` files are decoded as BinHex 4.0. `openstuffit` validates BinHex header/data/resource CRC16 before passing the decoded data fork to the SIT parser.

Example fixture:

- `reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.hqx`

Beginning of the fixture:

```text
(This file must be converted with BinHex 4.0)
:#h0[GA*MCA-ZFf9K!%&38%aKGA0d)!#3!`Vd!!"KfYep8dP8)3!'!!!+p(*-BA8
```

## Extraction Semantics

SEA executable/resource code is not run or reconstructed as an application. Extraction returns the entries inside the StuffIt archive payload. Wrapper resource forks are reported by `identify --show-forks` where available, but payload extraction focuses on the internal archive.

## Fixture Coverage

Covered tests:

- raw `.sea`: `testfile.stuffit45_dlx.mac9.sea`
- MacBinary `.sea.bin`: `testfile.stuffit45_dlx.mac9.sea.bin`
- AppleSingle `.sea.AS`: `testfile.stuffit45_dlx.mac9.sea.AS`
- BinHex `.sea.hqx`: `testfile.stuffit45_dlx.mac9.sea.hqx`

Implementation map:

| Behavior | Implementation | Test |
| --- | --- | --- |
| raw SEA delegation | `ost_detect_buffer()` in `src/ost_detect.c` | `make test-fixtures` |
| MacBinary SEA payload offset | MacBinary branch in `src/ost_detect.c` | `make test-xad-compare` |
| BinHex SEA decode and CRC | `src/ost_binhex.c` | `make test-fixtures`, `make test-corrupt` |
