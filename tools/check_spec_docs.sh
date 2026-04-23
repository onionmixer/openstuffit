#!/usr/bin/env bash
set -euo pipefail

specs=(
    document/SPEC_SIT_CLASSIC.md
    document/SPEC_SIT5.md
    document/SPEC_SEA.md
    document/SPEC_MAC_WRAPPERS.md
)

for spec in "${specs[@]}"; do
    if [ ! -f "$spec" ]; then
        echo "missing spec: $spec" >&2
        exit 1
    fi
    grep -qi 'hexdump' "$spec" || {
        echo "missing hexdump section: $spec" >&2
        exit 1
    }
    grep -q 'Implementation map' "$spec" || {
        echo "missing implementation map: $spec" >&2
        exit 1
    }
    grep -q 'Covered tests' "$spec" || {
        echo "missing covered tests: $spec" >&2
        exit 1
    }
done

echo "spec docs ok: ${#specs[@]} files"
