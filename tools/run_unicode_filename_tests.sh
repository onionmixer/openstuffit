#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}

if [ ! -x "$bin" ]; then
    echo "missing executable: $bin" >&2
    exit 1
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/openstuffit_unicode.XXXXXX")
cleanup() {
    rm -rf "$tmpdir"
}
trap cleanup EXIT

fixture="$tmpdir/macroman_unicode.sit"
python3 - "$fixture" <<'PY'
import struct
import sys

path = sys.argv[1]
archive_len = 22 + 112
header = bytearray()
header += b"SIT!"
header += struct.pack(">H", 1)
header += struct.pack(">I", archive_len)
header += b"rLau"
header += b"\x01"
header += b"\x00" * 7

entry = bytearray(112)
entry[0] = 0
entry[1] = 0
name = b"Caf\x8e"  # MacRoman e-acute, decoded as U+00E9.
entry[2] = len(name)
entry[3:3 + len(name)] = name
entry[66:70] = b"TEXT"
entry[70:74] = b"ttxt"
entry[74:76] = struct.pack(">H", 0)

with open(path, "wb") as f:
    f.write(header)
    f.write(entry)
PY

check_list_name() {
    mode=$1
    expected_hex=$2
    json="$tmpdir/list_$mode.json"
    "$bin" list --json --unicode-normalization "$mode" "$fixture" > "$json"
    python3 - "$json" "$expected_hex" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    doc = json.load(f)
name = doc["entries"][0]["path"]
actual = name.encode("utf-8").hex()
expected = sys.argv[2]
if actual != expected:
    raise SystemExit(f"name bytes mismatch: expected={expected} actual={actual} name={name!r}")
PY
}

check_extract_name() {
    mode=$1
    expected_hex=$2
    out="$tmpdir/extract_$mode"
    "$bin" extract --overwrite --unicode-normalization "$mode" -o "$out" "$fixture" >/dev/null
    python3 - "$out" "$expected_hex" <<'PY'
import os
import sys

entries = os.listdir(sys.argv[1])
if len(entries) != 1:
    raise SystemExit(f"unexpected output entries: {entries!r}")
actual = entries[0].encode("utf-8").hex()
expected = sys.argv[2]
if actual != expected:
    raise SystemExit(f"filename bytes mismatch: expected={expected} actual={actual} name={entries[0]!r}")
PY
}

nfc_hex=436166c3a9
nfd_hex=43616665cc81

check_list_name none "$nfc_hex"
check_list_name nfc "$nfc_hex"
check_list_name nfd "$nfd_hex"
check_extract_name none "$nfc_hex"
check_extract_name nfc "$nfc_hex"
check_extract_name nfd "$nfd_hex"

echo "unicode filename tests ok"
