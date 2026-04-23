#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
unit=${2:-build/test_m1}
tmp="${TMPDIR:-/tmp}/openstuffit_valgrind_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if ! command -v valgrind >/dev/null 2>&1; then
    echo "valgrind not found" >&2
    exit 77
fi
if [ ! -x "$bin" ] || [ ! -x "$unit" ]; then
    echo "missing binary for valgrind smoke" >&2
    exit 1
fi

mkdir -p "$tmp"
fixture=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
method14="$tmp/method14_rsrc.sit"
python3 tools/gen_sit5_method14_fixture.py rsrc "$method14"

vg() {
    label=$1
    shift
    valgrind --quiet --error-exitcode=99 --leak-check=full "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"
}

vg unit "$unit"
vg identify "$bin" identify "$fixture"
vg list "$bin" list --json "$method14"
vg dump "$bin" dump --json --entry method14_rsrc.txt "$method14"
vg extract "$bin" extract --overwrite --forks rsrc -o "$tmp/out" "$method14"

echo "valgrind smoke ok"
