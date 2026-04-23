#!/usr/bin/env bash
set -u

bin=${1:-build/openstuffit}
tmp="${TMPDIR:-/tmp}/openstuffit_fuzz_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if [ ! -x "$bin" ]; then
    echo "missing binary: $bin" >&2
    exit 1
fi

mkdir -p "$tmp/cases"

base=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
hqx=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx

cp "$base" "$tmp/cases/base.sit"
cp "$hqx" "$tmp/cases/base.hqx"
python3 tools/gen_sit5_method14_fixture.py flat "$tmp/cases/method14.sit"
python3 tools/gen_sit5_method14_fixture.py rsrc "$tmp/cases/method14_rsrc.sit"

for size in 0 1 2 4 8 16 21 22 64 111 112 127 128 256 1024; do
    cp "$base" "$tmp/cases/trunc_${size}.sit"
    truncate -s "$size" "$tmp/cases/trunc_${size}.sit"
done

for size in 0 1 2 4 8 16 64 80 82 84 90 100 113 114 128 160 200; do
    cp "$tmp/cases/method14.sit" "$tmp/cases/method14_trunc_${size}.sit"
    truncate -s "$size" "$tmp/cases/method14_trunc_${size}.sit"
done

for size in 0 1 2 4 8 16 64 80 82 84 90 100 113 114 128 160 200 220 260; do
    cp "$tmp/cases/method14_rsrc.sit" "$tmp/cases/method14_rsrc_trunc_${size}.sit"
    truncate -s "$size" "$tmp/cases/method14_rsrc_trunc_${size}.sit"
done

for off in 0 1 2 4 6 10 14 22 34 84 88 92 96 100 102 110 128 256 564; do
    cp "$base" "$tmp/cases/flip_${off}.sit"
    printf '\377' | dd of="$tmp/cases/flip_${off}.sit" bs=1 seek="$off" count=1 conv=notrunc 2>/dev/null
done

for off in 0 1 4 80 82 84 88 90 96 100 102 110 114 120 128 150 180 190 200; do
    cp "$tmp/cases/method14.sit" "$tmp/cases/method14_flip_${off}.sit"
    printf '\377' | dd of="$tmp/cases/method14_flip_${off}.sit" bs=1 seek="$off" count=1 conv=notrunc 2>/dev/null
done

for off in 0 1 4 80 82 84 88 90 96 100 102 110 114 120 128 150 180 190 200 220 240 260; do
    cp "$tmp/cases/method14_rsrc.sit" "$tmp/cases/method14_rsrc_flip_${off}.sit"
    printf '\377' | dd of="$tmp/cases/method14_rsrc_flip_${off}.sit" bs=1 seek="$off" count=1 conv=notrunc 2>/dev/null
done

for off in 40 60 80 100 120 200 400; do
    cp "$hqx" "$tmp/cases/hqx_flip_${off}.hqx"
    printf ! | dd of="$tmp/cases/hqx_flip_${off}.hqx" bs=1 seek="$off" count=1 conv=notrunc 2>/dev/null
done

failures=0
count=0

run_allowing_known_exit() {
    label=$1
    shift
    "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"
    rc=$?
    case "$rc" in
        0|2|3|4) ;;
        *)
            echo "unexpected exit: label=$label rc=$rc" >&2
            failures=$((failures + 1))
            ;;
    esac
}

for path in "$tmp"/cases/*; do
    count=$((count + 1))
    run_allowing_known_exit "identify_$count" "$bin" identify "$path"
    run_allowing_known_exit "list_$count" "$bin" list "$path"
    run_allowing_known_exit "extract_$count" "$bin" extract --overwrite -o "$tmp/extract" "$path"
    rm -rf "$tmp/extract"
    run_allowing_known_exit "extract_rsrc_$count" "$bin" extract --overwrite --forks rsrc -o "$tmp/extract_rsrc" "$path"
    rm -rf "$tmp/extract_rsrc"
done

if [ "$failures" -ne 0 ]; then
    echo "fuzz smoke failed: cases=$count failures=$failures" >&2
    exit 1
fi

echo "fuzz smoke ok: cases=$count"
