#!/usr/bin/env bash
set -euo pipefail

dest=${1:-/tmp/openstuffit_install_api}
prefix=${2:-/usr}
pkg_dir="$dest$prefix/lib/pkgconfig"
lib_dir="$dest$prefix/lib"
cc_bin=${CC:-cc}

rm -rf "$dest"
make install DESTDIR="$dest" PREFIX="$prefix" >/dev/null

test -x "$dest$prefix/bin/openstuffit"
test -f "$dest$prefix/lib/libopenstuffit.a"
test -f "$dest$prefix/include/openstuffit/openstuffit.h"
test -f "$pkg_dir/openstuffit.pc"

if ! ls "$dest$prefix"/include/openstuffit/ost_*.h >/dev/null 2>&1; then
  :
else
  echo "internal headers were installed" >&2
  exit 1
fi

if command -v pkg-config >/dev/null 2>&1; then
  pkg_env=(
    PKG_CONFIG_PATH="$pkg_dir"
    PKG_CONFIG_SYSROOT_DIR="$dest"
    PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1
    PKG_CONFIG_ALLOW_SYSTEM_LIBS=1
  )
  env "${pkg_env[@]}" pkg-config --validate openstuffit
  cflags=$(env "${pkg_env[@]}" pkg-config --cflags openstuffit)
  dyn_flags=$(env "${pkg_env[@]}" pkg-config --cflags --libs openstuffit)
  static_pkg_flags=$(env "${pkg_env[@]}" pkg-config --static --libs openstuffit)
  case "$static_pkg_flags" in
    *-lz*) ;;
    *) echo "pkg-config --static missing -lz: $static_pkg_flags" >&2; exit 1 ;;
  esac
  static_flags="$cflags $lib_dir/libopenstuffit.a -lz"
else
  dyn_flags="-I$dest$prefix/include -L$lib_dir -lopenstuffit"
  static_flags="-I$dest$prefix/include $lib_dir/libopenstuffit.a -lz"
fi

"$cc_bin" -std=c99 -D_FILE_OFFSET_BITS=64 tests/test_library_api.c $dyn_flags \
  -Wl,-rpath,"$lib_dir" -o /tmp/openstuffit_install_shared_api
/tmp/openstuffit_install_shared_api

"$cc_bin" -std=c99 -D_FILE_OFFSET_BITS=64 tests/test_library_api.c $static_flags \
  -o /tmp/openstuffit_install_static_api
/tmp/openstuffit_install_static_api

make uninstall DESTDIR="$dest" PREFIX="$prefix" >/dev/null
test ! -e "$dest$prefix/bin/openstuffit"
test ! -e "$dest$prefix/lib/libopenstuffit.a"
test ! -e "$dest$prefix/include/openstuffit/openstuffit.h"
test ! -e "$pkg_dir/openstuffit.pc"

echo "install tests ok"
