#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
tmp="${TMPDIR:-/tmp}/openstuffit_large_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if [ ! -x "$bin" ]; then
    echo "missing binary: $bin" >&2
    exit 1
fi

mkdir -p "$tmp"

python3 - "$tmp/large_store.sit" "$tmp/large_store.bin" "$tmp/large_deflate.sit" "$tmp/large_deflate.bin" <<'PY'
import struct
import sys
import zlib

store_archive, store_plain_path, deflate_archive, deflate_plain_path = sys.argv[1:5]

def crc16_ibm(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc

def classic_store(path, name, payload):
    archive_len = 22 + 112 + len(payload)
    archive = bytearray()
    archive += b"SIT!"
    archive += struct.pack(">H", 1)
    archive += struct.pack(">I", archive_len)
    archive += b"rLau"
    archive += bytes([2, 0])
    archive += b"\0" * 6
    entry = bytearray(112)
    entry[1] = 0
    entry[2] = len(name)
    entry[3:3 + len(name)] = name
    entry[66:70] = b"BINA"
    entry[70:74] = b"ttxt"
    entry[88:92] = struct.pack(">I", len(payload))
    entry[96:100] = struct.pack(">I", len(payload))
    entry[102:104] = struct.pack(">H", crc16_ibm(payload))
    archive += entry
    archive += payload
    open(path, "wb").write(archive)

store_payload = bytes((i * 37 + 11) & 0xFF for i in range(1024 * 1024))
classic_store(store_archive, b"large_store.bin", store_payload)
open(store_plain_path, "wb").write(store_payload)

deflate_payload = (b"Large method 14 payload block.\n" * 4096) + bytes(range(256)) * 128
co = zlib.compressobj(level=6, wbits=-15)
compressed = co.compress(deflate_payload) + co.flush()

def u16(v):
    return struct.pack(">H", v)

def u32(v):
    return struct.pack(">I", v)

data = bytearray()
sig = b"StuffIt (c)1997-2002 Aladdin Systems, Inc., http://www.aladdinsys.com/StuffIt/\r\n"
data += sig[:80].ljust(80, b"\0")
data += b"\x1a\x00\x05\x10"
total_pos = len(data)
data += b"\0" * 4
data += u32(114)
data += u16(1)
data += u32(114)
data += u16(0x009b)
data += b"\xa5\xa5"
data += b"Kestrel Sit5 Archive"
del data[114:]

name = b"large_method14.bin"
entry = bytearray()
entry += u32(0xA5A5A5A5)
entry += b"\x01\x00\0\0\x00\x00"
entry += u32(0xD256A35A)
entry += u32(0xD256A35A)
entry += u32(0)
entry += u32(0)
entry += u32(0)
entry += u16(len(name))
entry += u16(0)
entry += u32(len(deflate_payload))
entry += u32(len(compressed))
entry += u16(crc16_ibm(deflate_payload))
entry += u16(0)
entry += b"\x0e\x00"
entry += name
entry[6:8] = u16(len(entry))
entry[32:34] = u16(crc16_ibm(entry[:32]))
data += entry
data += u16(0)
data += u16(0)
data += b"BINA"
data += b"ttxt"
data += u16(0)
data += b"\0" * 22
data += compressed
data[total_pos:total_pos + 4] = u32(len(data))
open(deflate_archive, "wb").write(data)
open(deflate_plain_path, "wb").write(deflate_payload)
PY

"$bin" list --json "$tmp/large_store.sit" >"$tmp/store.json"
grep -q '"path":"large_store.bin"' "$tmp/store.json"
grep -q '"size":1048576' "$tmp/store.json"
"$bin" extract --overwrite -o "$tmp/store_out" "$tmp/large_store.sit" >/dev/null
cmp -s "$tmp/store_out/large_store.bin" "$tmp/large_store.bin"

"$bin" list --json "$tmp/large_deflate.sit" >"$tmp/deflate.json"
grep -q '"path":"large_method14.bin"' "$tmp/deflate.json"
grep -q '"method":14' "$tmp/deflate.json"
grep -q '"method_name":"deflate"' "$tmp/deflate.json"
"$bin" extract --overwrite -o "$tmp/deflate_out" "$tmp/large_deflate.sit" >/dev/null
cmp -s "$tmp/deflate_out/large_method14.bin" "$tmp/large_deflate.bin"

echo "large fixture tests ok"
