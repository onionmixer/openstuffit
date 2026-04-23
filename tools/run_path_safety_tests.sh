#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
tmp="${TMPDIR:-/tmp}/openstuffit_path_safety_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if [ ! -x "$bin" ]; then
    echo "missing binary: $bin" >&2
    exit 1
fi

mkdir -p "$tmp"

expect_rc() {
    expected=$1
    label=$2
    shift 2
    set +e
    "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"
    rc=$?
    set -e
    if [ "$rc" != "$expected" ]; then
        echo "path safety failed: $label expected=$expected actual=$rc" >&2
        exit 1
    fi
}

python3 tools/gen_classic_store_fixture.py "$tmp/good.sit" "safe.txt"
expect_rc 0 good_extract "$bin" extract --overwrite -o "$tmp/out_good" "$tmp/good.sit"
test -f "$tmp/out_good/safe.txt"

python3 tools/gen_classic_store_fixture.py "$tmp/parent.sit" "../escape.txt"
python3 tools/gen_classic_store_fixture.py "$tmp/absolute.sit" "/tmp/escape.txt"
python3 tools/gen_classic_store_fixture.py "$tmp/backslash.sit" 'bad\escape.txt'
python3 tools/gen_classic_store_fixture.py "$tmp/colon.sit" 'bad:escape.txt'

expect_rc 3 parent_rejected "$bin" extract --overwrite -o "$tmp/out_parent" "$tmp/parent.sit"
expect_rc 3 absolute_rejected "$bin" extract --overwrite -o "$tmp/out_absolute" "$tmp/absolute.sit"
expect_rc 3 backslash_rejected "$bin" extract --overwrite -o "$tmp/out_backslash" "$tmp/backslash.sit"
expect_rc 3 colon_rejected "$bin" extract --overwrite -o "$tmp/out_colon" "$tmp/colon.sit"
test ! -e "$tmp/escape.txt"
test ! -e /tmp/escape.txt

echo "path safety tests ok"
