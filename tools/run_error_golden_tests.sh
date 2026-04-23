#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
tmp="${TMPDIR:-/tmp}/openstuffit_error_golden_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if [ ! -x "$bin" ]; then
    echo "missing binary: $bin" >&2
    exit 1
fi

mkdir -p "$tmp"
fixture=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit

expect_error() {
    expected_rc=$1
    expected_file=$2
    label=$3
    shift 3
    set +e
    "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"
    rc=$?
    set -e
    if [ "$rc" != "$expected_rc" ]; then
        echo "error golden failed: $label expected=$expected_rc actual=$rc" >&2
        exit 1
    fi
    diff -u "$expected_file" "$tmp/$label.err"
}

expect_error 5 tests/expected/error_bad_forks.stderr bad_forks "$bin" extract --forks invalid "$fixture"
expect_error 5 tests/expected/error_bad_unicode.stderr bad_unicode "$bin" list --unicode-normalization bad "$fixture"
expect_error 5 tests/expected/error_bad_finder.stderr bad_finder "$bin" extract --finder invalid "$fixture"

echo "error golden tests ok"
