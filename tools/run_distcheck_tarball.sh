#!/usr/bin/env bash
set -euo pipefail

tarball=${1:?usage: run_distcheck_tarball.sh <tarball>}
work=/tmp/openstuffit_distcheck_tarball

rm -rf "$work"
mkdir -p "$work"
tar -C "$work" -xzf "$tarball"
src=$(find "$work" -mindepth 1 -maxdepth 1 -type d | head -n 1)

make -C "$src" all
make -C "$src" test

echo "tarball distcheck ok"
