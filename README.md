# openstuffit

`openstuffit` is a C command line tool for inspecting and extracting classic StuffIt archives.

Supported input coverage in the current implementation:

- classic `.sit` and raw `.sea` payloads
- StuffIt 5 archives
- MacBinary, AppleSingle, AppleDouble, and BinHex wrappers
- `.sitx` detection as unsupported
- data forks and resource forks via `.rsrc`, AppleDouble, or macOS native fork mode

StuffIt X extraction, classic method 14 Installer archives, and macOS native resource fork writing on non-macOS platforms are not supported.

## Build

```sh
make all
```

The default build uses `cc` and links with zlib. It builds the CLI program,
static library, shared library, and pkg-config metadata:

- `build/openstuffit`
- `build/openstuffit-fr-bridge`
- `build/libopenstuffit.a`
- `build/libopenstuffit.so`
- `build/openstuffit.pc`

Useful verification targets:

```sh
make test
make test-library
make test-examples
make test-corpus-matrix
make test-corrupt
make test-fuzz-smoke
make test-sanitize
make test-valgrind
make test-clang
make test-werror
make test-cppcheck
make distcheck
```

`make test` is the full local matrix and assumes local fixture/tool repositories
exist under `reference_repos/` and the local `examples/` tree is present.

`test-scan-build` and `test-shellcheck` are optional and skip when the corresponding tool is not installed.

## Local Setup

This repository is configured to keep large research assets and local planning
notes outside normal version tracking (`.gitignore` includes
`reference_repos/`, `examples/`, and `PLAN_DEV_OPENSTUFFIT.md`).

For full local compatibility testing, clone the reference repositories:

```sh
mkdir -p reference_repos
git clone https://github.com/ssokolow/stuffit-test-files.git reference_repos/stuffit-test-files
git clone https://github.com/thecloudexpanse/sit.git reference_repos/sit
git clone https://github.com/MacPaw/XADMaster.git reference_repos/XADMaster
git clone https://github.com/MacPaw/universal-detector.git reference_repos/UniversalDetector
git clone https://github.com/benletchford/stuffit-rs.git reference_repos/stuffit-rs
```

`make test-examples` requires a local `examples/list_archive.c`.

## Packaging

Create the Linux source package with:

```sh
make dist
```

The tarball is written under `package/linux/plain/`:

```text
package/linux/plain/openstuffit-0.1.0-m1.tar.gz
```

Validate the packaged source by unpacking it under `/tmp` and running its
normal build and test suite with:

```sh
make distcheck-tarball
```

Linux packaging layout used by this repository:

- `package/linux/plain`: source tarball outputs (`make dist`, `make distcheck-tarball`)
- `package/linux/deb`: Debian package outputs (`make deb`)
- `package/linux/plain/patches`: optional integration patches (not part of Debian package payload)

Build Debian packages:

```sh
make deb
```

Generated binary package set:

- `openstuffit`: CLI binary
- `libopenstuffit0`: runtime shared library
- `libopenstuffit-dev`: headers/pkg-config/development linker files

Debian packaging intentionally excludes File Roller integration artifacts and
`openstuffit-fr-bridge`.

## Library

The CLI entry point lives in `src/openstuffit.c`. Reusable archive logic lives in
the `src/ost_*.c` modules and is packaged as `build/libopenstuffit.a` and
`build/libopenstuffit.so`.

Install the program, static library, and headers with:

```sh
make install PREFIX=/usr/local
```

This installs:

- `/usr/local/bin/openstuffit`
- `/usr/local/bin/openstuffit-fr-bridge`
- `/usr/local/lib/libopenstuffit.a`
- `/usr/local/lib/libopenstuffit.so`
- `/usr/local/include/openstuffit/openstuffit.h`
- `/usr/local/lib/pkgconfig/openstuffit.pc`

Example compile command after installation:

```sh
cc -std=c99 -D_FILE_OFFSET_BITS=64 -I/usr/local/include example.c -L/usr/local/lib -lopenstuffit -lz
```

With pkg-config:

```sh
cc -std=c99 -D_FILE_OFFSET_BITS=64 example.c $(pkg-config --cflags --libs openstuffit)
```

Use one public header:

```c
#include <openstuffit/openstuffit.h>
```

The API and ABI policy is documented in `document/API_OPENSTUFFIT.md`.

When available, the local `examples/list_archive.c` example uses only the
public header and can be verified with:

```sh
make test-examples
```

Additional local archive corpora can be smoke-tested with:

```sh
OPENSTUFFIT_CORPUS_DIR=/path/to/corpus make test-corpus-matrix
```

## Usage

```sh
build/openstuffit identify [--json] [--show-forks] <input>...
build/openstuffit list [-l|-L] [--json] [--unicode-normalization none|nfc|nfd] <input>
build/openstuffit extract [-o dir] [--password text] [--overwrite|--skip-existing|--rename-existing] [--preserve-time|--no-preserve-time] [--no-verify-crc] [--forks skip|rsrc|appledouble|both|native] [--finder skip|sidecar] [--unicode-normalization none|nfc|nfd] [--entry path]... <input>
build/openstuffit create -o <output.sit> [--follow-links|--no-follow-links] [-T type] [-C creator] <input>...
build/openstuffit dump [--json] (--headers|--forks|--entry index-or-path|--hex offset:length) <input>
```

Examples:

```sh
build/openstuffit identify --json --show-forks sample.sea.bin
build/openstuffit list -L archive.sit
build/openstuffit extract --overwrite --forks both -o out archive.sit
build/openstuffit create -o out.sit source_dir
build/openstuffit extract --overwrite -o out --entry testfile.txt archive.sit
build/openstuffit dump --json --entry testfile.txt archive.sit
```

## File Roller Bridge

`openstuffit-fr-bridge` is a stable CLI adapter intended for File Roller style
backend integration:

```sh
build/openstuffit-fr-bridge identify --json archive.sit
build/openstuffit-fr-bridge list --json archive.sit
build/openstuffit-fr-bridge extract --output-dir out --overwrite archive.sit
build/openstuffit-fr-bridge create --output out.sit source_dir
build/openstuffit-fr-bridge add --base-dir src --entry path/in/archive.txt archive.sit
build/openstuffit-fr-bridge delete --entry obsolete.txt archive.sit
build/openstuffit-fr-bridge extract --output-dir out --overwrite --entry testfile.txt archive.sit
```

Current write path (`create`/`add`/`delete`) is intentionally limited to classic raw
SIT archives without encrypted or resource forks. Unsupported write targets return
an explicit error.

Bridge tests:

```sh
make test-fr-bridge
```

`make test-fr-bridge` includes selected extraction coverage (`--entry`) so
single-entry extraction regressions are caught in CI/local runs.

It also covers directory write flows end-to-end:

- create archive from directory trees (directory entry emission)
- add directory trees into existing archives
- delete directory trees from existing archives (recursive removal semantics)
- extract by directory selector (`--entry <directory>`) and verify subtree output

## File Roller Local Track

For local development on systems where `gtk4 >= 4.8.1` is not available, use
the compatibility track in `reference_repos/file-roller-local` (tag `3.40.0`,
gtk3-based).

Local setup used in this project:

```sh
git clone reference_repos/file-roller reference_repos/file-roller-local
git -C reference_repos/file-roller-local checkout 3.40.0
cd reference_repos/file-roller-local
meson setup _build --prefix=/usr -Dnautilus-actions=disabled -Dpackagekit=false
ninja -C _build
ninja -C _build test
```

Run File Roller with the bridge path explicitly set:

```sh
OPENSTUFFIT_FR_BRIDGE=/mnt/USERS/onion/DATA_ORIGN/Workspace/openstuffit/build/openstuffit-fr-bridge \
reference_repos/file-roller-local/_build/src/file-roller
```

Open a target archive directly:

```sh
OPENSTUFFIT_FR_BRIDGE=/mnt/USERS/onion/DATA_ORIGN/Workspace/openstuffit/build/openstuffit-fr-bridge \
reference_repos/file-roller-local/_build/src/file-roller \
reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
```

Selection behavior:

- extracting all entries: no `--entry` arguments
- extracting selected entries (including drag-out of one selected file): backend
  passes repeated `--entry <path>` to `openstuffit-fr-bridge`
- path mapping strips leading `/` before passing entries to the bridge

Patch artifacts:

- `package/linux/plain/patches/file-roller/0001-openstuffit-backend.patch` (44.6 track)
- `package/linux/plain/patches/file-roller-local/0001-openstuffit-backend-3.40.0.patch` (local-compatible track)

## Resource Forks

`--forks skip` is the default. Other modes:

- `rsrc`: writes `<path>.rsrc`
- `appledouble`: writes sibling `._filename`
- `both`: writes both `.rsrc` and AppleDouble
- `native`: writes macOS `..namedfork/rsrc`; returns unsupported on non-macOS

Finder metadata can be written with `--finder sidecar` as `<path>.finder.json`.

## Filenames

Classic Mac names are decoded from MacRoman to UTF-8. Unicode normalization defaults to NFC and can be changed with:

```sh
--unicode-normalization none
--unicode-normalization nfc
--unicode-normalization nfd
```

Extraction rejects unsafe paths containing absolute paths, `..`, backslash, or colon.

## Exit Codes

| Code | Meaning |
| ---: | --- |
| 0 | success |
| 1 | I/O or internal error |
| 2 | unsupported format, feature, or compression method |
| 3 | bad format or checksum error |
| 4 | password required or bad password |
| 5 | command line usage error |

## Reference Repositories

The implementation was developed against local reference material under
`reference_repos/`. These repositories are inputs for format analysis,
compatibility checks, and fixture generation; they are not vendored library
dependencies of `openstuffit`.

| Purpose | Local path | License | Use in this project |
| --- | --- | --- | --- |
| Format structure and modern implementation notes | `reference_repos/stuffit-rs` | MIT or Apache-2.0, per README | Used to understand classic StuffIt/SIT5 parsing, fork metadata, and method 14 raw deflate behavior. |
| Real-world compatibility behavior | `reference_repos/XADMaster` | LGPL-2.1 | Used as the main comparison implementation for listing/extraction behavior and edge cases. `make test-xad-compare` can compare against its `lsar`/`unar` binaries. |
| XADMaster build dependency | `reference_repos/UniversalDetector` | LGPL-2.1 | Required by the local XADMaster build path. |
| Test corpus | `reference_repos/stuffit-test-files` | CC0 1.0/public domain dedication for author-owned fixture content; README documents self-extractor stub caveats | Provides `.sit`, `.sea`, `.sitx`, MacBinary, AppleSingle, and BinHex fixtures used by the test suite. |
| Early classic SIT generation behavior | `reference_repos/sit` | BSD-2-Clause overall; some files carry BSD-3-Clause or BSD-2-Clause-FreeBSD headers | Used to generate StuffIt 1.5.1-compatible method 2 LZW fixtures for regression tests. |

XADMaster and UniversalDetector are LGPL-2.1 material and are used only for
behavioral comparison, local tool builds, and format research; their code is
not copied into this C implementation. Generated or source-provenance fixture
coverage is documented in the SPEC files and in `tests/reports/latest.md`.

## License

`openstuffit` itself is licensed under the MIT License. See `LICENSE`.

Reference repositories under `reference_repos/` keep their own licenses, as
listed above. They are used as research material, comparison tools, or fixture
sources and are not relicensed as part of `openstuffit`.

## Documentation

Project planning and format notes are maintained in:

- `document/SPEC_SIT_CLASSIC.md`
- `document/SPEC_SIT5.md`
- `document/SPEC_SEA.md`
- `document/SPEC_MAC_WRAPPERS.md`
- `document/API_OPENSTUFFIT.md`

The latest generated test summary is `tests/reports/latest.md`.
