#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
tmp="${TMPDIR:-/tmp}/openstuffit_json_schema_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if [ ! -x "$bin" ]; then
    echo "missing binary: $bin" >&2
    exit 1
fi

mkdir -p "$tmp"
classic=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
sea_bin=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin
method14="$tmp/method14_rsrc.sit"
python3 tools/gen_sit5_method14_fixture.py rsrc "$method14"

"$bin" identify --json --show-forks "$sea_bin" >"$tmp/identify.json"
"$bin" list --json "$method14" >"$tmp/list.json"
"$bin" dump --json --entry method14_rsrc.txt "$method14" >"$tmp/dump_entry.json"
"$bin" dump --json --hex 0:4 "$classic" >"$tmp/dump_hex.json"

python3 - "$tmp/identify.json" "$tmp/list.json" "$tmp/dump_entry.json" "$tmp/dump_hex.json" <<'PY'
import json
import sys

identify = json.load(open(sys.argv[1], "r", encoding="utf-8"))
listing = json.load(open(sys.argv[2], "r", encoding="utf-8"))
entry = json.load(open(sys.argv[3], "r", encoding="utf-8"))
hex_dump = json.load(open(sys.argv[4], "r", encoding="utf-8"))

assert identify["status"] == "ok"
assert identify["wrapper"] == "macbinary"
assert identify["format"] == "sit-classic"
assert identify["forks"]["data"]["present"] is True
assert identify["forks"]["resource"]["present"] is True
assert identify["finder"]["type"] == "APPL"
assert identify["finder"]["creator"] == "aust"

assert listing["format"] == "sit5"
assert listing["wrapper"] == "raw"
assert len(listing["entries"]) == 1
listed = listing["entries"][0]
assert listed["path"] == "method14_rsrc.txt"
assert listed["kind"] == "file"
assert listed["data_fork"]["method_name"] == "none"
assert listed["resource_fork"]["present"] is True
assert listed["resource_fork"]["method"] == 14
assert listed["resource_fork"]["method_name"] == "deflate"

assert entry["path"] == "method14_rsrc.txt"
assert entry["data_fork"]["present"] is True
assert entry["resource_fork"]["present"] is True
assert entry["resource_fork"]["method_name"] == "deflate"
assert entry["resource_fork"]["compressed_size"] > 0
assert entry["resource_fork"]["size"] > entry["resource_fork"]["compressed_size"]

assert hex_dump["offset"] == 0
assert hex_dump["length"] == 4
assert hex_dump["hex"] == "53495421"
print("json schema tests ok")
PY
