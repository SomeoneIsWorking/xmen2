#!/usr/bin/env bash
#
# Build a HYBRID libIG*.dll: some functions recompiled from our own C, the rest
# forwarded to the original, and stage a run directory for it.
#
#   tools/build_recomp.sh [<eps-file>] [<module>]
#
#   <eps-file>  entry points to recompile, one 0x… per line, '#' comments
#               allowed. Default: scratch/recomp/verified.eps
#               Pass the literal word ALL to recompile every translatable
#               function (this is the build that page-faults -- see
#               tools/bisect_recomp.sh, which exists to find out why).
#   <module>    module base name. Default: libIGDisplay
#
# This used to be a sequence of commands run by hand, which is why the
# recompiled set sat at one number for as long as it did: growing it meant
# remembering four generator invocations and a compiler line, and any of them
# could be got subtly wrong without saying so. The loop that grows the set is
# the core capability of this whole direction -- it has to be one command.
#
# Environment:
#   RUNDIR_NAME  run directory under scratch/run (default: recomp)
#   OPT          optimisation level for the recompiled C (default: -O1)
#
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD

EPS=${1:-scratch/recomp/verified.eps}
MOD=${2:-libIGDisplay}
RUNDIR_NAME=${RUNDIR_NAME:-recomp}
OPT=${OPT:--O1}

[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
: "${GAME_PC_DIR:?set GAME_PC_DIR in .env (see .env.example)}"

JSON=$ROOT/scratch/recomp/$MOD.json
GEN=$ROOT/src/recomp
RUN=$ROOT/scratch/run/$RUNDIR_NAME
OUTDLL=$ROOT/scratch/recomp/$MOD.dll
EXPORTS=$ROOT/scratch/recomp/$MOD.exports.txt

# Refuse rather than build something smaller that looks like progress. A
# missing JSON means the Ghidra export never ran for this module; a missing
# eps file would otherwise be read as "recompile nothing" and produce a DLL
# that is a pure forwarder while claiming to be a recomp build.
[ -f "$JSON" ] || { echo "build_recomp: no $JSON -- run the Ghidra export for $MOD first" >&2; exit 2; }
[ -f "$GAME_PC_DIR/$MOD.dll" ] || { echo "build_recomp: no $MOD.dll in \$GAME_PC_DIR" >&2; exit 2; }
if [ "$EPS" != "ALL" ] && [ ! -f "$EPS" ]; then
    echo "build_recomp: entry-point file '$EPS' does not exist." >&2
    echo "  Nothing was built. Pass ALL to recompile everything translatable." >&2
    exit 2
fi

mkdir -p "$GEN" "$RUN" "$ROOT/scratch/recomp"

# The export table is a MEASUREMENT of the original DLL, re-taken every build:
# it decides which exports get a shim and which are forwarded, so a stale copy
# silently changes the boundary.
python3 tools/pe.py exports "$GAME_PC_DIR/$MOD.dll" > "$EXPORTS"

# Steps 1 and 2 are a pure function of the JSON -- the entry-point set only
# reaches step 3. A bisection runs step 3 hundreds of times and must not pay
# for re-translating 521 functions each round, so it can skip them once they
# are newer than their input. Correctness is not on the honour system: the
# staleness test is against the JSON, so editing the translator still
# regenerates.
regen=1
if [ "${SKIP_GEN:-0}" = "1" ] \
   && [ "$GEN/$MOD.c" -nt "$JSON" ] && [ "$GEN/${MOD}_rtd.c" -nt "$JSON" ] \
   && [ "$GEN/$MOD.c" -nt tools/recomp.py ] && [ "$GEN/${MOD}_rtd.c" -nt tools/recomp.py ]; then
    regen=0
    echo "== 1-2/4 emit+runtime SKIPPED (SKIP_GEN=1; both newer than $MOD.json and recomp.py) =="
fi
if [ "$regen" = 1 ]; then
    echo "== 1/4 emit (x86 -> C) =="
    python3 tools/recomp.py emit "$JSON" "$GEN/$MOD.c" | tail -1

    echo "== 2/4 runtime (dispatch table, no import stubs) =="
    python3 tools/recomp.py runtime "$JSON" "$GEN/${MOD}_rtd.c" nostubs | tail -1
fi

echo "== 3/4 dll (export shims + import thunks) =="
if [ "$EPS" = "ALL" ]; then
    unset RECOMP_ONLY || true
    python3 tools/recomp.py dll "$JSON" "$GEN/${MOD}_dll.c" \
        "$ROOT/scratch/recomp/${MOD}_dll.def" "$EXPORTS" "${MOD}_orig"
else
    RECOMP_ONLY="$EPS" python3 tools/recomp.py dll "$JSON" "$GEN/${MOD}_dll.c" \
        "$ROOT/scratch/recomp/${MOD}_dll.def" "$EXPORTS" "${MOD}_orig"
fi

echo "== 4/4 compile =="
i686-w64-mingw32-gcc -shared "$OPT" -o "$OUTDLL" \
    "$ROOT/scratch/recomp/${MOD}_dll.def" \
    "$GEN/$MOD.c" "$GEN/${MOD}_dll.c" "$GEN/${MOD}_rtd.c" \
    -I "$GEN" -static-libgcc
[ -f "$OUTDLL" ] || { echo "build_recomp: compile produced no $OUTDLL" >&2; exit 1; }

# Stage: a symlink farm over the real install with this ONE dll replaced, so
# the game directory is never written to.
for e in "$GAME_PC_DIR"/*; do
    b=$(basename "$e")
    [ "$b" = "$MOD.dll" ] && continue
    [ "$b" = "alchemy.ini" ] && continue
    ln -sfn "$e" "$RUN/$b"
done
cp "$GAME_PC_DIR/$MOD.dll" "$RUN/${MOD}_orig.dll"
cp "$OUTDLL" "$RUN/$MOD.dll"
# FSAA off: llvmpipe cannot multisample headless.
sed 's/^multiSampleType = 4/multiSampleType = 0/' \
    "$GAME_PC_DIR/alchemy.ini" > "$RUN/alchemy.ini"

echo "build_recomp: staged $RUN ($MOD.dll -> ${MOD}_orig.dll), $(stat -c%s "$OUTDLL") bytes"
echo "  run it:  tools/run_shim.sh $RUNDIR_NAME 30"
