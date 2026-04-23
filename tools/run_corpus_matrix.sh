#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
matrix=${2:-tests/fixtures/corpus_matrix.tsv}
cases=0

run_case() {
  local file=$1
  local identify_expected=$2
  local list_expected=$3
  local extract_expected=$4
  local out_dir="/tmp/openstuffit_corpus_extract_$$"
  local rc

  set +e
  "$bin" identify "$file" >/dev/null 2>/tmp/openstuffit_corpus.err
  rc=$?
  set -e
  check_rc "$file identify" "$identify_expected" "$rc"

  set +e
  "$bin" list "$file" >/dev/null 2>/tmp/openstuffit_corpus.err
  rc=$?
  set -e
  check_rc "$file list" "$list_expected" "$rc"

  rm -rf "$out_dir"
  set +e
  "$bin" extract --overwrite -o "$out_dir" "$file" >/dev/null 2>/tmp/openstuffit_corpus.err
  rc=$?
  set -e
  check_rc "$file extract" "$extract_expected" "$rc"
  rm -rf "$out_dir"
  cases=$((cases + 1))
}

check_rc() {
  local label=$1
  local expected=$2
  local rc=$3
  case "$expected" in
    OK)
      if [ "$rc" -ne 0 ]; then
        echo "corpus case failed: $label expected OK rc=$rc" >&2
        cat /tmp/openstuffit_corpus.err >&2
        exit 1
      fi
      ;;
    UNSUPPORTED)
      if [ "$rc" -ne 2 ]; then
        echo "corpus case failed: $label expected unsupported rc=$rc" >&2
        cat /tmp/openstuffit_corpus.err >&2
        exit 1
      fi
      ;;
    ANY)
      if [ "$rc" -lt 0 ] || [ "$rc" -gt 5 ]; then
        echo "corpus case failed: $label unexpected rc=$rc" >&2
        cat /tmp/openstuffit_corpus.err >&2
        exit 1
      fi
      ;;
    *)
      echo "unknown expected value for $label: $expected" >&2
      exit 1
      ;;
  esac
}

while IFS=$'\t' read -r file identify_expected list_expected extract_expected; do
  case "$file" in
    ""|\#*) continue ;;
  esac
  run_case "$file" "$identify_expected" "$list_expected" "$extract_expected"
done < "$matrix"

if [ -n "${OPENSTUFFIT_CORPUS_DIR:-}" ] && [ -d "$OPENSTUFFIT_CORPUS_DIR" ]; then
  while IFS= read -r file; do
    run_case "$file" ANY ANY ANY
  done < <(find "$OPENSTUFFIT_CORPUS_DIR" -type f \( -name '*.sit' -o -name '*.sea' -o -name '*.hqx' -o -name '*.bin' -o -name '*.AS' -o -name '*.as' \) | sort)
fi

rm -f /tmp/openstuffit_corpus.err
echo "corpus matrix ok: cases=$cases"
