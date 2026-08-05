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
# ONE loop at a time. Two of these racing share xbox/seeds.json, the generated
# C directory and the build directory, and the damage is not obvious: the
# rounds interleave in one log, a re-lift half-writes the JSON another re-lift
# is reading, and a link picks up an object file a stray compiler is still
# writing (which reads as "undefined reference to sub_xxxxxxxx", a translator
# bug that is not there). The lock below makes a second loop refuse and say who
# holds it, instead of quietly corrupting the first one.
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
XBOXRECOMP_FUNCS="${XBOXRECOMP_FUNCS:-$REPO/vendor/xboxrecomp/tools/disasm/output/functions.json}"
LOG="$REPO/scratch/logs/xbox_discover.log"
LOCK="$REPO/scratch/.xbox_discover.lock"

mkdir -p "$(dirname "$LOG")"
# `<>` not `>`: `>` truncates on open, which erases the holder line BEFORE
# flock reports the conflict -- the refusal would then name nobody.
touch "$LOCK"
exec 9<>"$LOCK"
if ! flock -n 9; then
    holder="$(cat "$LOCK" 2>/dev/null)"
    echo "xbox_discover: another discovery loop is already running." >&2
    echo "  lock: $LOCK  holder: ${holder:-<lock held but unnamed>}" >&2
    echo "  Two loops corrupt each other's seeds, lift and build. Wait for it," >&2
    echo "  or kill THAT pid (never pkill -f; it matches this shell too)." >&2
    exit 1
fi
: > "$LOCK"                                    # ours now; drop the stale holder
echo "pid $$ started $(date -Is)" >&9

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

    # A tail-jump miss is a translator gap, not a missing function. Seeding it
    # builds a function with no prologue out of a jump-table target in the
    # middle of another function, and the run continues corrupted. Check for it
    # BEFORE the seed-candidate grep, and stop.
    tail=$(grep -oE 'UNRESOLVED-TAIL-JUMP VA 0x[0-9A-F]+' "$out" | head -1 | grep -oE '0x[0-9A-F]+')
    if [ -n "$tail" ]; then
        echo "round $round: STOP -- first miss is a TAIL JUMP ($tail)." | tee -a "$LOG"
        echo "  That address is a switch-table target inside some function," | tee -a "$LOG"
        echo "  not a function the detector missed. Seeding it would make the" | tee -a "$LOG"
        echo "  symptom vanish and leave the run executing a fragment with no" | tee -a "$LOG"
        echo "  prologue. Fix the table: find the 'indirect tail jmp' fallback" | tee -a "$LOG"
        echo "  in the dispatching function's generated C, and see" | tee -a "$LOG"
        echo "  _analyze_switch_table in the lifter." | tee -a "$LOG"
        exit 1
    fi

    va=$(grep -oE '\bUNRESOLVED VA 0x[0-9A-F]+' "$out" | head -1 | grep -oE '0x[0-9A-F]+')
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

    # Refuse to seed an address that lands INSIDE a function we already
    # detected. Such an address is not a function the detector missed; it is a
    # jump-table target in the middle of one -- memcpy's unrolled copy tail is
    # full of them. Seeding it manufactures a "function" with no prologue: the
    # dispatch resolves, the fragment executes on the caller's frame, and the
    # run continues corrupted (a name lookup was handed a `this` pointing into
    # the stack). The symptom disappears and the port gets further while being
    # more wrong, which is the exact failure this loop must not produce.
    inside=$(python3 - "$XBOXRECOMP_FUNCS" "$va" <<'PYIN'
import json, sys

# functions.json is a LIST of records with hex-string start/end. Anything else
# means the format moved: say so and let the caller decide, rather than
# reporting "not mid-function" for a file this cannot read.
try:
    funcs = json.load(open(sys.argv[1]))
    if not isinstance(funcs, list):
        raise TypeError("expected a list of functions, got %s" % type(funcs).__name__)
except Exception as e:
    print("UNKNOWN %s: %s" % (sys.argv[1], e))
    sys.exit(0)

va = int(sys.argv[2], 16)
checked = 0
for f in funcs:
    try:
        start = int(f["start"], 16)
        end = int(f["end"], 16)
    except (KeyError, TypeError, ValueError):
        continue
    checked += 1
    if start < va < end:
        print("0x%08X 0x%08X" % (start, end))
        break
else:
    if checked == 0:
        print("UNKNOWN %s: no readable function records" % sys.argv[1])
PYIN
)
    case "$inside" in
        UNKNOWN*)
            echo "round $round: cannot check whether $va is mid-function:" | tee -a "$LOG"
            echo "  ${inside#UNKNOWN } -- seeding anyway, VERIFY IT BY HAND." | tee -a "$LOG";;
        0x*)
            set -- $inside
            echo "round $round: STOP -- $va is INSIDE the existing function $1..$2." | tee -a "$LOG"
            echo "  That is a jump-table target, not a missed function. Seeding it" | tee -a "$LOG"
            echo "  would build a function with no prologue and let the run continue" | tee -a "$LOG"
            echo "  corrupted. The real fix is to enumerate that function's jump" | tee -a "$LOG"
            echo "  table so the dispatch becomes a goto -- see _analyze_switch_table" | tee -a "$LOG"
            echo "  and the 'indirect tail jmp' fallbacks in its generated C." | tee -a "$LOG"
            exit 1;;
    esac

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

    if ! XBOX_DISCOVER_PID=$$ "$REPO/tools/xbox_relift.sh" >>"$LOG" 2>&1; then
        echo "round $round: RE-LIFT FAILED (a seed did not land) -- see $LOG" >&2
        exit 1
    fi
done

echo "stopped after $ROUNDS rounds without converging; still finding new targets" | tee -a "$LOG"
exit 1
