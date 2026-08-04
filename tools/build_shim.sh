#!/usr/bin/env bash
# Build a replacement libIG*.dll for the differential harness and stage a run
# directory for it.
#
#   tools/build_shim.sh proxy libIGDisplay.dll     # pure pass-through forwarder
#   tools/build_shim.sh trace libIGDisplay.dll     # forwarder + call tracing
#
# The run directory is a symlink farm over $GAME_PC_DIR with the one DLL
# replaced, so the real game install is never modified.
set -eu
cd "$(dirname "$0")/.."
ROOT=$PWD
MODE=${1:?usage: build_shim.sh <proxy|trace> <libIGXxx.dll>}
DLL=${2:?usage: build_shim.sh <proxy|trace> <libIGXxx.dll>}
BASE=${DLL%.dll}
ORIG=${BASE}_orig

[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
: "${GAME_PC_DIR:?set GAME_PC_DIR in .env (see .env.example)}"
[ -f "$GAME_PC_DIR/$DLL" ] || { echo "build_shim: no $DLL in \$GAME_PC_DIR" >&2; exit 2; }

OUT=$ROOT/scratch/shim/$MODE
RUN=$ROOT/scratch/run/$MODE
mkdir -p "$OUT" "$RUN"

case $MODE in
proxy)
  python3 tools/pe.py proxydef "$GAME_PC_DIR/$DLL" "$ORIG" > "$OUT/$BASE.def"
  # -nostdlib: a pure forwarder needs no code at all, so no CRT and no DllMain.
  i686-w64-mingw32-gcc -shared -nostdlib -o "$OUT/$DLL" "$OUT/$BASE.def" 2>&1 \
    | grep -v "cannot find entry symbol" || true
  ;;
trace)
  # Trace exactly the boundary surface: the symbols other modules actually
  # import. Tracing all 898 would be noise, and most are never crossed.
  python3 tools/pe.py surface "$DLL" \
      "$GAME_PC_DIR"/*.exe "$GAME_PC_DIR"/libIG*.dll "$GAME_PC_DIR"/libMovie.dll \
      > "$OUT/surface.txt"
  python3 tools/gen_trace.py "$GAME_PC_DIR/$DLL" "$ORIG" "$OUT/surface.txt" "$OUT"
  ( cd "$OUT" && i686-w64-mingw32-gcc -shared -o "$DLL" trace.def trace.c thunks.S \
       -Wall -O2 -static-libgcc )
  ;;
*) echo "build_shim: unknown mode '$MODE' (want proxy|trace)" >&2; exit 2 ;;
esac
[ -f "$OUT/$DLL" ] || { echo "build_shim: build produced no $DLL" >&2; exit 1; }

for e in "$GAME_PC_DIR"/*; do
  b=$(basename "$e"); [ "$b" = "$DLL" ] && continue
  ln -sfn "$e" "$RUN/$b"
done
cp "$GAME_PC_DIR/$DLL" "$RUN/$ORIG.dll"
cp "$OUT/$DLL" "$RUN/$DLL"
# FSAA off: llvmpipe cannot do multisampling headless.
sed 's/^multiSampleType = 4/multiSampleType = 0/' "$GAME_PC_DIR/alchemy.ini" > "$RUN/alchemy.ini"

echo "build_shim: staged $RUN ($DLL -> $ORIG.dll)"
echo "  run it:  X2_TRACE=x2trace.log tools/run_shim.sh $MODE 60"
