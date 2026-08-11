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
RUN=${RUN-1}
# Extra x2native arguments for the discovery run.
#
# Defaults to --d3d8 because that IS the live path now: it answers
# Direct3DCreate8 with a host IDirect3D8, so the run gets through renderer
# init, resource loading and input and keeps reporting missing targets the
# whole way. Under any argument set that stops earlier the loop converges on
# "nothing found" while every target past the stop stays invisible -- which is
# a loop that reports success for not having looked.
#
# --vk (the ARK-substitution path) and RUN_ARGS= (un-substituted, stops at
# Direct3DCreate8) are still selectable; each has its own blind spot, and the
# convergence message names which one was used.
#
# NAMED RUN_ARGS, not X2_ARGS: X2_ARGS is the runtime's ARGUMENT WATCH (an
# entry-point list read by src/native/x86rt_native.c). This script used to take
# the same name for the run's COMMAND LINE, so exporting the watch turned an
# entry-point list into a command-line argument and x2native refused it as an
# unknown option -- two instruments, one name, and the collision only appears
# when both are in use.
if [ -n "${X2_ARGS:-}" ] && case ${X2_ARGS} in -*) false;; *) true;; esac; then
    echo "native_discover: X2_ARGS is set to '$X2_ARGS', which is the runtime"
    echo "  ARGUMENT WATCH (entry points), not this script's run arguments."
    echo "  It is passed through to the run as an environment variable and NOT"
    echo "  used as a command line. Use RUN_ARGS=... for that." >&2
fi
RUN_ARGS=${RUN_ARGS---d3d8}

[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
: "${GAME_PC_DIR:?set GAME_PC_DIR in .env}"
[ -x "$BIN" ] || { echo "native_discover: $BIN is not built -- discovered NOTHING" >&2; exit 2; }

# Refuse to seed against an export that does not describe the shipped binary.
# Seeding addresses into the wrong image produces functions at meaningless
# places, and the loop would happily converge on them (issue #12).
python3 tools/verify_export.py >/dev/null 2>&1 || {
    echo "native_discover: an export does not match its shipped binary --" >&2
    python3 tools/verify_export.py >&2
    exit 2
}

# Before the per-round loop: seed every function whose address appears as a CODE
# Bulk seeding, from two static sources, before the loop runs at all.
#
# RELOCATIONS are the complete one and the one that came last: every absolute
# address baked into a relocatable image has a base relocation entry, because
# the loader must fix it up if the image moves -- so the relocation values that
# land in an executable section ARE every absolute code pointer in the module,
# with no run-length threshold, no alignment assumption and no .rdata/.data
# distinction. Measured: both targets the loop found in msdia80 one round at a
# time were already sitting in .reloc, and one seeding pass created 1382
# functions there -- ten-odd rounds' worth, in one step.
#
# The older source is a callback handed to a registrar as an instruction
# IMMEDIATE (`push <addr>; call [registrar]`), in bulk.
#
# The loop below learns ONE function per round, because the runtime stops at the
# first dispatch target it cannot resolve and each round costs a Ghidra
# re-analysis, a re-emit and a relink. On libIGGfx that found exactly one per
# round for eight rounds and was still going. Those addresses are callbacks
# handed to a registrar, and nothing branches to them, so no reference-driven
# pass can see them -- but they are trivially findable in the instruction
# stream. One pass found 138 in libIGGfx and 988 in XMen2.exe.
#
# The list is DERIVED from what has been exported, not written out by hand.
# It used to be a hand-kept subset, and the modules missing from it -- libIGOpt,
# libIGGui, libIGLua, libMovie, libCriMovie -- were exactly the ones that then
# crawled through the round loop one function at a time: eleven rounds of
# "1 missing target in libIGOpt.dll", each costing a Ghidra re-analysis and a
# relink, for addresses a single bulk pass finds instantly. A list that has to
# be updated by hand every time a module joins the build is a list that will be
# wrong again.
#
# The `.ark`/`.vtab`/`.iat` side-car exports are not modules and are skipped by
# name; anything else with a .json gets seeded.
#
# Set SKIP_BULK=1 to go straight to the loop.
if [ "${SKIP_BULK:-0}" != "1" ]; then
    BULK_MODS=$(ls "$ROOT"/scratch/recomp/*.json 2>/dev/null \
                | sed 's|.*/||; s|\.json$||' \
                | grep -v '\.\(ark\|vtab\|iat\)$')
    [ -n "$BULK_MODS" ] || {
        echo "native_discover: no exports in scratch/recomp -- there is NOTHING" >&2
        echo "  to bulk-seed, and that is a broken tree, not an empty result." >&2
        exit 2; }
    echo "== bulk: seeding $(echo "$BULK_MODS" | wc -l) exported module(s)"
    for m in $BULK_MODS; do
        J=$ROOT/scratch/recomp/$m.json
        [ -f "$J" ] || continue
        changed=0
        # RELOCATIONS first -- the complete source, and the one that needs no
        # heuristic (see tools/seed_relocs.py). A module with no relocation
        # directory makes it REFUSE, which is not an error here: the loop says
        # so and moves on to the sources that do apply.
        R=$ROOT/scratch/recomp/$m.reloc
        RL=$ROOT/scratch/recomp/$m.reloc.log
        if python3 tools/seed_relocs.py "$J" -o "$R" >"$RL" 2>&1; then
            n=$(sed -n 's/.*CANDIDATE function starts: //p' "$RL")
            if [ "${n:-0}" -gt 0 ]; then
                echo "== bulk: $m has $n relocation-derived candidate(s) to seed"
                tools/ghidra_export.sh "$m" --seed "$R" 2>&1 |
                    grep -E '^ADD:|functions,' | tail -2
                changed=1
            fi
        else
            echo "== bulk: $m -- no relocation seeding: $(tail -1 "$RL")"
            # A /FIXED image (XMen2.exe) has no relocation table, so the
            # complete enumeration is not available and the pointers have to be
            # recognised BY VALUE instead -- see tools/seed_data_ptrs.py, which
            # refuses on any image where seed_relocs.py applies. Without it the
            # exe produced exactly one new indirect-call target per round, each
            # costing a Ghidra re-analysis, a re-emit and a relink.
            D=$ROOT/scratch/recomp/$m.dataptr
            DL=$ROOT/scratch/recomp/$m.dataptr.log
            if python3 tools/seed_data_ptrs.py "$J" -o "$D" >"$DL" 2>&1; then
                n=$(sed -n 's/.*CANDIDATE function starts: //p' "$DL")
                if [ "${n:-0}" -gt 0 ]; then
                    echo "== bulk: $m has $n data-pointer candidate(s) to seed"
                    tools/ghidra_export.sh "$m" --seed "$D" 2>&1 |
                        grep -E '^ADD:|functions,' | tail -2
                    changed=1
                fi
            else
                echo "== bulk: $m -- no data-pointer seeding: $(tail -1 "$DL")"
            fi
        fi
        S=$ROOT/scratch/recomp/$m.codeimm
        n=$(python3 tools/seed_code_imms.py "$J" -o "$S" \
            | sed -n 's/.*NEW function starts to seed: //p')
        if [ "${n:-0}" -gt 0 ]; then
            echo "== bulk: $m has $n code-immediate function start(s) to seed"
            tools/ghidra_export.sh "$m" --seed "$S" 2>&1 | tail -1
            changed=1
        fi
        if [ "$changed" = 1 ]; then
            rm -f "$ROOT/src/recomp/${m}_"[0-9][0-9][0-9].c "$ROOT/src/recomp/$m.c"
            python3 tools/recomp.py emit "$J" "$ROOT/src/recomp/$m.c" \
                    --split "$SPLIT" | tail -1
            python3 tools/recomp.py native "$J" \
                    "$ROOT/src/recomp/${m}_native.c" >/dev/null
        fi
    done
    cmake --build "$ROOT/scratch/build-native" --target x2native -j"$(nproc)" \
        >"$ROOT/scratch/logs/discover-build.log" 2>&1 \
        || { echo "native_discover: bulk-seed rebuild failed, see scratch/logs/discover-build.log" >&2; exit 1; }
fi

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
    # --run by default: the exe's own CRT startup has constructor tables too,
    # and stopping at module init would leave them undiscovered. RUN=0 limits
    # the loop to module initialisation.
    # BOUNDED. The run used to be unbounded, and once the game got far enough
    # to reach its own main loop it simply never returned: the loop sat on one
    # round for fifty minutes, and when that run was killed by hand the round
    # parsed no seeds and reported CONVERGENCE. "The run found nothing" and
    # "the run never finished" have to be different answers.
    # X2_HEARTBEAT on purpose: the convergence test below reads the present
    # counts out of this log, so a run with no heartbeat would be judged as
    # "not presenting" and reported as a spin. X2_UNPACED because a discovery
    # round has no reason to wait for the game's 60fps cap -- it sees more of
    # the game per second of wall clock, and the clock the game reads is
    # unchanged.
    X2_HEARTBEAT=${X2_HEARTBEAT:-5} X2_UNPACED=${X2_UNPACED:-1} \
    timeout -k 10 "${RUN_TIMEOUT:-300}" \
        "$BIN" --no-window ${RUN:+--run} ${RUN_ARGS:-} >"$SEEDS.raw" 2>&1
    rc=$?
    awk '/^    [A-Za-z0-9_]+\.(dll|exe) +0x/ {print $1, $2}' "$SEEDS.raw" \
        | sort -u > "$SEEDS"

    if [ ! -s "$SEEDS" ]; then
        # Report the negative with its denominator: "nothing found" has to be
        # distinguishable from "the run died before it got that far".
        if grep -q "constructor targets" "$SEEDS.raw"; then
            echo "native_discover: round $round reported missing targets but none"
            echo "  parsed -- the report format changed and this loop is BLIND." >&2
            exit 1
        fi
        # 124 is timeout(1); 137 is a SIGKILL from anywhere. Either way this
        # round did not finish looking, so it cannot report that it looked.
        if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ] || [ "$rc" -eq 143 ]; then
            # A killed run used to be one answer. It is now two, and the
            # heartbeat separates them: the game reaches a frame loop and holds
            # it, so "still going when the clock ran out" is the NORMAL outcome
            # of a healthy run, not a hang. A run that is still PRESENTING
            # FRAMES looked as far as this path goes; one that is not is
            # spinning and has to be reported as such.
            if grep -qE "presents [0-9]+ \(\+[1-9]" "$SEEDS.raw"; then
                echo "native_discover: round $round found no missing constructor targets"
                echo "  on the path taken by: $BIN --no-window ${RUN:+--run} ${RUN_ARGS:-}"
                echo "  The run was still RENDERING when the ${RUN_TIMEOUT:-300}s limit"
                echo "  hit -- it did not stop, it was not spinning, and it reported"
                echo "  no missing target in that time. The blind spot is TIME: a"
                echo "  target the game only reaches later in a session was never"
                echo "  executed. Raise RUN_TIMEOUT to look further. Last frames:"
                grep -E "presents [0-9]+ \(\+" "$SEEDS.raw" | tail -2
                exit 0
            fi
            echo "native_discover: round $round did NOT converge -- the run was" >&2
            echo "  still going after ${RUN_TIMEOUT:-300}s and was killed (exit $rc)," >&2
            echo "  and it was NOT presenting frames, so it was not merely long:" >&2
            echo "  it is spinning or blocked. It found no missing targets UP TO" >&2
            echo "  THAT POINT, which is not the same as there being none." >&2
            grep -v '^\[TRACE\]' "$SEEDS.raw" | tail -5 >&2
            exit 2
        fi
        echo "native_discover: round $round found no missing constructor targets"
        echo "  on the path taken by: $BIN --no-window ${RUN:+--run} ${RUN_ARGS:-}"
        echo "  That is the blind spot to keep in mind -- targets reachable only"
        echo "  under OTHER arguments were never executed and so never reported."
        echo "  Whatever stops the run now is not a function static analysis"
        echo "  missed -- the run ended by itself (exit $rc). Last output:"
        grep -v '^\[TRACE\]' "$SEEDS.raw" | tail -3
        exit 0
    fi

    NOW=$(cat "$SEEDS")
    if [ "$NOW" = "$PREV" ]; then
        echo "native_discover: round $round asked for exactly the same targets as"
        echo "  the round before, so the seeding did NOT take. This is a stuck"
        echo "  loop, not a slow one. What Ghidra said about THESE seeds:" >&2
        # Only the logs of the modules in this round's seed set. Globbing
        # ghidra-*.log printed a STALE log from a different module -- XMen2's,
        # while the seed being diagnosed was libIGUtils' -- which reads as an
        # explanation and is not one.
        for mod in $(cut -d' ' -f1 "$SEEDS" | sort -u); do
            base=${mod%.dll}; base=${base%.exe}
            log=$ROOT/scratch/logs/ghidra-$base.log
            if [ ! -f "$log" ]; then
                echo "    $base: NO ghidra log at $log, so nothing is known about" >&2
                echo "      why its seed did not take" >&2
                continue
            fi
            echo "    --- $base ($log)" >&2
            grep -E "^ADD:" "$log" 2>/dev/null | tail -4 | sed 's/^/      /' >&2
        done
        echo "  Two causes look different in that output and need different fixes:" >&2
        echo "    'already inside a function' -- needs a SPLIT, which seeding" >&2
        echo "      cannot do; the escalation below handles it." >&2
        echo "    'did NOT disassemble -- it may be data' -- the address is not" >&2
        echo "      code in THAT module. Usually the target was never a real" >&2
        echo "      address: check whether it is an unrelocated linked address" >&2
        echo "      belonging to a module mapped elsewhere (C093)." >&2
        exit 1
    fi
    PREV=$NOW
    echo "== round $round: $(wc -l < "$SEEDS") missing target(s) in $(cut -d' ' -f1 "$SEEDS" | sort -u | tr '\n' ' ')"
    cut -d' ' -f1 "$SEEDS" | sort -u | while read -r mod; do
        base=${mod%.dll}; base=${base%.exe}
        grep "^$mod " "$SEEDS" | awk '{print $2}' > "$ROOT/scratch/recomp/$base.seeds"
        tools/ghidra_export.sh "$base" --seed "$ROOT/scratch/recomp/$base.seeds" 2>&1 | tail -1
        # Seeding cannot create a function at an address Ghidra has already
        # swallowed into an earlier one -- it reports "already inside a
        # function" and does nothing, which is what made this loop spin. Those
        # need a SPLIT, so escalate automatically instead of stalling.
        SPLITS=$ROOT/scratch/recomp/$base.autosplit
        grep -E "^ADD: 0x0x[0-9a-f]+ already inside a function" \
            "$ROOT/scratch/logs/ghidra-$base.log" 2>/dev/null \
            | sed 's/^ADD: 0x//; s/ already.*//' | sort -u > "$SPLITS"
        if [ -s "$SPLITS" ]; then
            # A split CARVES a function that analysis already found, on the
            # assumption that the runtime is right and the database is wrong.
            # That is sometimes true (commit 8b15fec) and sometimes exactly
            # backwards (issue #21, where this carved one SEH-protected
            # function into five pieces across three sessions and each piece's
            # RET then popped part of the exception frame).
            #
            # So it must not carve SILENTLY. Say which function is about to be
            # cut and show its first instructions: an MSVC SEH prologue
            # (`PUSH -1; PUSH <handler>; MOV EAX,FS:[0]`) means the seed is
            # almost certainly a handler or scope-table pointer that only
            # LOOKS like a call target, and the split will make things worse.
            echo "   escalating $(grep -c . "$SPLITS") seed(s) to a split --"
            echo "   each of these CARVES an existing function. Check the"
            echo "   containing function's first instructions before believing"
            echo "   the result; an SEH prologue there means DO NOT split"
            echo "   (issue #21):"
            python3 tools/whose_function.py \
                "$ROOT/scratch/recomp/$base.json" "$SPLITS" || true
            tools/ghidra_export.sh "$base" --split-at "$SPLITS" 2>&1 \
                | grep -E "^SPLIT: [0-9]+ split|^ghidra_export:" | tail -2
        fi
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
