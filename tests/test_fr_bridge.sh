#!/usr/bin/env bash
set -u

bin=${1:-build/openstuffit-fr-bridge}
tmp="${TMPDIR:-/tmp}/openstuffit_fr_bridge_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if [ ! -x "$bin" ]; then
    echo "missing binary: $bin" >&2
    exit 1
fi

mkdir -p "$tmp"

fixture_sit=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
fixture_sea_bin=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin
fixture_pw=reference_repos/stuffit-test-files/build/testfile.stuffit7.win.password.sit
fixture_sitx=reference_repos/stuffit-test-files/build/testfile.stuffit7_dlx.mac9.sitx

failures=0

expect_rc() {
    expected=$1
    label=$2
    shift 2
    "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"
    rc=$?
    if [ "$rc" != "$expected" ]; then
        echo "fr bridge test failed: $label expected=$expected actual=$rc" >&2
        failures=$((failures + 1))
    fi
}

expect_rc 0 identify "$bin" identify --json "$fixture_sea_bin"
diff -u tests/expected/fr_bridge_identify_sea_bin.json "$tmp/identify.out" || failures=$((failures + 1))

expect_rc 0 list "$bin" list --json "$fixture_sit"
diff -u tests/expected/fr_bridge_list_stuffit45.json "$tmp/list.out" || failures=$((failures + 1))

expect_rc 0 extract "$bin" extract --overwrite --output-dir "$tmp/out" "$fixture_sit"
grep -q '"status":"ok"' "$tmp/extract.out" || failures=$((failures + 1))
test -f "$tmp/out/testfile.txt" || failures=$((failures + 1))
cmp -s "$tmp/out/testfile.txt" reference_repos/stuffit-test-files/sources/testfile.txt || failures=$((failures + 1))

expect_rc 0 extract_selected "$bin" extract --overwrite --output-dir "$tmp/out_selected" --entry testfile.txt "$fixture_sit"
grep -q '"extracted_files":1' "$tmp/extract_selected.out" || failures=$((failures + 1))
test -f "$tmp/out_selected/testfile.txt" || failures=$((failures + 1))
test ! -e "$tmp/out_selected/testfile.jpg" || failures=$((failures + 1))

expect_rc 0 extract_selected_slash "$bin" extract --overwrite --output-dir "$tmp/out_selected_slash" --entry /testfile.png "$fixture_sit"
test -f "$tmp/out_selected_slash/testfile.png" || failures=$((failures + 1))
test ! -e "$tmp/out_selected_slash/testfile.txt" || failures=$((failures + 1))

expect_rc 4 extract_password_required "$bin" extract --overwrite --output-dir "$tmp/pw_required" "$fixture_pw"
expect_rc 4 extract_password_bad "$bin" extract --password wrong --overwrite --output-dir "$tmp/pw_bad" "$fixture_pw"
expect_rc 0 extract_password_ok "$bin" extract --password password --overwrite --output-dir "$tmp/pw_ok" "$fixture_pw"
test -f "$tmp/pw_ok/sources/testfile.txt" || failures=$((failures + 1))

expect_rc 2 sitx_unsupported "$bin" list --json "$fixture_sitx"

if [ "$failures" -ne 0 ]; then
    exit 1
fi

echo "fr bridge tests ok"
