#!/usr/bin/env bash
set -euo pipefail

pc=${1:-build/openstuffit.pc}

if ! command -v pkg-config >/dev/null 2>&1; then
  echo "pkg-config not found; skipping"
  exit 0
fi

pkg-config --validate "$pc"

dynamic_libs=$(PKG_CONFIG_PATH="$(dirname "$pc")" pkg-config --libs openstuffit)
static_libs=$(PKG_CONFIG_PATH="$(dirname "$pc")" pkg-config --static --libs openstuffit)
cflags=$(PKG_CONFIG_PATH="$(dirname "$pc")" pkg-config --cflags openstuffit)

case "$dynamic_libs" in
  *-lopenstuffit*) ;;
  *) echo "pkg-config dynamic libs missing -lopenstuffit: $dynamic_libs" >&2; exit 1 ;;
esac

case "$static_libs" in
  *-lz*) ;;
  *) echo "pkg-config static libs missing -lz: $static_libs" >&2; exit 1 ;;
esac

case "$cflags" in
  *include*) ;;
  *) echo "pkg-config cflags missing include path: $cflags" >&2; exit 1 ;;
esac

echo "pkg-config tests ok"
