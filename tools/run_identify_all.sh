#!/usr/bin/env bash
set -u
set -o pipefail

bin=${1:-build/openstuffit}
root=${2:-reference_repos/stuffit-test-files/build}

if [ ! -x "$bin" ]; then
    echo "missing executable: $bin" >&2
    exit 1
fi

if [ ! -d "$root" ]; then
    echo "missing fixture root: $root" >&2
    exit 1
fi

total=0
ok=0
unsupported=0
checksum=0
password=0
failed=0

while IFS= read -r fixture; do
    total=$((total + 1))
    "$bin" identify "$fixture" >/dev/null 2>/dev/null
    rc=$?
    case "$rc" in
        0)
            ok=$((ok + 1))
            ;;
        2)
            unsupported=$((unsupported + 1))
            ;;
        3)
            checksum=$((checksum + 1))
            ;;
        4)
            password=$((password + 1))
            ;;
        *)
            failed=$((failed + 1))
            echo "identify failed rc=$rc: $fixture" >&2
            ;;
    esac
done < <(find "$root" -type f | sort)

echo "identify all: total=$total ok=$ok unsupported=$unsupported checksum=$checksum password=$password failed=$failed"
test "$failed" -eq 0
