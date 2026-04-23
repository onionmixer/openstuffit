# SPEC: Mac Wrappers and Metadata

Status: implemented subset  
Implementation: `src/ost_detect.c`, `src/ost_binhex.c`, `src/ost_extract.c`
Public API: `ost_detect_buffer`, `ost_binhex_decode`, `ost_archive_handle_open_file` in `document/API_OPENSTUFFIT.md`

## Scope

This document covers wrapper formats used to transport classic Macintosh forked files around StuffIt archives:

- MacBinary
- AppleSingle / AppleDouble
- BinHex 4.0
- Finder metadata sidecar output

## MacBinary

MacBinary detection is heuristic but follows common structural checks.

| Offset | Size | Field | Notes |
| --- | ---: | --- | --- |
| 0 | 1 | zero | must be 0 |
| 1 | 1 | filename length | 1..63 |
| 2 | 63 | filename | MacRoman, currently not exposed |
| 65 | 4 | Finder type | copied to detection |
| 69 | 4 | Finder creator | copied to detection |
| 74 | 1 | zero | must be 0 |
| 82 | 1 | zero | must be 0 |
| 83 | 4 | data fork length | big-endian |
| 87 | 4 | resource fork length | big-endian |
| 102 | 4 | signature | `mBIN` for MacBinary III candidate |
| 124 | 2 | header CRC | CCITT, used to infer MacBinary II |

The data fork begins at offset 128. Resource fork begins after the data fork rounded up to a 128-byte boundary.

Example hexdump from
`reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin`:

```text
000000 00 0b 73 6f 75 72 63 65 73 2e 73 65 61 00 00 00
000040 00 41 50 50 4c 61 75 73 74 21 00 00 40 00 81 00
000050 00 00 00 00 00 0a f4 00 00 62 02 e0 07 81 b9 e0
000080 53 49 54 21 00 06 00 00 0a f4 72 4c 61 75 02 ec
```

Key decoded values:

| Offset | Bytes | Decoded value |
| ---: | --- | --- |
| `1` | `0b` | filename length 11 |
| `2..12` | `73 6f...61` | `sources.sea` |
| `65..68` | `41 50 50 4c` | Finder type `APPL` |
| `69..72` | `61 75 73 74` | Finder creator `aust` |
| `83..86` | `00 00 0a f4` | data fork length `2804` |
| `87..90` | `00 00 62 02` | resource fork length `25090` |
| `128..` | `53 49 54 21` | data fork starts with `SIT!` |

`dump --forks` cross-check for the same fixture:

```text
Wrapper fork map:
  wrapper: macbinary
  format: sit-classic
  payload: offset=128 size=2804
  data fork: present=yes offset=128 size=2804
  resource fork: present=yes offset=2944 size=25090
  finder: type=APPL creator=aust
  macbinary version: 1
```

## AppleSingle / AppleDouble

Supported magic values:

| Magic | Meaning |
| ---: | --- |
| `0x00051600` | AppleSingle big-endian |
| `0x00051607` | AppleDouble big-endian |
| `0x00160500` | AppleSingle little-endian variant |
| `0x07160500` | AppleDouble little-endian variant |

Important entry IDs:

| ID | Meaning |
| ---: | --- |
| 1 | data fork |
| 2 | resource fork |
| 3 | real name |
| 4 | comment |
| 8 | dates |
| 9 | Finder info |

`openstuffit` currently uses data fork entry 1 for payload discovery and records resource fork presence for reporting.

AppleSingle example hexdump from
`reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.AS`:

```text
000000 00 05 16 00 00 02 00 00 00 00 00 00 00 00 00 00
000010 00 00 00 00 00 00 00 00 00 07 00 00 00 08 00 00
000020 00 6e 00 00 00 10 00 00 00 09 00 00 00 7e 00 00
000030 00 20 00 00 00 0a 00 00 00 9e 00 00 00 04 00 00
000040 00 03 00 00 00 a2 00 00 00 0b 00 00 00 04 00 00
000050 00 ad 00 00 00 00 00 00 00 02 00 00 00 ad 00 00
```

Interpretation:

| Offset | Bytes | Meaning |
| ---: | --- | --- |
| `0..3` | `00 05 16 00` | AppleSingle magic |
| `4..7` | `00 02 00 00` | version |
| `24..25` | `00 07` | seven entry descriptors |
| `26..37` | `00 00 00 08 ...` | entry id 8, dates |
| `38..49` | `00 00 00 09 ...` | entry id 9, Finder info |
| `86..95` | `00 00 00 02 ...` | entry id 2, resource fork |

## BinHex 4.0

Recognition requires the text marker:

```text
(This file must be converted with BinHex
```

The encoded stream starts at a colon after the marker line. The alphabet is:

```text
!"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr
```

Decoded bytes are RLE-expanded using `0x90` as the repeat marker.

Decoded layout:

| Field | Size |
| --- | ---: |
| name length | 1 |
| name bytes | variable |
| version | 1 |
| Finder type | 4 |
| Finder creator | 4 |
| Finder flags | 2 |
| data fork length | 4 |
| resource fork length | 4 |
| header CRC16 | 2 |
| data fork | variable |
| data CRC16 | 2 |
| resource fork | variable |
| resource CRC16 | 2 |

CRC16 is CCITT and is enforced for header, data fork, and resource fork.

BinHex SEA example:

```text
(This file must be converted with BinHex 4.0)
:#h0[GA*MCA-ZFf9K!%&38%aKGA0d)!#3!`Vd!!"KfYep8dP8)3!'!!!+p(*-BA8
```

## Finder Metadata Output

Extraction can write Finder metadata sidecars:

```bash
openstuffit extract --finder sidecar -o out archive.sit
```

Sidecar path:

```text
<extracted-path>.finder.json
```

Sidecar schema:

```json
{
  "path": "testfile.txt",
  "finder": {"type": "TEXT", "creator": "ttxt", "flags": "0100"},
  "mac_time": {"create": 0, "modify": 0}
}
```

The default is `--finder skip`, preserving prior extraction behavior.

## Resource Fork Output

Current resource fork modes:

| Mode | Status |
| --- | --- |
| `skip` | supported, default |
| `rsrc` | supported as `<path>.rsrc` |
| `appledouble` | supported as sibling `._filename` |
| `native` | writes macOS `..namedfork/rsrc`; returns unsupported on non-macOS |
| `both` | supported as `<path>.rsrc` plus sibling `._filename` |

The generated AppleDouble sidecar uses magic `0x00051607`, version
`0x00020000`, Finder Info entry id `9`, and resource fork entry id `2`.
The Finder Info payload is 32 bytes: file type, creator, Finder flags, then
zero-filled fields. The resource fork payload starts at offset `82`.

Generated AppleDouble layout:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `0x00051607` |
| 4 | 4 | version `0x00020000` |
| 24 | 2 | entry count `2` |
| 26 | 12 | Finder Info descriptor: id `9`, offset `50`, size `32` |
| 38 | 12 | resource fork descriptor: id `2`, offset `82`, size `<rsrc>` |
| 50 | 32 | Finder Info payload |
| 82 | variable | resource fork payload |

Covered tests:

| Behavior | Test |
| --- | --- |
| MacBinary payload and fork offsets | `make test-fixtures`, `make test-identify-all` |
| AppleSingle payload delegation | `make test-fixtures` |
| BinHex CRC and payload delegation | `make test-fixtures`, `make test-corrupt` |
| Finder JSON sidecar | `make test-cli` |
| `.rsrc`, AppleDouble, both fork output | `make test-cli`, generated method 14 resource-fork checks in `make test-generated-method-fixtures` |
| AppleDouble magic/version/descriptors/Finder payload | generated method 14 AppleDouble parser assertions in `make test-generated-method-fixtures` |
| unsafe path rejection | extract path sanitizer in `src/ost_extract.c` | `make test-path-safety` |
| native resource fork output | `make test-native-forks` |

Implementation map:

| Behavior | Implementation |
| --- | --- |
| MacBinary detection | `src/ost_detect.c` |
| AppleSingle/AppleDouble detection | `src/ost_detect.c` |
| BinHex decode/RLE/CRC | `src/ost_binhex.c` |
| Finder sidecar and AppleDouble/native output | `src/ost_extract.c` |
