#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
out=${2:-tests/reports/method_scan_latest.md}

if [ ! -x "$bin" ]; then
    echo "missing executable: $bin" >&2
    exit 1
fi

tmp="${TMPDIR:-/tmp}/openstuffit_method_scan_$$"
cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT
mkdir -p "$tmp"
mkdir -p "$(dirname "$out")"
: >"$tmp/methods.tsv"

scan_one() {
    local path=$1
    case "$path" in
        *.exe|*.sitx|*.jpg|*.jpeg|*.md|*.txt|*.png|*.PICT) return 0 ;;
    esac
    local json="$tmp/item.json"
    if "$bin" list --json "$path" >"$json" 2>/dev/null; then
        for method in 0 1 2 3 5 6 8 13 14 15; do
            if grep -q "\"method\":$method" "$json"; then
                printf '%s\t%s\n' "$method" "$path" >>"$tmp/methods.tsv"
            fi
        done
    fi
}

while IFS= read -r path; do
    scan_one "$path"
done < <(find reference_repos/stuffit-test-files/build reference_repos/stuffit-rs/tests/fixtures -type f 2>/dev/null | sort)

{
    printf '# openstuffit Method Scan\n\n'
    printf 'Generated: `%s`\n\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '## Summary\n\n'
    printf '| Method | Fixture count |\n'
    printf '| ---: | ---: |\n'
    for method in 0 1 2 3 5 6 8 13 14 15; do
        count=$(awk -F '\t' -v m="$method" '$1 == m { seen[$2]=1 } END { print length(seen) }' "$tmp/methods.tsv")
        printf '| `%s` | `%s` |\n' "$method" "$count"
    done
    printf '\n## Method 2/3/14 Candidates\n\n'
    matches=$(awk -F '\t' '$1 == 2 || $1 == 3 || $1 == 14 { print }' "$tmp/methods.tsv" | sort -u)
    if [ -n "$matches" ]; then
        printf '| Method | Fixture |\n'
        printf '| ---: | --- |\n'
        printf '%s\n' "$matches" | while IFS=$'\t' read -r method path; do
            printf '| `%s` | `%s` |\n' "$method" "$path"
        done
    else
        printf 'No original method 2, method 3, or method 14 fixtures were found in the scanned local fixture set.\n'
    fi
} >"$out"

echo "wrote $out"
