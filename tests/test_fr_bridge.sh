#!/usr/bin/env bash
set -u

bin=${1:-build/openstuffit-fr-bridge}
tmp="${TMPDIR:-/tmp}/openstuffit_fr_bridge_$$"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT

if [ ! -x "$bin" ]; then
    echo "missing binary: $bin" >&2
    exit 1
fi

mkdir -p "$tmp"

fixture_sit=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
fixture_sea_bin=reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin
fixture_pw=reference_repos/stuffit-test-files/build/testfile.stuffit7.win.password.sit
fixture_sitx=reference_repos/stuffit-test-files/build/testfile.stuffit7_dlx.mac9.sitx

failures=0

expect_rc() {
    expected=$1
    label=$2
    shift 2
    "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"
    rc=$?
    if [ "$rc" != "$expected" ]; then
        echo "fr bridge test failed: $label expected=$expected actual=$rc" >&2
        failures=$((failures + 1))
    fi
}

expect_rc 0 identify "$bin" identify --json "$fixture_sea_bin"
diff -u tests/expected/fr_bridge_identify_sea_bin.json "$tmp/identify.out" || failures=$((failures + 1))

expect_rc 0 list "$bin" list --json "$fixture_sit"
diff -u tests/expected/fr_bridge_list_stuffit45.json "$tmp/list.out" || failures=$((failures + 1))

expect_rc 0 extract "$bin" extract --overwrite --output-dir "$tmp/out" "$fixture_sit"
grep -q '"status":"ok"' "$tmp/extract.out" || failures=$((failures + 1))
test -f "$tmp/out/testfile.txt" || failures=$((failures + 1))
cmp -s "$tmp/out/testfile.txt" reference_repos/stuffit-test-files/sources/testfile.txt || failures=$((failures + 1))

expect_rc 0 extract_selected "$bin" extract --overwrite --output-dir "$tmp/out_selected" --entry testfile.txt "$fixture_sit"
grep -q '"extracted_files":1' "$tmp/extract_selected.out" || failures=$((failures + 1))
test -f "$tmp/out_selected/testfile.txt" || failures=$((failures + 1))
test ! -e "$tmp/out_selected/testfile.jpg" || failures=$((failures + 1))

expect_rc 0 extract_selected_slash "$bin" extract --overwrite --output-dir "$tmp/out_selected_slash" --entry /testfile.png "$fixture_sit"
test -f "$tmp/out_selected_slash/testfile.png" || failures=$((failures + 1))
test ! -e "$tmp/out_selected_slash/testfile.txt" || failures=$((failures + 1))

expect_rc 4 extract_password_required "$bin" extract --overwrite --output-dir "$tmp/pw_required" "$fixture_pw"
expect_rc 4 extract_password_bad "$bin" extract --password wrong --overwrite --output-dir "$tmp/pw_bad" "$fixture_pw"
expect_rc 0 extract_password_ok "$bin" extract --password password --overwrite --output-dir "$tmp/pw_ok" "$fixture_pw"
test -f "$tmp/pw_ok/sources/testfile.txt" || failures=$((failures + 1))

expect_rc 2 sitx_unsupported "$bin" list --json "$fixture_sitx"
expect_rc 2 add_sit5_unsupported "$bin" add --base-dir reference_repos/stuffit-test-files/sources --entry testfile.txt "$fixture_pw"
expect_rc 2 delete_sit5_unsupported "$bin" delete --entry testfile.txt "$fixture_pw"

mkdir -p "$tmp/in"
printf "alpha\n" > "$tmp/in/alpha.txt"
printf "beta\n" > "$tmp/in/beta.txt"
ln -s alpha.txt "$tmp/in/alpha.link"

expect_rc 0 create "$bin" create --output "$tmp/new.sit" "$tmp/in/alpha.txt"
expect_rc 0 create_list "$bin" list --json "$tmp/new.sit"
grep -q '"path":"alpha.txt"' "$tmp/create_list.out" || failures=$((failures + 1))

expect_rc 0 add "$bin" add --base-dir "$tmp/in" --entry beta.txt "$tmp/new.sit"
expect_rc 0 add_list "$bin" list --json "$tmp/new.sit"
grep -q '"path":"beta.txt"' "$tmp/add_list.out" || failures=$((failures + 1))

expect_rc 0 add_update_skip "$bin" add --base-dir "$tmp/in" --update --entry alpha.txt "$tmp/new.sit"
grep -q '"skipped":1' "$tmp/add_update_skip.out" || failures=$((failures + 1))
sleep 1
touch "$tmp/in/alpha.txt"
expect_rc 0 add_update_apply "$bin" add --base-dir "$tmp/in" --update --entry alpha.txt "$tmp/new.sit"
grep -q '"added":1' "$tmp/add_update_apply.out" || failures=$((failures + 1))

expect_rc 0 delete "$bin" delete --entry beta.txt "$tmp/new.sit"
expect_rc 0 delete_list "$bin" list --json "$tmp/new.sit"
grep -q '"path":"alpha.txt"' "$tmp/delete_list.out" || failures=$((failures + 1))
if grep -q '"path":"beta.txt"' "$tmp/delete_list.out"; then
    failures=$((failures + 1))
fi

expect_rc 2 create_nofollow_symlink "$bin" create --output "$tmp/link-nofollow.sit" --no-follow-links "$tmp/in/alpha.link"
expect_rc 0 create_follow_symlink "$bin" create --output "$tmp/link-follow.sit" --follow-links "$tmp/in/alpha.link"
expect_rc 0 create_follow_list "$bin" list --json "$tmp/link-follow.sit"
grep -q '"path":"alpha.link"' "$tmp/create_follow_list.out" || failures=$((failures + 1))

mkdir -p "$tmp/in/dirpack/sub"
printf "nested\n" > "$tmp/in/dirpack/sub/nested.txt"

expect_rc 0 create_dir_tree "$bin" create --output "$tmp/dir-tree.sit" "$tmp/in/dirpack"
expect_rc 0 extract_dir_tree "$bin" extract --overwrite --output-dir "$tmp/out_dir_tree" "$tmp/dir-tree.sit"
test -d "$tmp/out_dir_tree/dirpack/sub" || failures=$((failures + 1))
test -f "$tmp/out_dir_tree/dirpack/sub/nested.txt" || failures=$((failures + 1))

expect_rc 0 add_dir "$bin" add --base-dir "$tmp/in" --entry dirpack "$tmp/new.sit"
expect_rc 0 add_dir_list "$bin" list --json "$tmp/new.sit"
grep -q '"path":"dirpack"' "$tmp/add_dir_list.out" || failures=$((failures + 1))
grep -q '"path":"dirpack/sub"' "$tmp/add_dir_list.out" || failures=$((failures + 1))
grep -q '"path":"dirpack/sub/nested.txt"' "$tmp/add_dir_list.out" || failures=$((failures + 1))

expect_rc 0 extract_dir_selected "$bin" extract --overwrite --output-dir "$tmp/out_dir_selected" --entry dirpack "$tmp/new.sit"
grep -q '"extracted_files":1' "$tmp/extract_dir_selected.out" || failures=$((failures + 1))
test -d "$tmp/out_dir_selected/dirpack/sub" || failures=$((failures + 1))
test -f "$tmp/out_dir_selected/dirpack/sub/nested.txt" || failures=$((failures + 1))
test ! -e "$tmp/out_dir_selected/alpha.txt" || failures=$((failures + 1))

expect_rc 0 delete_dir "$bin" delete --entry dirpack "$tmp/new.sit"
expect_rc 0 delete_dir_list "$bin" list --json "$tmp/new.sit"
if grep -q '"path":"dirpack' "$tmp/delete_dir_list.out"; then
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    exit 1
fi

echo "fr bridge tests ok"
