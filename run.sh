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
# question is settled against, while `wine` keeps the hosted-recompiler path
# independently testable. Losing either would cost more than the convenience
# of a shorter command.
#
# This is the counterpart to tools/run_shim.sh, which is the HEADLESS harness:
# Xvfb, no sound, screenshots, a fixed timeout. That one is for measuring. This
# one is for looking, so it uses the real display, leaves the audio drivers
# alone, and runs until you close it.
#
#
# The output of every run is TEE'd to scratch/logs/<mode>.log, always. This is
# not a convenience: a hand-driven run is expensive -- someone has to play the
# game to the scene in question -- and its shutdown report is where several
# censuses print. One such run's answer was lost to a terminal scrollback
# already, and re-driving to the same dialog costs minutes of a person's time.
# Environment (all optional):
#   REBUILD=1       rebuild before running
#   RUN_ARGS=...    extra arguments passed through to the game / to x2native;
#                   these never replace the native target's launch mode
#                   (NOT X2_ARGS -- that name belongs to the runtime's argument
#                   watch, an entry-point list, and passing it as a command
#                   line makes x2native refuse it as an unknown option)
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

    # The live target includes Xbox prompts. Build the derived pack from the
    # player's PC font plus this port's SVGs, with a content-addressed cache,
    # unless the caller explicitly supplied a different asset pack. The hook
    # is enabled only with the matching pack, so a custom/missing pack cannot
    # turn a prompt into an invisible byte.
    if [ -z "${X2_ASSETS:-}" ]; then
        X2_ASSETS=$ROOT/scratch/generated-assets/prompt-font
        python3 tools/prepare_native_assets.py "$GAME_PC_DIR" "$X2_ASSETS" || exit $?
        export X2_ASSETS X2_PROMPT_GLYPHS=1
    fi

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

     WHAT YOU WILL SEE: the current project target -- the recompiled game,
     native SDL3 GPU/Vulkan renderer, input and sound -- in its configured
     window mode (1280x720 windowed on first run). Press F1 for video, player
     assignment and keyboard-profile settings. It runs until you close it,
     then prints its reports on the way out.

     It is not silent about what it is doing: [HB] lines every few
     seconds carry the frame, draw and present counts.

     Inputs are recorded automatically. While this run is open,
     tools/x2ctl.py probe finds it through scratch/run/live.json and reports
     the live frame, guest input state, and recent recorded input changes.

     ./run.sh wine is still the reference for "what it should look
     like", and ./run.sh stock is the untouched install as the control.

EOM
    # Deliberately run from the REPO ROOT, not from the install. x2native finds
    # the install through $GAME_PC_DIR, and its working directory is where it
    # puts run state -- the emulated registry defaults to scratch/x2registry.txt
    # relative to CWD. Launching from inside the game directory would write that
    # into the install, which this project treats as strictly read-only.
    cd "$ROOT" || exit 2
    # x2native's own zero-argument route is the live SDL3 GPU + D3D8 game.
    # RUN_ARGS only extends that route; it cannot replace a required renderer
    # by being non-empty (the old ${RUN_ARGS:---d3d8} expression did exactly
    # that). Keeping run.sh and the binary on one default prevents drift.
    mkdir -p "$ROOT/scratch/logs"
    echo "run: logging this run to scratch/logs/native.log" >&2
    # PIPESTATUS, not the pipeline's status, which would be tee's.
    # shellcheck disable=SC2086 -- RUN_ARGS is intentionally an argv fragment.
    set -o pipefail
    "$BUILD/x2native" ${RUN_ARGS:-} 2>&1 | tee "$ROOT/scratch/logs/native.log"
    exit "${PIPESTATUS[0]}"
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
mkdir -p "$ROOT/scratch/logs"
echo "run: logging this run to scratch/logs/$NAME.log" >&2
set -o pipefail
env WINEDLLOVERRIDES="d3d8,d3d9=$X2_D3D" \
    wine "${LAUNCH[@]}" "$(winepath -w ./XMen2.exe)" ${RUN_ARGS:-} 2>&1 \
    | tee "$ROOT/scratch/logs/$NAME.log"
exit "${PIPESTATUS[0]}"
