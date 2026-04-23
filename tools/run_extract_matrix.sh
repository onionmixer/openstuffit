#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
matrix=${2:-tests/fixtures/extract_matrix.tsv}

if [ ! -x "$bin" ]; then
    echo "missing executable: $bin" >&2
    exit 1
fi

if [ ! -f "$matrix" ]; then
    echo "missing matrix: $matrix" >&2
    exit 1
fi

count=0
while IFS='|' read -r id fixture manifest; do
    case "${id:-}" in
        ""|\#*) continue ;;
    esac

    bash tools/check_extract_manifest.sh "$bin" "$fixture" "$manifest" >/dev/null
    count=$((count + 1))
done < "$matrix"

echo "extract matrix ok: cases=$count"
