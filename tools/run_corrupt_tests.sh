#!/usr/bin/env bash
set -u

bin=${1:-build/openstuffit}
tmp="${TMPDIR:-/tmp}/openstuffit_corrupt_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

mkdir -p "$tmp"

failures=0

expect_rc() {
    expected=$1
    label=$2
    shift 2
    "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"
    rc=$?
    if [ "$rc" != "$expected" ]; then
        echo "corrupt test failed: $label expected=$expected actual=$rc" >&2
        failures=$((failures + 1))
    else
        echo "corrupt test ok: $label"
    fi
}

if [ ! -x "$bin" ]; then
    echo "missing binary: $bin" >&2
    exit 1
fi

classic=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
hqx=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx

cp "$classic" "$tmp/bad_sig.sit"
printf X | dd of="$tmp/bad_sig.sit" bs=1 seek=0 count=1 conv=notrunc 2>/dev/null
expect_rc 2 bad_sig "$bin" identify "$tmp/bad_sig.sit"

cp "$classic" "$tmp/truncated.sit"
truncate -s 16 "$tmp/truncated.sit"
expect_rc 3 truncated_list "$bin" list "$tmp/truncated.sit"

cp "$classic" "$tmp/bad_crc.sit"
printf X | dd of="$tmp/bad_crc.sit" bs=1 seek=564 count=1 conv=notrunc 2>/dev/null
expect_rc 3 bad_crc_extract "$bin" extract --overwrite -o "$tmp/out_bad_crc" "$tmp/bad_crc.sit"

cp "$hqx" "$tmp/bad_hqx_crc.hqx"
printf ! | dd of="$tmp/bad_hqx_crc.hqx" bs=1 seek=100 count=1 conv=notrunc 2>/dev/null
expect_rc 3 bad_hqx_crc "$bin" identify "$tmp/bad_hqx_crc.hqx"

python3 tools/gen_classic_store_fixture.py "$tmp/classic_impossible_clen.sit" "classic_bad.bin"
python3 - "$tmp/classic_impossible_clen.sit" <<'PY'
import sys
path = sys.argv[1]
data = bytearray(open(path, "rb").read())
data[22 + 96:22 + 100] = (0x7fffffff).to_bytes(4, "big")
open(path, "wb").write(data)
PY
expect_rc 3 classic_impossible_clen "$bin" extract --overwrite -o "$tmp/out_classic_impossible_clen" "$tmp/classic_impossible_clen.sit"

python3 tools/gen_sit5_method14_fixture.py flat "$tmp/method14.sit"
python3 tools/gen_sit5_method14_fixture.py flat "$tmp/method14_truncated.sit" --variant truncated
python3 tools/gen_sit5_method14_fixture.py flat "$tmp/method14_bad_stream.sit" --variant bad-stream
python3 tools/gen_sit5_method14_fixture.py flat "$tmp/method14_bad_ulen.sit" --variant bad-ulen
cp "$tmp/method14.sit" "$tmp/method14_bad_first_offset.sit"
cp "$tmp/method14.sit" "$tmp/method14_bad_header_size.sit"
cp "$tmp/method14.sit" "$tmp/method14_impossible_clen.sit"
python3 - "$tmp/method14_bad_first_offset.sit" "$tmp/method14_bad_header_size.sit" "$tmp/method14_impossible_clen.sit" <<'PY'
import sys
bad_first, bad_header, bad_clen = sys.argv[1:4]

data = bytearray(open(bad_first, "rb").read())
data[94:98] = (0x7fffffff).to_bytes(4, "big")
open(bad_first, "wb").write(data)

data = bytearray(open(bad_header, "rb").read())
data[114 + 6:114 + 8] = (1).to_bytes(2, "big")
open(bad_header, "wb").write(data)

data = bytearray(open(bad_clen, "rb").read())
data[114 + 38:114 + 42] = (0x7fffffff).to_bytes(4, "big")
open(bad_clen, "wb").write(data)
PY

expect_rc 0 method14_valid_extract "$bin" extract --overwrite -o "$tmp/out_method14_valid" "$tmp/method14.sit"
expect_rc 3 method14_truncated_extract "$bin" extract --overwrite -o "$tmp/out_method14_truncated" "$tmp/method14_truncated.sit"
expect_rc 3 method14_bad_stream_extract "$bin" extract --overwrite -o "$tmp/out_method14_bad_stream" "$tmp/method14_bad_stream.sit"
expect_rc 3 method14_bad_ulen_extract "$bin" extract --overwrite -o "$tmp/out_method14_bad_ulen" "$tmp/method14_bad_ulen.sit"
expect_rc 3 method14_bad_first_offset_list "$bin" list "$tmp/method14_bad_first_offset.sit"
expect_rc 3 method14_bad_header_size_list "$bin" list "$tmp/method14_bad_header_size.sit"
expect_rc 3 method14_impossible_clen_extract "$bin" extract --overwrite -o "$tmp/out_method14_impossible_clen" "$tmp/method14_impossible_clen.sit"

python3 tools/gen_sit5_method14_fixture.py rsrc "$tmp/method14_rsrc.sit"
python3 tools/gen_sit5_method14_fixture.py rsrc "$tmp/method14_rsrc_bad_stream.sit" --variant bad-rsrc-stream
python3 tools/gen_sit5_method14_fixture.py rsrc "$tmp/method14_rsrc_bad_crc.sit" --variant bad-rsrc-crc

expect_rc 0 method14_rsrc_valid_extract "$bin" extract --overwrite --forks rsrc -o "$tmp/out_method14_rsrc_valid" "$tmp/method14_rsrc.sit"
expect_rc 3 method14_rsrc_bad_stream_extract "$bin" extract --overwrite --forks rsrc -o "$tmp/out_method14_rsrc_bad_stream" "$tmp/method14_rsrc_bad_stream.sit"
expect_rc 3 method14_rsrc_bad_crc_extract "$bin" extract --overwrite --forks rsrc -o "$tmp/out_method14_rsrc_bad_crc" "$tmp/method14_rsrc_bad_crc.sit"

if [ "$failures" -ne 0 ]; then
    exit 1
fi

echo "corrupt tests ok"
