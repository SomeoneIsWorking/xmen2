#!/usr/bin/env bash
#
# Play a staged build on YOUR screen, with sound.
#
#   ./run.sh                 play scratch/run/all (staging it first if needed)
#   ./run.sh stock           play the untouched install, as a control
#   ./run.sh <rundir-name>   play an already-staged scratch/run/<name>
#
# This is the counterpart to tools/run_shim.sh, which is the HEADLESS harness:
# Xvfb, no sound, screenshots, a fixed timeout. That one is for measuring. This
# one is for looking, so it uses the real display, leaves the audio drivers
# alone, and runs until you close it.
#
# Environment (all optional):
#   EPS=<file|ALL>  entry points to recompile (default ALL). Ignored when the
#                   run directory already exists and REBUILD is not set.
#   REBUILD=1       rebuild even if the run directory is already staged
#   WATCH=1         build with the entry-point watch and the crash reporter;
#                   set X2_WATCH=all to trace, and read <rundir>/x2watch.log
#   X2_RES=WxH      virtual desktop size (default 800x600, the game's own mode)
#   X2_WINDOWED=0   run without the Wine virtual desktop (real fullscreen)
#
# Machine-specific paths live in .env only (see .env.example) -- nothing here
# assumes anything about where the game or the Wine prefix are.
set -u
cd "$(dirname "$0")"
ROOT=$PWD

NAME=${1:-all}
[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }

export WINEPREFIX=${WINEPREFIX:-${WINE_PREFIX:-}}
[ -n "$WINEPREFIX" ] || { echo "run: set WINE_PREFIX in .env (see .env.example)" >&2; exit 2; }
[ -d "$WINEPREFIX" ] || { echo "run: WINEPREFIX $WINEPREFIX does not exist" >&2; exit 2; }

# ---- pick what to play -------------------------------------------------
if [ "$NAME" = "stock" ]; then
    : "${GAME_PC_DIR:?set GAME_PC_DIR in .env (see .env.example)}"
    RUNDIR=$GAME_PC_DIR
    echo "run: playing the STOCK install (control) -- $RUNDIR"
else
    RUNDIR=$ROOT/scratch/run/$NAME
    if [ ! -f "$RUNDIR/XMen2.exe" ] || [ -n "${REBUILD:-}" ]; then
        echo "run: staging $NAME (EPS=${EPS:-ALL}) ..."
        RUNDIR_NAME=$NAME tools/build_recomp.sh "${EPS:-ALL}" || exit $?
    fi
    [ -f "$RUNDIR/XMen2.exe" ] || {
        echo "run: $RUNDIR/XMen2.exe missing -- nothing was staged, so nothing ran" >&2
        exit 2; }
    echo "run: playing $RUNDIR"
    echo "run:   libIGDisplay.dll is the recompiled one; libIGDisplay_orig.dll is the original"
fi

# ---- the same DLL overrides the headless harness needs -----------------
# d3d8 MUST be native (DXVK): this Wine build ships no builtin d3d8 at all and
# fails at import_dll without it. Audio is deliberately NOT disabled here --
# that is the one thing this script does differently from run_shim.sh.
: "${X2_D3D:=n}"
: "${X2_RES:=800x600}"

if [ "${X2_WINDOWED:-1}" = "1" ]; then
    # A Wine virtual desktop: the game asks for a fullscreen mode change, and
    # this satisfies it without taking over your screen.
    LAUNCH=(explorer "/desktop=x2,$X2_RES")
else
    LAUNCH=()
fi

cd "$RUNDIR" || exit 2
exec env WINEDLLOVERRIDES="d3d8,d3d9=$X2_D3D" \
     wine "${LAUNCH[@]}" "$(winepath -w ./XMen2.exe)" ${X2_ARGS:-}
