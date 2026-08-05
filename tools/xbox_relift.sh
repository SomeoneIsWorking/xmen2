#!/usr/bin/env bash
#
# Re-run the Xbox static-recompilation lift: disasm -> func_id -> recomp.
#
# This is the back half of the RUNTIME DISCOVERY LOOP:
#
#     run   ->  the binary prints an indirect-call target it could not resolve
#               ("[ICALL] UNRESOLVED VA 0x........ -- the call did NOT execute")
#     add   ->  that address goes into xbox/seeds.json with WHY it was invisible
#     lift  ->  this script; the seed makes the detector emit a function there
#     build ->  cmake --build "$BUILD_DIR"
#
# The loop exists because the static detector can only find a function that
# something references. A routine reached only as a function-POINTER argument
# (a thread start routine, a callback) has no static reference at all, so it is
# invisible until the running binary names it.
#
# Paths are repo-relative; override with the environment variables below.
#
#   XBE       path to the game's default.xbe   (default: $XBOXRECOMP/game_files/default.xbe)
#   SEEDS     seed function list               (default: xbox/seeds.json)
#   GEN_DIR   generated C output directory     (default: xbox/src/recomp/gen)
#   LOG_DIR   where stage logs are written     (default: scratch/logs)
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
XBOXRECOMP="$REPO/vendor/xboxrecomp"

XBE="${XBE:-$XBOXRECOMP/game_files/default.xbe}"
SEEDS="${SEEDS:-$REPO/xbox/seeds.json}"
GEN_DIR="${GEN_DIR:-$REPO/xbox/src/recomp/gen}"
LOG_DIR="${LOG_DIR:-$REPO/scratch/logs}"

# Refuse rather than produce an empty lift: a missing XBE or seed file must
# stop the run loudly, not yield a smaller function count that looks like
# progress. (The XBE is copyrighted and gitignored -- provide it yourself.)
for f in "$XBE" "$SEEDS"; do
    if [ ! -f "$f" ]; then
        echo "xbox_relift: required input missing: $f" >&2
        echo "  Nothing was lifted. Set XBE=/path/to/default.xbe if it lives elsewhere." >&2
        exit 1
    fi
done

mkdir -p "$LOG_DIR" "$GEN_DIR"

seed_count=$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))))' "$SEEDS")
echo "xbox_relift: XBE=$XBE"
echo "xbox_relift: seeding $seed_count runtime-discovered function(s) from $SEEDS"

cd "$XBOXRECOMP"

# NOT --text-only: this XBE has TEN executable sections, not one. Beyond
# .text there are D3D, DSOUND, WMADEC, PSFD00/_I/_B/_P, XONLINE, XNET, D3DX,
# XGRPH and XPP, together ~430 KB of shipped code ending at 0x0048EEC8. With
# --text-only the detector reported "functions_by_section: {.text: 21908}" and
# every one of those sections had ZERO functions lifted -- an indirect call
# into them could not resolve, and seeds landing there silently produced no
# function (which is what the seed check at the end of this script caught).
echo "== 1/3 disasm (function detection) =="
python3 -m tools.disasm "$XBE" --force \
    --seed-functions "$SEEDS" 2>&1 | tee "$LOG_DIR/xbox_relift_disasm.log" | tail -20

echo "== 2/3 func_id (categorisation) =="
python3 -m tools.func_id "$XBE" 2>&1 | tee "$LOG_DIR/xbox_relift_funcid.log" | tail -10

echo "== 3/3 recomp (x86 -> C) =="
# No --header: that flag is an exclusive mode that writes the declaration
# header and exits WITHOUT translating anything. Split mode emits its own
# recomp_funcs.h alongside the chunks.
python3 -m tools.recomp "$XBE" --all --split 1000 \
    --gen-dir "$GEN_DIR" 2>&1 | tee "$LOG_DIR/xbox_relift_recomp.log" | tail -20

echo
echo "xbox_relift: seeded addresses now in the dispatch table:"
missing=0
harvest_missing=0
while read -r addr src; do
    # recomp_dispatch.c holds the address as a hex literal; a seed that does
    # not appear there was NOT lifted, and the loop has not closed.
    if grep -qi "0x${addr}" "$GEN_DIR/recomp_dispatch.c"; then
        : # present; only the misses are worth a line at this volume
    elif [ "$src" = "harvest" ]; then
        # Inferred from a vtable-shaped run, not seen being called. A few
        # false positives are expected and are not a failure.
        echo "  0x${addr}  not a function (harvested candidate, no match)"
        harvest_missing=$((harvest_missing + 1))
    else
        echo "  0x${addr}  MISSING -- a RUNTIME-OBSERVED seed did not land" >&2
        missing=1
    fi
done < <(python3 -c '
import json, sys
for e in json.load(open(sys.argv[1])):
    print(e["start"][2:].upper().zfill(8), e.get("source", "observed"))
' "$SEEDS")

total=$(python3 -c 'import json,sys;print(len(json.load(open(sys.argv[1]))))' "$SEEDS")
echo "xbox_relift: $total seeds, $harvest_missing harvested candidates did not resolve to a function"

[ "$missing" -eq 0 ] || { echo "xbox_relift: a runtime-observed seed did not land." >&2; exit 1; }
echo "xbox_relift: done. Rebuild with: cmake --build <build-dir>"
