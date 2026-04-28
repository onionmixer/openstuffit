#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
tmp="${TMPDIR:-/tmp}/openstuffit_generated_methods_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if [ ! -x "$bin" ]; then
    echo "missing executable: $bin" >&2
    exit 1
fi

make -C reference_repos/sit sit CFLAGS=-D_GNU_SOURCE >/dev/null

mkdir -p "$tmp/src"
printf 'This is a method 2 LZW fixture. This is a method 2 LZW fixture. This is a method 2 LZW fixture.\n' >"$tmp/src/method2.txt"

reference_repos/sit/sit -o "$tmp/method2.sit" "$tmp/src/method2.txt" >/dev/null

"$bin" list --json "$tmp/method2.sit" >"$tmp/method2.json"
grep -q '"format":"sit-classic"' "$tmp/method2.json"
grep -q '"method":2' "$tmp/method2.json"
grep -q '"method_name":"compress"' "$tmp/method2.json"

"$bin" extract --overwrite -o "$tmp/out" "$tmp/method2.sit" >/dev/null
cmp -s "$tmp/out/method2.txt" "$tmp/src/method2.txt"

python3 - "$tmp/method3.sit" <<'PY'
import struct
import sys

path = sys.argv[1]
name = b"method3.txt"
compressed = bytes.fromhex("50684a")
plain = b"ABAB"

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

archive_len = 22 + 112 + len(compressed)
archive = bytearray()
archive += b"SIT!"
archive += struct.pack(">H", 1)
archive += struct.pack(">I", archive_len)
archive += b"rLau"
archive += bytes([2, 0])
archive += b"\0" * 6

entry = bytearray(112)
entry[0] = 0
entry[1] = 3
entry[2] = len(name)
entry[3:3 + len(name)] = name
entry[66:70] = b"TEXT"
entry[70:74] = b"ttxt"
entry[84:88] = struct.pack(">I", 0)
entry[88:92] = struct.pack(">I", len(plain))
entry[92:96] = struct.pack(">I", 0)
entry[96:100] = struct.pack(">I", len(compressed))
entry[100:102] = struct.pack(">H", 0)
entry[102:104] = struct.pack(">H", crc16_ibm(plain))
archive += entry
archive += compressed

with open(path, "wb") as fp:
    fp.write(archive)
PY

"$bin" list --json "$tmp/method3.sit" >"$tmp/method3.json"
grep -q '"format":"sit-classic"' "$tmp/method3.json"
grep -q '"method":3' "$tmp/method3.json"
grep -q '"method_name":"huffman"' "$tmp/method3.json"

"$bin" extract --overwrite -o "$tmp/out3" "$tmp/method3.sit" >/dev/null
printf 'ABAB' | cmp -s - "$tmp/out3/method3.txt"

python3 tools/gen_sit5_method14_fixture.py flat "$tmp/method14.sit" --plain-out "$tmp/method14.txt"

"$bin" list --json "$tmp/method14.sit" >"$tmp/method14.json"
grep -q '"format":"sit5"' "$tmp/method14.json"
grep -q '"method":14' "$tmp/method14.json"
grep -q '"method_name":"deflate"' "$tmp/method14.json"
"$bin" dump --entry method14.txt "$tmp/method14.sit" >"$tmp/method14_dump.txt"
grep -q 'data fork: present=yes .* method=14/deflate' "$tmp/method14_dump.txt"
"$bin" dump --json --entry method14.txt "$tmp/method14.sit" >"$tmp/method14_dump.json"
grep -q '"data_fork":{"present":true' "$tmp/method14_dump.json"
grep -q '"method":14,"method_name":"deflate"' "$tmp/method14_dump.json"

"$bin" extract --overwrite -o "$tmp/out14" "$tmp/method14.sit" >/dev/null
cmp -s "$tmp/out14/method14.txt" "$tmp/method14.txt"

python3 tools/gen_sit5_method14_fixture.py nested "$tmp/method14_nested.sit" --plain-out "$tmp/method14_nested.txt"

"$bin" list --json "$tmp/method14_nested.sit" >"$tmp/method14_nested.json"
grep -q '"format":"sit5"' "$tmp/method14_nested.json"
grep -q '"path":"folder","kind":"directory"' "$tmp/method14_nested.json"
grep -q '"path":"folder/method14_nested.txt","kind":"file"' "$tmp/method14_nested.json"
grep -q '"method":14' "$tmp/method14_nested.json"
grep -q '"method_name":"deflate"' "$tmp/method14_nested.json"

"$bin" extract --overwrite -o "$tmp/out14_nested" "$tmp/method14_nested.sit" >/dev/null
cmp -s "$tmp/out14_nested/folder/method14_nested.txt" "$tmp/method14_nested.txt"

python3 tools/gen_sit5_method14_fixture.py dir_sentinel "$tmp/dir_sentinel.sit"

timeout 10 "$bin" list --json "$tmp/dir_sentinel.sit" >"$tmp/dir_sentinel.json"
grep -q '"format":"sit5"' "$tmp/dir_sentinel.json"
grep -q '"path":"dir","kind":"directory"' "$tmp/dir_sentinel.json"
grep -q '"path":"dir/a.txt","kind":"file"' "$tmp/dir_sentinel.json"
grep -q '"path":"dir/b.txt","kind":"file"' "$tmp/dir_sentinel.json"

timeout 10 "$bin" extract --overwrite -o "$tmp/out_dir_sentinel" "$tmp/dir_sentinel.sit" >/dev/null
printf 'alpha\n' | cmp -s - "$tmp/out_dir_sentinel/dir/a.txt"
printf 'beta\n' | cmp -s - "$tmp/out_dir_sentinel/dir/b.txt"

python3 tools/gen_sit5_method14_fixture.py rsrc "$tmp/method14_rsrc.sit" --plain-out "$tmp/method14_rsrc_data.txt" --rsrc-out "$tmp/method14_rsrc.bin"

"$bin" list --json "$tmp/method14_rsrc.sit" >"$tmp/method14_rsrc.json"
grep -q '"format":"sit5"' "$tmp/method14_rsrc.json"
grep -q '"path":"method14_rsrc.txt","kind":"file"' "$tmp/method14_rsrc.json"
grep -q '"resource_fork":{"present":true' "$tmp/method14_rsrc.json"
grep -q '"method":14' "$tmp/method14_rsrc.json"
grep -q '"method_name":"deflate"' "$tmp/method14_rsrc.json"
"$bin" dump --entry method14_rsrc.txt "$tmp/method14_rsrc.sit" >"$tmp/method14_rsrc_dump.txt"
grep -q 'data fork: present=yes .* method=0/none' "$tmp/method14_rsrc_dump.txt"
grep -q 'resource fork: present=yes .* method=14/deflate' "$tmp/method14_rsrc_dump.txt"
"$bin" dump --json --entry method14_rsrc.txt "$tmp/method14_rsrc.sit" >"$tmp/method14_rsrc_dump.json"
grep -q '"resource_fork":{"present":true' "$tmp/method14_rsrc_dump.json"
grep -q '"method":14,"method_name":"deflate"' "$tmp/method14_rsrc_dump.json"

"$bin" extract --overwrite --forks rsrc -o "$tmp/out14_rsrc" "$tmp/method14_rsrc.sit" >/dev/null
cmp -s "$tmp/out14_rsrc/method14_rsrc.txt" "$tmp/method14_rsrc_data.txt"
cmp -s "$tmp/out14_rsrc/method14_rsrc.txt.rsrc" "$tmp/method14_rsrc.bin"

"$bin" extract --overwrite --forks appledouble -o "$tmp/out14_ad" "$tmp/method14_rsrc.sit" >/dev/null
cmp -s "$tmp/out14_ad/method14_rsrc.txt" "$tmp/method14_rsrc_data.txt"
test -f "$tmp/out14_ad/._method14_rsrc.txt"
test ! -f "$tmp/out14_ad/method14_rsrc.txt.rsrc"
dd if="$tmp/out14_ad/._method14_rsrc.txt" of="$tmp/out14_ad_rsrc.bin" bs=1 skip=82 2>/dev/null
cmp -s "$tmp/out14_ad_rsrc.bin" "$tmp/method14_rsrc.bin"
python3 - "$tmp/out14_ad/._method14_rsrc.txt" "$tmp/method14_rsrc.bin" <<'PY'
import struct
import sys

sidecar, expected_rsrc = sys.argv[1:3]
data = open(sidecar, "rb").read()
rsrc = open(expected_rsrc, "rb").read()
assert struct.unpack(">I", data[0:4])[0] == 0x00051607
assert struct.unpack(">I", data[4:8])[0] == 0x00020000
assert struct.unpack(">H", data[24:26])[0] == 2
entry1 = struct.unpack(">III", data[26:38])
entry2 = struct.unpack(">III", data[38:50])
assert entry1 == (9, 50, 32)
assert entry2 == (2, 82, len(rsrc))
finder = data[50:82]
assert finder[0:4] == b"TEXT"
assert finder[4:8] == b"ttxt"
assert finder[8:10] == b"\0\0"
assert data[82:] == rsrc
PY

"$bin" extract --overwrite --forks both -o "$tmp/out14_both" "$tmp/method14_rsrc.sit" >/dev/null
cmp -s "$tmp/out14_both/method14_rsrc.txt" "$tmp/method14_rsrc_data.txt"
cmp -s "$tmp/out14_both/method14_rsrc.txt.rsrc" "$tmp/method14_rsrc.bin"
test -f "$tmp/out14_both/._method14_rsrc.txt"
dd if="$tmp/out14_both/._method14_rsrc.txt" of="$tmp/out14_both_ad_rsrc.bin" bs=1 skip=82 2>/dev/null
cmp -s "$tmp/out14_both_ad_rsrc.bin" "$tmp/method14_rsrc.bin"
cmp -s "$tmp/out14_both_ad_rsrc.bin" "$tmp/out14_both/method14_rsrc.txt.rsrc"
python3 - "$tmp/out14_both/._method14_rsrc.txt" "$tmp/method14_rsrc.bin" <<'PY'
import struct
import sys

sidecar, expected_rsrc = sys.argv[1:3]
data = open(sidecar, "rb").read()
rsrc = open(expected_rsrc, "rb").read()
assert struct.unpack(">I", data[0:4])[0] == 0x00051607
assert struct.unpack(">I", data[4:8])[0] == 0x00020000
assert struct.unpack(">H", data[24:26])[0] == 2
assert struct.unpack(">III", data[26:38]) == (9, 50, 32)
assert struct.unpack(">III", data[38:50]) == (2, 82, len(rsrc))
assert data[50:54] == b"TEXT"
assert data[54:58] == b"ttxt"
assert data[58:60] == b"\0\0"
assert data[82:] == rsrc
PY

echo "generated method fixtures ok: method2=lzw method3=huffman method14=deflate method14_nested=deflate method14_dir_sentinel=sit5 method14_rsrc=deflate"
