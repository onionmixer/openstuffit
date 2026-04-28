#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bridge="$repo_root/build/openstuffit-fr-bridge"
file_roller="$repo_root/reference_repos/file-roller-local/_build/src/file-roller"

if [ ! -x "$bridge" ]; then
    echo "missing executable: $bridge (run 'make' in $repo_root)" >&2
    exit 1
fi
if [ ! -x "$file_roller" ]; then
    echo "missing executable: $file_roller (build file-roller-local first)" >&2
    exit 1
fi

export OPENSTUFFIT_FR_BRIDGE="$bridge"
export PATH="$repo_root/build:${PATH:-}"

exec "$file_roller" "$@"
