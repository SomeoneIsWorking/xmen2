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
# --vk (the ARK-substitution path) and X2_ARGS= (un-substituted, stops at
# Direct3DCreate8) are still selectable; each has its own blind spot, and the
# convergence message names which one was used.
X2_ARGS=${X2_ARGS---d3d8}

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
# Set SKIP_BULK=1 to go straight to the loop.
if [ "${SKIP_BULK:-0}" != "1" ]; then
    for m in libIGDisplay libIGCore libIGSg libIGMath libIGAttrs \
             libIGGfx libIGUtils XMen2; do
        J=$ROOT/scratch/recomp/$m.json
        [ -f "$J" ] || continue
        S=$ROOT/scratch/recomp/$m.codeimm
        n=$(python3 tools/seed_code_imms.py "$J" -o "$S" \
            | sed -n 's/.*NEW function starts to seed: //p')
        if [ "${n:-0}" -gt 0 ]; then
            echo "== bulk: $m has $n code-immediate function start(s) to seed"
            tools/ghidra_export.sh "$m" --seed "$S" 2>&1 | tail -1
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
    "$BIN" --no-window ${RUN:+--run} ${X2_ARGS:-} >"$SEEDS.raw" 2>&1
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
        echo "native_discover: round $round found no missing constructor targets"
        echo "  on the path taken by: $BIN --no-window ${RUN:+--run} ${X2_ARGS:-}"
        echo "  That is the blind spot to keep in mind -- targets reachable only"
        echo "  under OTHER arguments were never executed and so never reported."
        echo "  Whatever stops the run now is not a function static analysis"
        echo "  missed. Last output:"
        tail -3 "$SEEDS.raw"
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
