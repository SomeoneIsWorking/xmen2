#!/usr/bin/env bash
#
# Loop until the native build stops finding functions static analysis missed.
#
#   tools/native_discover.sh [max-rounds]
#
# The CRT's static-constructor tables are the main source: their targets are
# referenced only by a data pointer in .rdata, so nothing in the Ghidra database
# points at them and no reference-driven pass can find them. x2native reports
# every missing target in one pass (module + GUEST address, because the modules
# are relocated and a seed has to name the address the module was LINKED for);
# this feeds them back and rebuilds until a round finds nothing.
#
# The PC counterpart of the Xbox discovery loop (I013, C040). Sequential on
# purpose: two analyzeHeadless runs against one Ghidra project corrupt it
# (issue #3).
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
MAX=${1:-8}
BIN=$ROOT/scratch/build-native/x2native
SPLIT=${SPLIT:-1500}

[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
: "${GAME_PC_DIR:?set GAME_PC_DIR in .env}"
[ -x "$BIN" ] || { echo "native_discover: $BIN is not built -- discovered NOTHING" >&2; exit 2; }

# Remembering the previous round's seed set, because a loop that keeps
# requesting the same address is not converging -- it is stuck, and saying
# "stopped after N rounds" would report that as a budget problem when it is a
# seeding failure. Observed: Ghidra refuses an address that falls INSIDE an
# already-detected function, so the seed silently does nothing and the round
# repeats forever.
PREV=""
round=0
while [ "$round" -lt "$MAX" ]; do
    round=$((round + 1))
    SEEDS=$ROOT/scratch/recomp/.discover.seeds
    "$BIN" --no-window >"$SEEDS.raw" 2>&1
    awk '/^    lib.*\.dll +0x/ {print $1, $2}' "$SEEDS.raw" | sort -u > "$SEEDS"

    if [ ! -s "$SEEDS" ]; then
        # Report the negative with its denominator: "nothing found" has to be
        # distinguishable from "the run died before it got that far".
        if grep -q "constructor targets" "$SEEDS.raw"; then
            echo "native_discover: round $round reported missing targets but none"
            echo "  parsed -- the report format changed and this loop is BLIND." >&2
            exit 1
        fi
        echo "native_discover: round $round found no missing constructor targets."
        echo "  The run got as far as it can on this axis; whatever stops it now"
        echo "  is not a function static analysis missed. Last output:"
        tail -3 "$SEEDS.raw"
        exit 0
    fi

    NOW=$(cat "$SEEDS")
    if [ "$NOW" = "$PREV" ]; then
        echo "native_discover: round $round asked for exactly the same targets as"
        echo "  the round before, so the seeding did NOT take. This is a stuck"
        echo "  loop, not a slow one. The usual cause is in the Ghidra log:" >&2
        grep -E "^ADD:" "$ROOT/scratch/logs/ghidra-"*.log 2>/dev/null | tail -3 >&2
        echo "  An address that falls inside an already-detected function needs" >&2
        echo "  the function SPLIT, which seeding cannot do." >&2
        exit 1
    fi
    PREV=$NOW
    echo "== round $round: $(wc -l < "$SEEDS") missing target(s) in $(cut -d' ' -f1 "$SEEDS" | sort -u | tr '\n' ' ')"
    cut -d' ' -f1 "$SEEDS" | sort -u | while read -r mod; do
        base=${mod%.dll}
        grep "^$mod " "$SEEDS" | awk '{print $2}' > "$ROOT/scratch/recomp/$base.seeds"
        tools/ghidra_export.sh "$base" --seed "$ROOT/scratch/recomp/$base.seeds" 2>&1 | tail -1
        rm -f "$ROOT/src/recomp/${base}_"[0-9][0-9][0-9].c "$ROOT/src/recomp/$base.c"
        python3 tools/recomp.py emit "$ROOT/scratch/recomp/$base.json" \
                "$ROOT/src/recomp/$base.c" --split "$SPLIT" | tail -1
        python3 tools/recomp.py native "$ROOT/scratch/recomp/$base.json" \
                "$ROOT/src/recomp/${base}_native.c" >/dev/null
    done
    cmake --build "$ROOT/scratch/build-native" --target x2native -j"$(nproc)" \
        >"$ROOT/scratch/logs/discover-build.log" 2>&1 \
        || { echo "native_discover: build failed, see scratch/logs/discover-build.log" >&2; exit 1; }
done
echo "native_discover: stopped after $MAX rounds with targets still outstanding." >&2
echo "  That is a cap, not a conclusion -- re-run to continue." >&2
exit 1
