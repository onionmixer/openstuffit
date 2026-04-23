#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
tmp="${TMPDIR:-/tmp}/openstuffit_json_golden_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if [ ! -x "$bin" ]; then
    echo "missing binary: $bin" >&2
    exit 1
fi

mkdir -p "$tmp"
fixture="$tmp/method14_rsrc.sit"
python3 tools/gen_sit5_method14_fixture.py rsrc "$fixture"

"$bin" list --json "$fixture" >"$tmp/list.json"
diff -u tests/expected/list_generated_method14_rsrc.json "$tmp/list.json"

"$bin" dump --json --entry method14_rsrc.txt "$fixture" >"$tmp/dump.json"
diff -u tests/expected/dump_generated_method14_rsrc_entry.json "$tmp/dump.json"

echo "json golden tests ok"
