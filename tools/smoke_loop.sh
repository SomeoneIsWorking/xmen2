#!/usr/bin/env bash
#
# The end-to-end gate: does the game still go all the way round?
#
#   tools/smoke_loop.sh [timeout-seconds]
#
# Drives one headless run through main menu -> New Game -> difficulty ->
# cutscene skipped -> level load -> gameplay -> the party dies -> the death
# dialog -> back to the main menu, and checks what came out.
#
# The input is scheduled by FRAMES PRESENTED, not by the wall clock. That is
# not a detail: two runs of the same time-scheduled script on this machine
# reached frame 2639 at 140.03s and at 106.44s, a 25% spread, so a script tuned
# on one run misses on the next and the run diverges with nothing reporting a
# failure. Frames are the game's own progress and hold across machines.
#
# WHAT THIS CHECKS
#   1. every scripted key press fired -- a run that deadlocks stops presenting,
#      so the frame-scheduled events after the stall never happen;
#   2. the run reached the last one, which is only reachable through a level;
#   3. no draw was refused, no instruction was unsupported, nothing aborted;
#   4. the final frame is not blank.
#
# WHAT THIS DOES NOT CHECK, and must not be read as checking: that the picture
# is CORRECT. It cannot tell the main menu from any other lit scene. A wrong
# colour, a missing character or an inside-out mesh all pass this. Compare the
# screenshot it leaves in scratch/screenshots/ by eye, or use the per-subsystem
# self-tests (--vk-selftest, --d3d8-selftest), which check pixels against
# values they compute.
set -uo pipefail
cd "$(dirname "$0")/.."

TIMEOUT=400
SELFTEST=0
case "${1:-}" in
    --selftest) SELFTEST=1 ;;
    "")         ;;
    *)          TIMEOUT=$1 ;;
esac

fail=0
say() { echo "  $*"; }

check_run() {   # $1 = log, $2 = screenshot, $3 = expected press count
LOG=$1; SHOT=$2; want=$3
# 1 + 2. Every scripted press, in order.
got=$(grep -c 'INJECTING' "$LOG")
if [ "$got" -ne "$want" ]; then
    say "FAIL: $got of $want scripted key press(es) fired. A run that stalls"
    say "      stops presenting, so the frame-scheduled events after it never"
    say "      happen -- the last one that DID fire is where it got to:"
    grep 'INJECTING' "$LOG" | tail -1 | sed 's/^/        /'
    fail=1
else
    say "ok: all $want scripted key press(es) fired, the last at $(grep 'INJECTING' "$LOG" | tail -1 | sed 's/.*frame /frame /')"
fi

# 3. Nothing refused, nothing untranslated, nothing aborted.
# ANCHORED to the start of a line, because these names appear in prose too:
# the shutdown report explains that x86_note_fallback is what would abort, and
# an unanchored grep matched that explanation and failed a passing run. A
# diagnostic that matches its own description is a diagnostic that cries wolf.
for pat in 'x86_unsupported_insn:' 'x86_fallthrough:' 'x86_after_noreturn:' \
           'x86_call_unknown' 'x86_dispatch: no recompiled body' \
           'x86_note_fallback:'; do
    if grep -q "^$pat" "$LOG"; then
        say "FAIL: $pat -- the run hit a translator or dispatch stop:"
        grep -m1 -A2 "$pat" "$LOG" | sed 's/^/        /'
        fail=1
    fi
done
ref=$(grep -oE 'refused [0-9]+' "$LOG" | tail -1 | grep -oE '[0-9]+' || echo 0)
if [ "${ref:-0}" -ne 0 ]; then
    say "FAIL: $ref draw(s) were refused by the GPU backend; the picture is"
    say "      missing whatever they were."
    fail=1
else
    say "ok: no draw refused"
fi

# 4. The final frame is not blank. Read as bytes rather than eyeballed: a
#    P6 file whose pixels are all one value is a frame that drew nothing, and
#    that is the failure this run is most likely to produce silently.
if [ ! -s "$SHOT" ]; then
    say "FAIL: no screenshot was written, so NOTHING is known about the picture"
    fail=1
else
    distinct=$(tail -c +16 "$SHOT" | od -An -v -tu1 -w3 | sort -u | head -3000 | wc -l)
    if [ "$distinct" -lt 50 ]; then
        say "FAIL: the final frame has only $distinct distinct colour(s) -- it is"
        say "      blank or nearly so ($SHOT)"
        fail=1
    else
        say "ok: the final frame has $distinct+ distinct colours ($SHOT)"
    fi
fi

}

# --selftest: feed the checks a log for each failure and require each to be
# caught. A gate nobody has seen fail is a gate nobody should trust -- and this
# one DID pass a broken run once, because an unanchored grep matched the
# shutdown report's own description of x86_note_fallback. Needs no game.
if [ "$SELFTEST" -eq 1 ]; then
    # scratch/, never /tmp: /tmp here is a RAM-backed tmpfs with a per-user
    # quota that logs and dumps fill in a run or two.
    T=scratch/smoke-selftest; rm -rf "$T"; mkdir -p "$T"; rc=0
    good_shot=$T/good.ppm
    printf 'P6\n2 1\n255\n' > "$good_shot"
    python3 - "$good_shot" <<'EOP'
import sys
# 3000 distinct colours in a 3000x1 image, so the "not blank" check passes.
w = 3000
with open(sys.argv[1], 'wb') as f:
    f.write(b'P6\n%d 1\n255\n' % w)
    f.write(bytes(b for i in range(w) for b in (i % 251, (i * 7) % 253, (i * 13) % 249)))
EOP
    blank_shot=$T/blank.ppm
    python3 - "$blank_shot" <<'EOP'
import sys
w = 3000
with open(sys.argv[1], 'wb') as f:
    f.write(b'P6\n%d 1\n255\n' % w)
    f.write(b'\x10\x20\x30' * w)
EOP
    ok_log=$T/ok.log
    { for i in 1 2 3 4 5 6; do echo "DINPUT8: INJECTING \"Return\" at t=1s, frame $i"; done
      echo "gpu draws 100 refused 0"; } > "$ok_log"

    expect() {  # name, expected-fail(0/1), log, shot, want
        # NOT $( ), which runs check_run in a SUBSHELL and throws away the
        # `fail` it sets -- the first version of this selftest reported every
        # expect-a-failure case as "returned 0", which is the selftest failing
        # to test rather than the checks failing to fire. Redirection keeps it
        # in this shell.
        fail=0
        check_run "$3" "$4" "$5" > "$T/out.txt" 2>&1
        if [ "$fail" -ne "$2" ]; then
            echo "  SELFTEST FAIL: '$1' -- the checks returned fail=$fail, expected $2"
            sed 's/^/      /' "$T/out.txt"
            rc=1
        else
            echo "  selftest ok: $1"
        fi
    }
    echo "== smoke_loop --selftest: the checks must be able to FAIL =="
    expect "a clean run passes"            0 "$ok_log"   "$good_shot" 6
    cp "$ok_log" "$T/short.log"
    sed -i '4,6d' "$T/short.log"
    expect "a stalled run (3 of 6 presses)" 1 "$T/short.log" "$good_shot" 6
    { cat "$ok_log"; echo "x86_unsupported_insn: reached guest 0x1"; } > "$T/insn.log"
    expect "an unsupported instruction"     1 "$T/insn.log"  "$good_shot" 6
    { cat "$ok_log"; echo "x86_note_fallback: 0x1"; } > "$T/fb.log"
    expect "a hybrid fallback"              1 "$T/fb.log"    "$good_shot" 6
    { for i in 1 2 3 4 5 6; do echo "DINPUT8: INJECTING \"Return\" at t=1s, frame $i"; done
      echo "gpu draws 100 refused 7"; } > "$T/ref.log"
    expect "refused draws"                  1 "$T/ref.log"   "$good_shot" 6
    expect "a blank final frame"            1 "$ok_log"      "$blank_shot" 6
    # The false positive that shipped: the report NAMES x86_note_fallback.
    { cat "$ok_log"
      echo "  recomp: NO original machine code ran. ... aborts by name (x86_note_fallback), so ..."; } > "$T/prose.log"
    expect "the report's own prose is not a failure" 0 "$T/prose.log" "$good_shot" 6
    rm -rf "$T"
    [ "$rc" -eq 0 ] && echo "== smoke_loop --selftest: PASSED ==" \
                    || echo "== smoke_loop --selftest: FAILED =="
    exit "$rc"
fi

[ -f .env ] || { echo "smoke_loop: no .env -- copy .env.example and fill in GAME_PC_DIR" >&2; exit 2; }
set -a; . ./.env; set +a
[ -n "${GAME_PC_DIR:-}" ] || { echo "smoke_loop: GAME_PC_DIR is unset; NOTHING was run" >&2; exit 2; }
BIN=scratch/build-native/x2native
[ -x "$BIN" ] || { echo "smoke_loop: $BIN is not built; NOTHING was run" >&2; exit 2; }

mkdir -p scratch/logs scratch/screenshots
RUNLOG=scratch/logs/smoke_loop.log
RUNSHOT=scratch/screenshots/smoke_loop.ppm
rm -f "$RUNSHOT"

# Frame numbers measured on a run that closes the loop. They are a property of
# the GAME (how many frames its menus and its load take), not of this machine.
SCRIPT="f2639+40:Return,f2815+40:Return,f3182+40:Escape,f3204+40:Escape,f4044+40:Down,f4135+40:Return"

echo "== smoke_loop: one full run, up to ${TIMEOUT}s =="
X2_INPUT_SCRIPT="$SCRIPT" X2_SHOT="$RUNSHOT" X2_SHOT_EVERY=10 \
X2_UNPACED=1 X2_HEARTBEAT=60 \
    timeout "$TIMEOUT" "$BIN" --no-window --d3d8 --run > "$RUNLOG" 2>&1
RC=$?

fail=0
check_run "$RUNLOG" "$RUNSHOT" "$(printf '%s' "$SCRIPT" | tr ',' '\n' | grep -c .)"

if [ "$RC" -ne 124 ] && [ "$RC" -ne 0 ]; then
    say "note: the run exited $RC. 124 is the timeout ending a run that was"
    say "      still going, which is the expected end -- this game does not"
    say "      stop on its own."
fi

if [ "$fail" -ne 0 ]; then
    echo "== smoke_loop: FAILED -- see $RUNLOG =="
    exit 1
fi
echo "== smoke_loop: PASSED -- the loop closed. This says the run got round;"
echo "   it does NOT say the picture is right. Look at $RUNSHOT. =="
