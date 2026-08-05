#!/usr/bin/env bash
#
# Find which recompiled entry points break the game.
#
#   tools/bisect_recomp.sh [<good-eps>] [<module>]
#
# Delta-debugging over the entry-point set. The GOOD set is known to run; the
# candidates are every translatable function not already in it. The bisection
# adds half the candidates at a time and asks the real game whether it still
# runs, narrowing to the smallest addition that still fails.
#
# Why a script and not a session of manual runs: this is the loop that grows
# the recompiled set, for every module, forever. Done by hand it is ~9 rounds
# of four generator invocations each, and one mistyped round poisons the whole
# result without saying so.
#
# The verdict is the REAL GAME, not a unit test: a build passes only if the
# game image loads, the process is still alive at the end, and the frame is not
# uniform. Each of those three has been the one that mattered at some point --
# a DLL that loads and instantly dies still "loads", and a run that survives
# with a black screen still "survives".
#
#   ROUNDS_LOG  where the per-round record goes (default scratch/logs/bisect)
#   SECS        seconds per game run (default 25)
#
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD

GOOD=${1:-scratch/recomp/verified.eps}
MOD=${2:-libIGDisplay}
SECS=${SECS:-25}
WORK=$ROOT/scratch/bisect
LOGDIR=${ROUNDS_LOG:-$ROOT/scratch/logs/bisect}
JSON=$ROOT/scratch/recomp/$MOD.json

[ -f "$GOOD" ] || { echo "bisect_recomp: no good set at $GOOD -- bisected NOTHING" >&2; exit 2; }
[ -f "$JSON" ] || { echo "bisect_recomp: no $JSON -- bisected NOTHING" >&2; exit 2; }
mkdir -p "$WORK" "$LOGDIR"

# Every translatable entry point, from the same JSON the build uses.
python3 - "$JSON" > "$WORK/all.eps" <<'PY'
import json, sys
sys.path.insert(0, "tools")
import recomp
d = recomp.load(sys.argv[1])
for fn in d["functions"]:
    if recomp.translate(fn)[0] is not None:
        print("0x%08x" % fn["ep"])
PY

sort -u "$GOOD" | grep -v '^#' | grep . > "$WORK/good.eps"
sort -u "$WORK/all.eps" > "$WORK/all.sorted"
comm -13 "$WORK/good.eps" "$WORK/all.sorted" > "$WORK/cand.eps"

NGOOD=$(wc -l < "$WORK/good.eps")
NCAND=$(wc -l < "$WORK/cand.eps")
echo "bisect: good=$NGOOD  candidates=$NCAND  (total $((NGOOD + NCAND)))"
[ "$NCAND" -gt 0 ] || { echo "bisect: nothing to add -- the good set is already everything"; exit 0; }

round=0

# Build the good set + the candidates listed in $1, run the game, and answer
# PASS/FAIL. Prints the three signals every time, including on a pass, so a
# clean round is something the log states rather than something it omits.
try() {
    local add=$1 tag=$2
    round=$((round + 1))
    cat "$WORK/good.eps" "$add" | sort -u > "$WORK/try.eps"
    local n; n=$(wc -l < "$WORK/try.eps")
    printf 'round %2d [%s]: %d entry points ... ' "$round" "$tag" "$n"

    if ! SKIP_GEN=1 RUNDIR_NAME=bisect "$ROOT/tools/build_recomp.sh" \
            "$WORK/try.eps" "$MOD" > "$LOGDIR/build.$round.log" 2>&1; then
        echo "BUILD FAILED (see $LOGDIR/build.$round.log)"
        return 2
    fi
    "$ROOT/tools/run_shim.sh" bisect "$SECS" > "$LOGDIR/run.$round.log" 2>&1
    local verdict; verdict=$(grep '^RUN:' "$LOGDIR/run.$round.log" || true)
    local loaded alive uniform
    loaded=$(echo "$verdict" | grep -o 'game_image_loaded=[a-z]*' | cut -d= -f2)
    alive=$(echo "$verdict"  | grep -o 'wrapper_alive=[a-z]*'     | cut -d= -f2)
    if grep -q 'ALL SAMPLES UNIFORM' "$LOGDIR/run.$round.log"; then uniform=yes; else uniform=no; fi

    cp "$WORK/try.eps" "$LOGDIR/try.$round.eps"
    if [ "$loaded" = yes ] && [ "$alive" = yes ] && [ "$uniform" = no ]; then
        echo "PASS (loaded=$loaded alive=$alive uniform=$uniform)"
        return 0
    fi
    echo "FAIL (loaded=${loaded:-?} alive=${alive:-?} uniform=$uniform)"
    return 1
}

# Sanity: the good set must actually pass and the full set must actually fail.
# Without both, a bisection is theatre -- it would "converge" on whatever the
# first split happened to be. This has to be measured, not assumed.
echo "--- control: the good set alone must PASS"
: > "$WORK/empty.eps"
if ! try "$WORK/empty.eps" "control-good"; then
    echo "bisect: the GOOD set does not pass. Nothing was bisected -- fix the" >&2
    echo "  baseline first, or the search has no fixed point to work from." >&2
    exit 3
fi
echo "--- control: the full set must FAIL"
if try "$WORK/cand.eps" "control-all"; then
    echo "bisect: the FULL set PASSES. There is nothing to bisect --" >&2
    echo "  promote it: cp $WORK/try.eps $GOOD" >&2
    exit 0
fi

# Outer loop: find a culprit, exclude it, and go again. One bisection answers
# "which function breaks the good set"; it does NOT answer "which set is the
# largest that works", because the culprits are independent -- removing the
# first one from the full set still failed here, on the real game. So the
# search repeats until what remains passes, and the exclusions accumulate.
: > "$WORK/excluded.eps"
cp "$WORK/cand.eps" "$WORK/pool.eps"

while : ; do
    if [ ! -s "$WORK/pool.eps" ]; then
        echo
        echo "bisect: every candidate ended up excluded -- the good set could"
        echo "  not be grown at all. Exclusions: $WORK/excluded.eps"
        exit 5
    fi
    if try "$WORK/pool.eps" "pool $(wc -l < "$WORK/pool.eps")"; then
        cat "$WORK/good.eps" "$WORK/pool.eps" | sort -u > "$WORK/grown.eps"
        echo
        echo "bisect: DONE. $(wc -l < "$WORK/grown.eps") entry points run the game;"
        echo "  $(wc -l < "$WORK/excluded.eps") excluded and still forwarded to the original."
        echo "  grown set : $WORK/grown.eps"
        echo "  exclusions: $WORK/excluded.eps"
        sed 's/^/    /' "$WORK/excluded.eps"
        exit 0
    fi

    # Narrow this pool to one culprit.
    cp "$WORK/pool.eps" "$WORK/cur.eps"
    stuck=0
    while [ "$(wc -l < "$WORK/cur.eps")" -gt 1 ]; do
        n=$(wc -l < "$WORK/cur.eps")
        half=$(( (n + 1) / 2 ))
        head -n "$half" "$WORK/cur.eps" > "$WORK/lo.eps"
        tail -n +"$((half + 1))" "$WORK/cur.eps" > "$WORK/hi.eps"
        if try "$WORK/lo.eps" "lo $half/$n"; then
            if try "$WORK/hi.eps" "hi $((n - half))/$n"; then
                # Neither half breaks it alone: the failure needs a combination.
                # Say so rather than blaming whichever function the split landed
                # on, and stop -- a single-culprit search cannot go further.
                echo
                echo "bisect: both halves of the remaining $n pass alone but their"
                echo "  union fails, so this failure needs more than one function"
                echo "  together. Halves: $WORK/lo.eps and $WORK/hi.eps"
                echo "  Excluded so far: $(wc -l < "$WORK/excluded.eps")"
                stuck=1
                break
            fi
            cp "$WORK/hi.eps" "$WORK/cur.eps"
        else
            cp "$WORK/lo.eps" "$WORK/cur.eps"
        fi
    done
    [ "$stuck" = 1 ] && exit 4

    CULPRIT=$(cat "$WORK/cur.eps")
    echo "$CULPRIT" >> "$WORK/excluded.eps"
    grep -v -x "$CULPRIT" "$WORK/pool.eps" > "$WORK/pool.next" || true
    mv "$WORK/pool.next" "$WORK/pool.eps"
    printf 'bisect: culprit #%d = %s  ' "$(wc -l < "$WORK/excluded.eps")" "$CULPRIT"
    python3 - "$JSON" "$CULPRIT" <<'PY_INNER'
import sys
sys.path.insert(0, "tools")
import recomp
d = recomp.load(sys.argv[1])
ep = int(sys.argv[2], 16)
for fn in d["functions"]:
    if fn["ep"] == ep:
        print("%s (%d instrs)" % (fn["qname"], len(fn["ins"])))
        break
else:
    print("(not in the function database)")
PY_INNER
    echo "bisect: $(wc -l < "$WORK/pool.eps") candidate(s) left; going again"
done
