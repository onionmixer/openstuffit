#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
matrix=${2:-tests/fixtures/list_matrix.tsv}

if [ ! -x "$bin" ]; then
    echo "missing executable: $bin" >&2
    exit 1
fi

if [ ! -f "$matrix" ]; then
    echo "missing matrix: $matrix" >&2
    exit 1
fi

count=0
while IFS='|' read -r id fixture format wrapper entries tokens; do
    case "${id:-}" in
        ""|\#*) continue ;;
    esac

    tmp=$(mktemp "${TMPDIR:-/tmp}/openstuffit_list_matrix.XXXXXX")
    if ! "$bin" list --json "$fixture" >"$tmp"; then
        echo "list failed: $id $fixture" >&2
        rm -f "$tmp"
        exit 1
    fi

    grep -q "\"format\":\"$format\"" "$tmp" || {
        echo "format mismatch: $id expected=$format" >&2
        rm -f "$tmp"
        exit 1
    }
    grep -q "\"wrapper\":\"$wrapper\"" "$tmp" || {
        echo "wrapper mismatch: $id expected=$wrapper" >&2
        rm -f "$tmp"
        exit 1
    }

    actual_entries=$(grep -o '"index":' "$tmp" | wc -l | awk '{print $1}')
    if [ "$actual_entries" != "$entries" ]; then
        echo "entry count mismatch: $id expected=$entries actual=$actual_entries" >&2
        rm -f "$tmp"
        exit 1
    fi

    old_ifs=$IFS
    IFS=','
    for token in $tokens; do
        if [ -n "$token" ]; then
            grep -q "$token" "$tmp" || {
                echo "missing token: $id token=$token" >&2
                IFS=$old_ifs
                rm -f "$tmp"
                exit 1
            }
        fi
    done
    IFS=$old_ifs

    rm -f "$tmp"
    count=$((count + 1))
done < "$matrix"

echo "list matrix ok: cases=$count"
