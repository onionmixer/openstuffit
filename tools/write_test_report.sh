#!/usr/bin/env bash
set -u
set -o pipefail

bin=${1:-build/openstuffit}
out=${2:-tests/reports/latest.md}

mkdir -p "$(dirname "$out")"

run_status() {
    label=$1
    shift
    tmp_out="${TMPDIR:-/tmp}/openstuffit_report_$$.out"
    tmp_err="${TMPDIR:-/tmp}/openstuffit_report_$$.err"
    if "$@" >"$tmp_out" 2>"$tmp_err"; then
        rc=0
    else
        rc=$?
    fi
    printf '| `%s` | `%s` |\n' "$label" "$rc"
    rm -f "$tmp_out" "$tmp_err"
}

{
    printf '# openstuffit Test Report\n\n'
    printf 'Generated: `%s`\n\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

    printf '## Build\n\n'
    if [ -x "$bin" ]; then
        printf -- '- Binary: `%s`\n' "$bin"
        printf -- '- Version: `'
        "$bin" --version 2>/dev/null | tr -d '\n'
        printf '`\n'
    else
        printf -- '- Binary missing: `%s`\n' "$bin"
    fi
    printf '\n'

    printf '## Command Status\n\n'
    printf '| Command | Exit |\n'
    printf '| --- | --- |\n'
    run_status "make test" make test
    run_status "make test-library" make test-library
    run_status "make test-examples" make test-examples
    run_status "make test-symbols" make test-symbols
    run_status "make test-pkg-config" make test-pkg-config
    run_status "make test-install" make test-install
    run_status "make test-corpus-matrix" make test-corpus-matrix
    run_status "make test-password" make test-password
    run_status "make test-fixtures" make test-fixtures
    run_status "make test-cli-errors" make test-cli-errors
    run_status "make test-error-golden" make test-error-golden
    run_status "make test-list-matrix" make test-list-matrix
    run_status "make test-extract-matrix" make test-extract-matrix
    run_status "make test-generated-method-fixtures" make test-generated-method-fixtures
    run_status "make test-generator-selftest" make test-generator-selftest
    run_status "make test-json-schema" make test-json-schema
    run_status "make test-json-golden" make test-json-golden
    run_status "make test-path-safety" make test-path-safety
    run_status "make test-large-fixtures" make test-large-fixtures
    run_status "make test-unicode-filenames" make test-unicode-filenames
    run_status "make test-native-forks" make test-native-forks
    run_status "make test-identify-all" make test-identify-all
    run_status "make test-corrupt" make test-corrupt
    run_status "make test-fuzz-smoke" make test-fuzz-smoke
    run_status "make test-docs" make test-docs
    run_status "make test-method-scan" make test-method-scan
    run_status "make test-valgrind" make test-valgrind
    run_status "make test-clang" make test-clang
    run_status "make test-werror" make test-werror
    run_status "make test-cppcheck" make test-cppcheck
    run_status "make test-scan-build" make test-scan-build
    run_status "make test-shellcheck" make test-shellcheck
    run_status "make test-xad-compare" make test-xad-compare

    printf '\n## Fixture Smoke\n\n'
    printf '| Fixture | Expected | Actual |\n'
    printf '| --- | --- | --- |\n'
    for fixture in \
        reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit \
        reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin \
        reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx \
        reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit \
        reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit \
        reference_repos/stuffit-test-files/build/testfile.stuffit7.win.password.sit \
        reference_repos/stuffit-test-files/build/testfile.stuffit7_dlx.mac9.sitx
    do
        if [ -x "$bin" ]; then
            tmp_json="${TMPDIR:-/tmp}/openstuffit_report_ident_$$.json"
            "$bin" identify --json "$fixture" >"$tmp_json" 2>/dev/null
            rc=$?
            line=$(sed 's/|/\\|/g' "$tmp_json")
            rm -f "$tmp_json"
            printf '| `%s` | identify | rc=%s `%s` |\n' "$fixture" "$rc" "$line"
        else
            printf '| `%s` | identify | binary missing |\n' "$fixture"
        fi
    done

    printf '\n## Notes\n\n'
    printf -- '- Resource fork manifest checks use `tools/check_extract_manifest.sh` with `--forks rsrc`.\n'
    printf -- '- Generated method fixture checks create classic LZW method 2, classic Huffman method 3, and flat/nested/resource-fork SIT5 raw deflate method 14 archives through `tools/gen_sit5_method14_fixture.py`; method 14 dump and resource-fork rsrc/AppleDouble/both output modes are checked.\n'
    printf -- '- Fixture generator self-test validates generated SIT5 method 14 layout and corrupt variants without invoking `openstuffit`.\n'
    printf -- '- JSON schema tests parse identify/list/dump JSON with Python and assert stable key/value structure.\n'
    printf -- '- JSON golden tests diff generated method 14 list/dump JSON against checked-in expected files.\n'
    printf -- '- Path safety tests reject parent traversal, absolute, backslash, and colon paths during extract.\n'
    printf -- '- Large generated fixture tests cover 1 MiB stored data and larger SIT5 method 14 inflate/extract paths.\n'
    printf -- '- `make distcheck` performs clean, all, test, corrupt, fuzz, docs, and report generation in sequence.\n'
    printf -- '- Library checks validate the single public header, static/shared link paths, exported symbols, examples, install/uninstall, and pkg-config dynamic/static flags.\n'
    printf -- '- Corpus matrix tests representative `.sit`, `.sea`, `.hqx`, and unsupported `.sitx` files, plus `OPENSTUFFIT_CORPUS_DIR` when provided.\n'
    printf -- '- `make distcheck-tarball` validates `package/linux/openstuffit-0.1.0-m1.tar.gz` by unpacking it under `/tmp` and running `make all && make test`.\n'
    printf -- '- Corrupt fixture checks include malformed SIT/HQX inputs, SIT5 method 14 data fork truncation/stream/length failures, and method 14 resource fork stream/CRC failures.\n'
    printf -- '- Fuzz smoke mutates classic SIT, BinHex HQX, and generated SIT5 method 14 data/resource fork raw deflate archives, including `extract --forks rsrc`.\n'
    printf -- '- CLI error matrix fixes invalid/missing argument cases at exit `5`.\n'
    printf -- '- Error golden tests diff representative stderr messages for invalid CLI options.\n'
    printf -- '- Valgrind smoke checks unit, identify, list, dump, and extract paths when valgrind is available.\n'
    printf -- '- Clang, Werror, cppcheck, scan-build, and shellcheck targets cover compiler/static-analysis portability; unavailable optional tools skip cleanly.\n'
    printf -- '- Method scan details are written to `tests/reports/method_scan_latest.md`.\n'
    printf -- '- `.sitx` fixtures are expected unsupported cases.\n'
    printf -- '- Password protected SIT5 fixtures extract with `--password password`; wrong or missing passwords return exit `4`.\n'
    printf -- '- Password protected classic SIT fixtures list metadata; MacBinary resource fork fixtures expose `MKey`, per-entry trailer sizes, and extract successfully with `--password password`.\n'
    printf -- '- BinHex `.hqx` header/data/resource CRC16 mismatches are expected checksum failures.\n'
} > "$out"

echo "wrote $out"
