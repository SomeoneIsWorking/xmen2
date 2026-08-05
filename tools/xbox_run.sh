#!/usr/bin/env bash
#
# Run the recompiled Xbox build.
#
# The binary looks for its data at the RELATIVE path "game/default.xbe", so it
# must be started from a directory that has a `game` entry -- not from the
# build directory and not from the repo root. Running it from the wrong place
# prints "Cannot open XBE: game/default.xbe" and exits 1, which is easy to
# mistake for a build problem. This script makes the run directory and the
# symlink, so the invocation is not folklore.
#
#   BUILD   build directory        (default: scratch/build-xbox)
#   GAME    extracted game files   (default: scratch/xbox_iso)
#   RUNDIR  working directory      (default: scratch/run-xbox)
#   LOG     log file               (default: scratch/logs/xbox_run.log)
#
# Everything after `--` (or every argument, if there is no `--`) is passed to
# the binary. Environment variables the build reads:
#
#   XBOX_ICALL_WATCH=0x…,0x…   trace args+eax of those indirect-call targets
#   XBOX_ICALL_CONTINUE=1      survey past the first unresolved indirect call
#   XBOX_NATIVE_HEAP=0         use the recompiled MSVC heap instead (C056 repro)
#   XBOX_ABICHECK=0            turn off the callee-saved contract check
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$REPO/scratch/build-xbox}"
GAME="${GAME:-$REPO/scratch/xbox_iso}"
RUNDIR="${RUNDIR:-$REPO/scratch/run-xbox}"
LOG="${LOG:-$REPO/scratch/logs/xbox_run.log}"
EXE="$BUILD/xml2_xbox_recomp"

# Refuse rather than run into a confusing failure downstream.
[ -x "$EXE" ]  || { echo "xbox_run: no binary at $EXE -- build it first:" >&2
                    echo "  cmake --build $BUILD -j\$(nproc)" >&2; exit 1; }
[ -f "$GAME/default.xbe" ] || { echo "xbox_run: no default.xbe under $GAME." >&2
                    echo "  The XBE is copyrighted and gitignored; provide it yourself" >&2
                    echo "  or set GAME=/path/to/extracted/disc." >&2; exit 1; }

mkdir -p "$RUNDIR" "$(dirname "$LOG")"
ln -sfn "$GAME" "$RUNDIR/game"

cd "$RUNDIR"
echo "xbox_run: $EXE  (cwd $RUNDIR, game -> $GAME)"
echo "xbox_run: log $LOG"
set +e
"$EXE" "$@" 2>&1 | tee "$LOG"
rc=${PIPESTATUS[0]}
set -e
echo "xbox_run: exit $rc"
exit "$rc"
