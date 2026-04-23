#!/usr/bin/env bash
set -u

bin=${1:-build/openstuffit}
tmp="${TMPDIR:-/tmp}/openstuffit_cli_errors_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

mkdir -p "$tmp"

fixture=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
failures=0

expect_rc() {
    expected=$1
    label=$2
    shift 2
    "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"
    rc=$?
    if [ "$rc" != "$expected" ]; then
        echo "cli error matrix failed: $label expected=$expected actual=$rc" >&2
        failures=$((failures + 1))
    fi
}

if [ ! -x "$bin" ]; then
    echo "missing binary: $bin" >&2
    exit 1
fi

expect_rc 5 no_args "$bin"
expect_rc 5 unknown_command "$bin" unknown-command
expect_rc 5 identify_missing_input "$bin" identify
expect_rc 5 list_missing_input "$bin" list
expect_rc 5 list_bad_unicode "$bin" list --unicode-normalization bad "$fixture"
expect_rc 5 extract_bad_unicode "$bin" extract --unicode-normalization bad "$fixture"
expect_rc 5 extract_bad_forks "$bin" extract --forks invalid "$fixture"
expect_rc 5 extract_bad_finder "$bin" extract --finder invalid "$fixture"
expect_rc 5 dump_missing_mode "$bin" dump "$fixture"
expect_rc 5 dump_missing_entry_value "$bin" dump --entry "$fixture"
expect_rc 5 dump_bad_hex_range "$bin" dump --hex bad "$fixture"

if [ "$failures" -ne 0 ]; then
    exit 1
fi

echo "cli error matrix ok: cases=11"
