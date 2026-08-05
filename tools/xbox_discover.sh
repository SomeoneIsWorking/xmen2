#!/usr/bin/env bash
#
# Drive the runtime discovery loop to a fixed point.
#
#   run -> the binary stops at the first indirect call it cannot resolve
#       -> if the target is inside the image, add it to xbox/seeds.json
#       -> re-lift, rebuild, run again
#
# Stops when a run resolves every indirect call, when the same address comes
# back twice (seeding it did not help -- a real problem to look at), or after
# MAX_ROUNDS. Every outcome is printed; a quiet exit is not one of them.
#
#   ROUNDS    maximum iterations (default 8)
#   BUILD_DIR cmake build directory (default scratch/build-xbox)
#   RUN_DIR   directory holding default.xbe + game/ (default scratch/run/xbox)
#
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROUNDS="${ROUNDS:-8}"
BUILD_DIR="${BUILD_DIR:-$REPO/scratch/build-xbox}"
RUN_DIR="${RUN_DIR:-$REPO/scratch/run/xbox}"
SEEDS="$REPO/xbox/seeds.json"
LOG="$REPO/scratch/logs/xbox_discover.log"

: > "$LOG"
seen=""

for round in $(seq 1 "$ROUNDS"); do
    echo "=== round $round: build + run ===" | tee -a "$LOG"
    if ! make -C "$BUILD_DIR" -j"$(nproc)" xml2_xbox_recomp >>"$LOG" 2>&1; then
        echo "round $round: BUILD FAILED -- see $LOG" >&2
        exit 1
    fi

    out="$REPO/scratch/logs/xbox_discover_run_$round.log"
    ( cd "$RUN_DIR" && timeout 300 "$BUILD_DIR/xml2_xbox_recomp" ) >"$out" 2>&1
    status=$?

    calls=$(grep -oE '^\[ICALL\] [0-9]+ indirect calls' "$out" | grep -oE '[0-9]+' | head -1)
    # The per-call kernel trace is capped at 200 lines, so counting those
    # lines would report 200 for every run past the cap. Take the total the
    # binary prints instead, and say so if it is missing.
    kernel=$(grep -oE '^\[KERNEL\] [0-9]+ kernel calls total' "$out" \
             | grep -oE '[0-9]+' | head -1)
    kernel="${kernel:-unreported}"
    echo "round $round: exit=$status, ${calls:-0} indirect calls, $kernel kernel calls" | tee -a "$LOG"

    va=$(grep -oE 'UNRESOLVED VA 0x[0-9A-F]+' "$out" | head -1 | grep -oE '0x[0-9A-F]+')
    if [ -z "$va" ]; then
        skipped=$(grep -oE 'range-skipped VA 0x[0-9A-F]+' "$out" | head -1 | grep -oE '0x[0-9A-F]+')
        if [ -n "$skipped" ]; then
            echo "round $round: STOP -- first miss is OUT-OF-IMAGE ($skipped)." | tee -a "$LOG"
            echo "  That is a garbage function pointer, not a missing seed: the" | tee -a "$LOG"
            echo "  game state was already wrong. Seeding cannot fix it." | tee -a "$LOG"
        else
            echo "round $round: DONE -- every indirect call resolved." | tee -a "$LOG"
        fi
        exit 0
    fi

    case " $seen " in
        *" $va "*)
            echo "round $round: STOP -- $va came back after being seeded." | tee -a "$LOG"
            echo "  The seed did not produce a dispatchable function; look at it" | tee -a "$LOG"
            echo "  by hand rather than looping." | tee -a "$LOG"
            exit 1;;
    esac
    seen="$seen $va"

    echo "round $round: seeding $va" | tee -a "$LOG"
    python3 - "$SEEDS" "$va" "$round" <<'PY'
import json, sys
path, va, rnd = sys.argv[1], sys.argv[2], sys.argv[3]
d = json.load(open(path))
if any(e["start"].lower() == va.lower() for e in d):
    sys.exit(0)
d.append({"start": va, "name": f"icall_{va[2:]}",
          "why": f"Indirect-call target with no static reference, found by "
                 f"tools/xbox_discover.sh round {rnd} from the ICALL miss tally."})
open(path, "w").write(json.dumps(d, indent=2) + "\n")
PY

    if ! "$REPO/tools/xbox_relift.sh" >>"$LOG" 2>&1; then
        echo "round $round: RE-LIFT FAILED (a seed did not land) -- see $LOG" >&2
        exit 1
    fi
done

echo "stopped after $ROUNDS rounds without converging; still finding new targets" | tee -a "$LOG"
exit 1
