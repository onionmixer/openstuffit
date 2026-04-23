#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <openstuffit> <archive> <manifest.tsv>" >&2
    exit 5
fi

bin=$1
archive=$2
manifest=$3
out=$(mktemp -d "${TMPDIR:-/tmp}/openstuffit_manifest.XXXXXX")
stdout_log=$(mktemp "${TMPDIR:-/tmp}/openstuffit_manifest_stdout.XXXXXX")
stderr_log=$(mktemp "${TMPDIR:-/tmp}/openstuffit_manifest_stderr.XXXXXX")

cleanup() {
    rm -rf "$out"
    rm -f "$stdout_log" "$stderr_log"
}
trap cleanup EXIT

"$bin" extract --overwrite --forks rsrc -o "$out" "$archive" >"$stdout_log" 2>"$stderr_log"

expected_count=0
while IFS=$'\t' read -r expected_hash expected_size relpath; do
    if [ "$expected_hash" = "sha256" ]; then
        continue
    fi
    expected_count=$((expected_count + 1))
    file="$out/$relpath"
    if [ ! -f "$file" ]; then
        echo "missing extracted file: $relpath" >&2
        exit 1
    fi
    actual_hash=$(sha256sum "$file" | awk '{print $1}')
    actual_size=$(stat -c '%s' "$file")
    if [ "$actual_hash" != "$expected_hash" ]; then
        echo "sha256 mismatch: $relpath expected=$expected_hash actual=$actual_hash" >&2
        exit 1
    fi
    if [ "$actual_size" != "$expected_size" ]; then
        echo "size mismatch: $relpath expected=$expected_size actual=$actual_size" >&2
        exit 1
    fi
done < "$manifest"

actual_count=$(find "$out" -type f | wc -l | awk '{print $1}')
if [ "$actual_count" != "$expected_count" ]; then
    echo "file count mismatch: expected=$expected_count actual=$actual_count" >&2
    find "$out" -type f -printf '%P\n' | sort >&2
    exit 1
fi

echo "extract manifest ok: $archive"
