# openstuffit Test Report

Generated: `2026-04-23T01:41:18Z`

## Build

- Binary: `build/openstuffit`
- Version: `openstuffit 0.1.0-m1`

## Command Status

| Command | Exit |
| --- | --- |
| `make test` | `0` |
| `make test-library` | `0` |
| `make test-examples` | `0` |
| `make test-symbols` | `0` |
| `make test-pkg-config` | `0` |
| `make test-install` | `0` |
| `make test-corpus-matrix` | `0` |
| `make test-password` | `0` |
| `make test-fixtures` | `0` |
| `make test-cli-errors` | `0` |
| `make test-error-golden` | `0` |
| `make test-list-matrix` | `0` |
| `make test-extract-matrix` | `0` |
| `make test-generated-method-fixtures` | `0` |
| `make test-generator-selftest` | `0` |
| `make test-json-schema` | `0` |
| `make test-json-golden` | `0` |
| `make test-path-safety` | `0` |
| `make test-large-fixtures` | `0` |
| `make test-unicode-filenames` | `0` |
| `make test-native-forks` | `0` |
| `make test-identify-all` | `0` |
| `make test-corrupt` | `0` |
| `make test-fuzz-smoke` | `0` |
| `make test-docs` | `0` |
| `make test-method-scan` | `0` |
| `make test-valgrind` | `0` |
| `make test-clang` | `0` |
| `make test-werror` | `0` |
| `make test-cppcheck` | `0` |
| `make test-scan-build` | `0` |
| `make test-shellcheck` | `0` |
| `make test-xad-compare` | `0` |

## Fixture Smoke

| Fixture | Expected | Actual |
| --- | --- | --- |
| `reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit` | identify | rc=0 `{"input":"reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit","status":"ok","wrapper":"raw","format":"sit-classic","supported":true,"payload_offset":0,"payload_size":2804}` |
| `reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin` | identify | rc=0 `{"input":"reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin","status":"ok","wrapper":"macbinary","format":"sit-classic","supported":true,"payload_offset":128,"payload_size":2804}` |
| `reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx` | identify | rc=0 `{"input":"reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx","status":"ok","wrapper":"binhex","format":"sit-classic","supported":true,"payload_offset":0,"payload_size":2804}` |
| `reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit` | identify | rc=0 `{"input":"reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit","status":"ok","wrapper":"raw","format":"sit5","supported":true,"payload_offset":0,"payload_size":795}` |
| `reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit` | identify | rc=0 `{"input":"reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.password.sit","status":"ok","wrapper":"raw","format":"sit-classic","supported":true,"payload_offset":0,"payload_size":2990}` |
| `reference_repos/stuffit-test-files/build/testfile.stuffit7.win.password.sit` | identify | rc=0 `{"input":"reference_repos/stuffit-test-files/build/testfile.stuffit7.win.password.sit","status":"ok","wrapper":"raw","format":"sit5","supported":true,"payload_offset":0,"payload_size":816}` |
| `reference_repos/stuffit-test-files/build/testfile.stuffit7_dlx.mac9.sitx` | identify | rc=2 `{"input":"reference_repos/stuffit-test-files/build/testfile.stuffit7_dlx.mac9.sitx","status":"unsupported","wrapper":"raw","format":"sitx","supported":false,"payload_offset":0,"payload_size":2074}` |

## Notes

- Resource fork manifest checks use `tools/check_extract_manifest.sh` with `--forks rsrc`.
- Generated method fixture checks create classic LZW method 2, classic Huffman method 3, and flat/nested/resource-fork SIT5 raw deflate method 14 archives through `tools/gen_sit5_method14_fixture.py`; method 14 dump and resource-fork rsrc/AppleDouble/both output modes are checked.
- Fixture generator self-test validates generated SIT5 method 14 layout and corrupt variants without invoking `openstuffit`.
- JSON schema tests parse identify/list/dump JSON with Python and assert stable key/value structure.
- JSON golden tests diff generated method 14 list/dump JSON against checked-in expected files.
- Path safety tests reject parent traversal, absolute, backslash, and colon paths during extract.
- Large generated fixture tests cover 1 MiB stored data and larger SIT5 method 14 inflate/extract paths.
- `make distcheck` performs clean, all, test, corrupt, fuzz, docs, and report generation in sequence.
- Library checks validate the single public header, static/shared link paths, exported symbols, examples, install/uninstall, and pkg-config dynamic/static flags.
- Corpus matrix tests representative `.sit`, `.sea`, `.hqx`, and unsupported `.sitx` files, plus `OPENSTUFFIT_CORPUS_DIR` when provided.
- `make distcheck-tarball` validates the generated source tarball by unpacking it under `/tmp` and running `make all && make test`.
- Corrupt fixture checks include malformed SIT/HQX inputs, SIT5 method 14 data fork truncation/stream/length failures, and method 14 resource fork stream/CRC failures.
- Fuzz smoke mutates classic SIT, BinHex HQX, and generated SIT5 method 14 data/resource fork raw deflate archives, including `extract --forks rsrc`.
- CLI error matrix fixes invalid/missing argument cases at exit `5`.
- Error golden tests diff representative stderr messages for invalid CLI options.
- Valgrind smoke checks unit, identify, list, dump, and extract paths when valgrind is available.
- Clang, Werror, cppcheck, scan-build, and shellcheck targets cover compiler/static-analysis portability; unavailable optional tools skip cleanly.
- Method scan details are written to `tests/reports/method_scan_latest.md`.
- `.sitx` fixtures are expected unsupported cases.
- Password protected SIT5 fixtures extract with `--password password`; wrong or missing passwords return exit `4`.
- Password protected classic SIT fixtures list metadata; MacBinary resource fork fixtures expose `MKey`, per-entry trailer sizes, and extract successfully with `--password password`.
- BinHex `.hqx` header/data/resource CRC16 mismatches are expected checksum failures.
