#!/usr/bin/env bash
#
# Run a build on YOUR screen, with sound.
#
#   ./run.sh                 the NATIVE build (x2native) -- no Wine
#   ./run.sh native          the same, said explicitly
#   ./run.sh wine            the Wine oracle with our recompiled DLL swapped in
#   ./run.sh stock           the untouched install under Wine, as the control
#   ./run.sh <rundir-name>   an already-staged scratch/run/<name> under Wine
#
# The default is the native build because that is the live front. The Wine
# paths are kept, not deprecated: `stock` is the control every rendering
# question is settled against, and `wine` is still the only configuration that
# actually draws the game. Losing either would cost more than the convenience
# of a shorter command.
#
# This is the counterpart to tools/run_shim.sh, which is the HEADLESS harness:
# Xvfb, no sound, screenshots, a fixed timeout. That one is for measuring. This
# one is for looking, so it uses the real display, leaves the audio drivers
# alone, and runs until you close it.
#
# Environment (all optional):
#   REBUILD=1       rebuild before running
#   X2_ARGS=...     extra arguments passed through to the game / to x2native
#
#   native only:
#     BUILD=<dir>   build directory (default scratch/build-native)
#     TRACE=1       configure with -DX2_NATIVE_TRACE=ON (every body into the ring)
#
#   wine/stock/<name> only:
#     EPS=<file|ALL>  entry points to recompile (default ALL). Ignored when the
#                     run directory already exists and REBUILD is not set.
#     WATCH=1       build with the entry-point watch and the crash reporter;
#                   set X2_WATCH=all to trace, and read <rundir>/x2watch.log
#     X2_RES=WxH    virtual desktop size (default 800x600, the game's own mode)
#     X2_WINDOWED=0 run without the Wine virtual desktop (real fullscreen)
#
# Machine-specific paths live in .env only (see .env.example) -- nothing here
# assumes anything about where the game or the Wine prefix are.
set -u
cd "$(dirname "$0")"
ROOT=$PWD

NAME=${1:-native}
[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }

# ======================================================================
# the native build
# ======================================================================
if [ "$NAME" = "native" ]; then
    : "${GAME_PC_DIR:?set GAME_PC_DIR in .env (see .env.example)}"
    [ -d "$GAME_PC_DIR" ] || {
        echo "run: GAME_PC_DIR is set to '$GAME_PC_DIR', which is not a" >&2
        echo "     directory. The native build maps the shipped PE images at" >&2
        echo "     startup, so it cannot run without the real install." >&2
        exit 2; }

    BUILD=${BUILD:-$ROOT/scratch/build-native}

    # The recompiler output is GENERATED and gitignored. Without it CMake still
    # configures and links an x2native with no guest code in it at all -- a
    # binary that starts, finds nothing to run, and looks like a broken port
    # rather than a missing build step. Refuse here instead, and say the two
    # commands that fix it.
    if ! ls "$ROOT"/src/recomp/*_native.c >/dev/null 2>&1; then
        cat >&2 <<'EOM'
run: src/recomp/ has no generated module sources, so there is nothing native
     to run. They are gitignored on purpose -- they are a mechanical
     translation of your own copy of the game, produced locally.

     Generate them (per module, e.g. libIGCore) with:

       tools/ghidra_export.sh <module>
       python3 tools/recomp.py emit   scratch/recomp/<module>.json \
               src/recomp/<module>.c --split 1500
       python3 tools/recomp.py native scratch/recomp/<module>.json \
               src/recomp/<module>_native.c

     or, for a module not yet in the set, tools/add_module.sh <module>.
EOM
        exit 2
    fi

    CMAKE_ARGS=(-S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo)
    [ -n "${TRACE:-}" ] && CMAKE_ARGS+=(-DX2_NATIVE_TRACE=ON)

    if [ ! -x "$BUILD/x2native" ] || [ -n "${REBUILD:-}" ]; then
        echo "run: building x2native in $BUILD ..."
        # CMake announces which modules it linked and which it skipped; that
        # line is worth seeing, because a silently-absent module is the
        # difference between "the port stops here" and "that code is not in
        # this binary".
        cmake "${CMAKE_ARGS[@]}" 2>&1 | grep -E "x2native:|error|Error" || true
        cmake --build "$BUILD" --target x2native -j"$(nproc)" || exit $?
    fi
    [ -x "$BUILD/x2native" ] || {
        echo "run: $BUILD/x2native was not built -- nothing ran" >&2; exit 2; }

    cat <<'EOM'

run: NATIVE build -- no Wine, no original binaries in the loop.

     WHAT YOU WILL SEE, so that the expected outcome is not read as a
     failure: the engine starts, initialises, and reaches the game's own
     DirectX 9.0c check, which truthfully reports DirectX absent, and the
     process exits 0. There is no rendering yet -- the renderer is the
     work in progress (docs/issues, C113/C114). A window may flash: that
     is the SDL surface probe, not the game.

     For a build that actually draws the game, use:  ./run.sh wine

EOM
    # Deliberately run from the REPO ROOT, not from the install. x2native finds
    # the install through $GAME_PC_DIR, and its working directory is where it
    # puts run state -- the emulated registry defaults to scratch/x2registry.txt
    # relative to CWD. Launching from inside the game directory would write that
    # into the install, which this project treats as strictly read-only.
    cd "$ROOT" || exit 2
    exec "$BUILD/x2native" --run ${X2_ARGS:-}
fi

# ======================================================================
# the Wine paths
# ======================================================================
export WINEPREFIX=${WINEPREFIX:-${WINE_PREFIX:-}}
[ -n "$WINEPREFIX" ] || { echo "run: set WINE_PREFIX in .env (see .env.example)" >&2; exit 2; }
[ -d "$WINEPREFIX" ] || { echo "run: WINEPREFIX $WINEPREFIX does not exist" >&2; exit 2; }

# ---- pick what to play -------------------------------------------------
if [ "$NAME" = "stock" ]; then
    : "${GAME_PC_DIR:?set GAME_PC_DIR in .env (see .env.example)}"
    RUNDIR=$GAME_PC_DIR
    echo "run: playing the STOCK install (control) -- $RUNDIR"
else
    # `wine` is the staged all-modules run directory; any other name is an
    # already-staged one under scratch/run/.
    [ "$NAME" = "wine" ] && NAME=all
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
