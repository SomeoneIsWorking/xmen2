#!/usr/bin/env bash
# Fetch the upstream Xbox static recompiler we build on.
#
# NOT vendored into git: once we start MODIFYING it we need our own fork, and a
# copied-in tree would silently diverge from upstream with no way to rebase.
# Until that fork exists this script pins the source of truth.
set -eu
cd "$(dirname "$0")/.."
[ -d vendor/xboxrecomp ] && { echo "vendor/xboxrecomp already present"; exit 0; }
mkdir -p vendor
git clone https://github.com/sp00nznet/xboxrecomp.git vendor/xboxrecomp
echo "cloned. Pipeline:"
echo "  python3 -m tools.xbe_parser game_files/default.xbe --json game_files/default_analysis.json"
echo "  python3 -m tools.disasm     game_files/default.xbe --text-only"
echo "  python3 -m tools.func_id    game_files/default.xbe -v"
echo "  python3 -m tools.recomp     game_files/default.xbe --all --split 1000"
