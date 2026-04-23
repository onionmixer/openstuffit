#!/usr/bin/env bash
set -u

bin=${1:-build/openstuffit}
out=${2:-tests/reports/xad_compare_latest.md}

mkdir -p "$(dirname "$out")"

lsar_bin=${LSAR:-}
unar_bin=${UNAR:-}
if [ -z "$lsar_bin" ]; then
    lsar_bin=$(command -v lsar 2>/dev/null || true)
fi
if [ -z "$unar_bin" ]; then
    unar_bin=$(command -v unar 2>/dev/null || true)
fi
if [ -z "$lsar_bin" ] && [ -x reference_repos/XADMaster/lsar ]; then
    lsar_bin=reference_repos/XADMaster/lsar
fi
if [ -z "$unar_bin" ] && [ -x reference_repos/XADMaster/unar ]; then
    unar_bin=reference_repos/XADMaster/unar
fi

fixtures=(
    reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit
    reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sea.bin
    reference_repos/stuffit-test-files/build/testfile.stuffit45_dlx.mac9.sit.hqx
    reference_repos/stuffit-test-files/build/testfile.stuffit7.win.sit
)

make_manifest() {
    root=$1
    strip_first=$2
    can_strip=no
    if [ "$strip_first" = "yes" ]; then
        top_count=$(find "$root" -type f ! -name '*.rsrc' ! -name '._*' -printf '%P\n' |
            awk 'index($0,"/")==0{root=1} index($0,"/")>0{split($0,a,"/"); seen[a[1]]=1} END{n=0; for(k in seen)n++; if(root) print 0; else print n}')
        if [ "$top_count" = "1" ]; then
            can_strip=yes
        fi
    fi
    find "$root" -type f ! -name '*.rsrc' ! -name '._*' -print0 |
        while IFS= read -r -d '' file; do
            rel=${file#"$root"/}
            if [ "$can_strip" = "yes" ] && [ "${rel#*/}" != "$rel" ]; then
                rel=${rel#*/}
            fi
            hash=$(sha256sum "$file" | awk '{print $1}')
            size=$(stat -c '%s' "$file")
            printf '%s\t%s\t%s\n' "$hash" "$size" "$rel"
        done | sort
}

{
    printf '# XADMaster Comparison Report\n\n'
    printf 'Generated: `%s`\n\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf -- '- openstuffit: `%s`\n' "$bin"
    printf -- '- lsar: `%s`\n' "${lsar_bin:-not found}"
    printf -- '- unar: `%s`\n\n' "${unar_bin:-not found}"

    if [ ! -x "$bin" ]; then
        printf 'Status: `skip`, openstuffit binary missing.\n'
        exit 0
    fi
    if [ -z "$lsar_bin" ] || [ -z "$unar_bin" ]; then
        printf 'Status: `skip`, XADMaster command line tools are not installed in PATH.\n'
        exit 0
    fi

    printf 'Status: `ok`, comparison commands executed.\n\n'
    printf '| Fixture | openstuffit list rc | lsar rc | openstuffit extract rc | unar rc | data SHA256 manifest |\n'
    printf '| --- | --- | --- | --- | --- | --- |\n'
    for fixture in "${fixtures[@]}"; do
        ost_list="${TMPDIR:-/tmp}/openstuffit_xad_ost_list_$$.txt"
        xad_list="${TMPDIR:-/tmp}/openstuffit_xad_xad_list_$$.txt"
        ost_out=$(mktemp -d "${TMPDIR:-/tmp}/openstuffit_xad_ost_out.XXXXXX")
        xad_out=$(mktemp -d "${TMPDIR:-/tmp}/openstuffit_xad_xad_out.XXXXXX")
        ost_manifest="${TMPDIR:-/tmp}/openstuffit_xad_ost_manifest_$$.txt"
        xad_manifest="${TMPDIR:-/tmp}/openstuffit_xad_xad_manifest_$$.txt"
        xad_manifest_raw="${TMPDIR:-/tmp}/openstuffit_xad_xad_manifest_raw_$$.txt"

        "$bin" list -L "$fixture" >"$ost_list" 2>/dev/null
        ost_list_rc=$?
        "$lsar_bin" -L "$fixture" >"$xad_list" 2>/dev/null
        xad_list_rc=$?
        "$bin" extract --overwrite -o "$ost_out" "$fixture" >/dev/null 2>/dev/null
        ost_extract_rc=$?
        "$unar_bin" -q -o "$xad_out" "$fixture" >/dev/null 2>/dev/null
        xad_extract_rc=$?
        manifest_status="skip"
        if [ "$ost_extract_rc" -eq 0 ] && [ "$xad_extract_rc" -eq 0 ]; then
            make_manifest "$ost_out" no > "$ost_manifest"
            make_manifest "$xad_out" yes > "$xad_manifest"
            make_manifest "$xad_out" no > "$xad_manifest_raw"
            if cmp -s "$ost_manifest" "$xad_manifest" || cmp -s "$ost_manifest" "$xad_manifest_raw"; then
                manifest_status="match"
            else
                manifest_status="diff"
            fi
        fi

        printf '| `%s` | `%s` | `%s` | `%s` | `%s` | `%s` |\n' \
            "$fixture" "$ost_list_rc" "$xad_list_rc" "$ost_extract_rc" "$xad_extract_rc" "$manifest_status"

        rm -f "$ost_list" "$xad_list" "$ost_manifest" "$xad_manifest" "$xad_manifest_raw"
        rm -rf "$ost_out" "$xad_out"
    done
} > "$out"

echo "wrote $out"
