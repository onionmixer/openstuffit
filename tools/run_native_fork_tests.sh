#!/usr/bin/env bash
set -euo pipefail

bin=${1:-build/openstuffit}
fixture=${2:-reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit}
tmp="${TMPDIR:-/tmp}/openstuffit_native_forks_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if [ ! -x "$bin" ]; then
    echo "missing executable: $bin" >&2
    exit 1
fi

mkdir -p "$tmp"

case "$(uname -s)" in
    Darwin)
        "$bin" extract --overwrite --forks native -o "$tmp/native" "$fixture" >/dev/null
        "$bin" extract --overwrite --forks rsrc -o "$tmp/rsrc" "$fixture" >/dev/null
        cmp -s "$tmp/native/Test Text/..namedfork/rsrc" "$tmp/rsrc/Test Text.rsrc"
        cmp -s "$tmp/native/testfile.txt/..namedfork/rsrc" "$tmp/rsrc/testfile.txt.rsrc"
        cmp -s "$tmp/native/testfile.PICT/..namedfork/rsrc" "$tmp/rsrc/testfile.PICT.rsrc"
        echo "native fork tests ok: macOS native resource forks match .rsrc sidecars"
        ;;
    *)
        rc=0
        "$bin" extract --overwrite --forks native -o "$tmp/native" "$fixture" >/tmp/openstuffit_native_stdout.log 2>/tmp/openstuffit_native_stderr.log || rc=$?
        if [ "$rc" -ne 2 ]; then
            echo "expected --forks native to return 2 on non-macOS, got $rc" >&2
            exit 1
        fi
        grep -q "unsupported platform" /tmp/openstuffit_native_stderr.log
        echo "native fork tests ok: unsupported on non-macOS"
        ;;
esac
