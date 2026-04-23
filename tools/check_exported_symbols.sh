#!/usr/bin/env bash
set -euo pipefail

lib=${1:?usage: check_exported_symbols.sh <shared-library>}
expected=${2:-tests/expected/exported_symbols.txt}
actual=/tmp/openstuffit_exported_symbols.$$
trap 'rm -f "$actual"' EXIT

case "$(uname -s 2>/dev/null || true)" in
  Darwin)
    nm -gU "$lib" | awk '{print $NF}' | sed 's/^_//' | sort > "$actual"
    ;;
  *)
    nm -D --defined-only "$lib" | awk '{print $NF}' | sort > "$actual"
    ;;
esac

if ! cmp -s "$expected" "$actual"; then
  echo "exported symbol mismatch" >&2
  echo "--- expected" >&2
  cat "$expected" >&2
  echo "--- actual" >&2
  cat "$actual" >&2
  exit 1
fi

echo "exported symbols ok"
