#!/usr/bin/env bash
#
# Bring one more shipped module into the recompiled set.
#
#   tools/add_module.sh <module-basename> [<module-basename> ...]
#
# Adding libMovie took five commands in a fixed order, one of which (generating
# the .iat) is easy to forget -- and forgetting it does not fail quietly:
# `recomp.py emit` refuses, saying the coverage number would be meaningless
# without it. That refusal is why the omission was caught, and it is exactly the
# kind of sequence that should be a command rather than something remembered.
#
# The steps, and why each is here:
#   1. ghidra_export.sh   PE -> functions + instructions, with vtable seeding
#   2. pe.py iat          the import table, so import calls resolve by name
#   3. seed_relocs.py     EVERY absolute code pointer in the image, read out of
#                         its own relocation table -- the linker had to record
#                         each one, so this needs no heuristic and misses no
#                         vtable, dispatch table or lone callback field
#   4. seed_code_imms.py  callbacks passed as code immediates (nothing points
#                         at them, so no reference-driven pass finds them)
#   5. re-export          only if (3) or (4) found any, to pick them up
#   6. recomp.py emit     the bodies
#   7. recomp.py native   the dispatch table and weak import stubs
#
# It does NOT edit CMakeLists.txt: the module list there is a deliberate
# statement of what the build links, and a script silently extending it is how
# a module ends up in a binary nobody decided to put it in. The line to add is
# printed instead.
#
# Sequential on purpose: two analyzeHeadless runs against one Ghidra project
# corrupt it (issue #3).
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
SPLIT=${SPLIT:-1500}

[ $# -ge 1 ] || { sed -n '2,8p' "$0"; exit 2; }
[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
: "${GAME_PC_DIR:?set GAME_PC_DIR in .env}"

added=()
failed=()
for MOD in "$@"; do
    DLL=$GAME_PC_DIR/$MOD.dll
    [ -f "$DLL" ] || DLL=$GAME_PC_DIR/$MOD.exe
    if [ ! -f "$DLL" ]; then
        echo "add_module: neither $MOD.dll nor $MOD.exe is in \$GAME_PC_DIR --"
        echo "  nothing was added for it" >&2
        failed+=("$MOD")
        continue
    fi
    echo "=== $MOD"
    J=$ROOT/scratch/recomp/$MOD.json

    SEED_TABLES=1 tools/ghidra_export.sh "$MOD" 2>&1 | tail -1 || {
        failed+=("$MOD"); continue; }
    [ -s "$J" ] || { echo "add_module: $MOD produced no JSON" >&2
                     failed+=("$MOD"); continue; }

    python3 tools/pe.py iat "$DLL" > "$ROOT/scratch/recomp/$MOD.iat" || {
        echo "add_module: could not read $MOD's import table" >&2
        failed+=("$MOD"); continue; }

    # Relocation-derived code pointers FIRST: it is the complete source, and
    # every function it creates is one the later passes no longer have to
    # guess at. It refuses (rather than reporting an empty result) for an
    # image with no relocation directory, which an EXE linked /FIXED is, so a
    # failure here is not fatal to the module -- but it is printed.
    R=$ROOT/scratch/recomp/$MOD.reloc
    if python3 tools/seed_relocs.py "$J" -o "$R" > "$ROOT/scratch/recomp/$MOD.reloc.log" 2>&1; then
        n=$(sed -n 's/.*CANDIDATE function starts: //p' "$ROOT/scratch/recomp/$MOD.reloc.log")
        echo "   $n relocation-derived candidate(s)"
        if [ "${n:-0}" -gt 0 ]; then
            tools/ghidra_export.sh "$MOD" --seed "$R" 2>&1 | grep -E '^ADD:|functions,' | tail -2
        fi
    else
        echo "   no relocation-derived seeding for $MOD:"
        sed 's/^/     /' "$ROOT/scratch/recomp/$MOD.reloc.log"
    fi

    S=$ROOT/scratch/recomp/$MOD.codeimm
    n=$(python3 tools/seed_code_imms.py "$J" -o "$S" \
        | sed -n 's/.*NEW function starts to seed: //p')
    if [ "${n:-0}" -gt 0 ]; then
        echo "   $n code-immediate function start(s); re-exporting with them"
        tools/ghidra_export.sh "$MOD" --seed "$S" 2>&1 | tail -1
    fi

    rm -f "$ROOT/src/recomp/${MOD}_"[0-9][0-9][0-9].c "$ROOT/src/recomp/$MOD.c"
    python3 tools/recomp.py emit "$J" "$ROOT/src/recomp/$MOD.c" \
            --split "$SPLIT" 2>&1 | tail -1 || { failed+=("$MOD"); continue; }
    python3 tools/recomp.py native "$J" \
            "$ROOT/src/recomp/${MOD}_native.c" 2>&1 | tail -1 || {
        failed+=("$MOD"); continue; }
    added+=("$MOD")
done

echo
if [ ${#added[@]} -gt 0 ]; then
    echo "add_module: generated output for: ${added[*]}"
    echo "  Add them to X2_MODULES in CMakeLists.txt to LINK them -- this script"
    echo "  deliberately does not, so that what the binary contains stays a"
    echo "  decision someone made:"
    echo "      set(X2_MODULES ... ${added[*]} ...)"
else
    echo "add_module: nothing was generated." >&2
fi
if [ ${#failed[@]} -gt 0 ]; then
    echo "add_module: FAILED for: ${failed[*]}" >&2
    exit 1
fi
