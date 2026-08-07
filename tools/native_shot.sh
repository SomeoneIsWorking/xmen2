#!/bin/sh
# Screenshot the NATIVE build, headless -- no X server, no window, no desktop.
#
# "18 draws per frame" is a count. It is not a picture, and a renderer that
# draws eighteen wrong triangles produces the same count as one that draws the
# game.
#
# x2native --no-window renders into an off-screen target and X2_SHOT writes it
# out, so this needs nothing on the machine and takes over nothing. The
# previous version drove a real window on Xvfb; that worked, and then only
# after a first attempt photographed a 100%-black root because SDL3 prefers
# Wayland and had put the window on the user's actual desktop. Rendering
# off-screen removes the whole question.
#
#   tools/native_shot.sh [seconds] [name]
#
# Artifacts: scratch/screenshots/<name>.{ppm,png} and scratch/logs/<name>.log.
# Never /tmp.
set -u
cd "$(dirname "$0")/.." || exit 2
ROOT=$PWD
[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
: "${GAME_PC_DIR:?set GAME_PC_DIR in .env (see .env.example)}"

SECS=${1:-45}
NAME=${2:-native}
BIN=$ROOT/scratch/build-native/x2native

[ -x "$BIN" ] || { echo "native_shot: $BIN is not built -- captured NOTHING" >&2; exit 2; }
mkdir -p "$ROOT/scratch/screenshots" "$ROOT/scratch/logs"
PPM=$ROOT/scratch/screenshots/$NAME.ppm
PNG=$ROOT/scratch/screenshots/$NAME.png
LOG=$ROOT/scratch/logs/$NAME.log
rm -f "$PPM" "$PNG"

# Unpaced: the game caps itself at 60fps and a capture has no reason to wait
# for the cap. The clock is not scaled, so nothing about what is DRAWN changes.
X2_SHOT=$PPM X2_UNPACED=${X2_UNPACED:-1} X2_HEARTBEAT=${X2_HEARTBEAT:-10} \
    "$BIN" --no-window --d3d8 --run >"$LOG" 2>&1 &
GAME=$!

# Wait for frames to actually be presenting rather than sleeping a guess: a
# fixed sleep photographs whatever happens to be there and calls it a result.
waited=0
while [ "$waited" -lt "$SECS" ]; do
    sleep 2
    waited=$((waited + 2))
    kill -0 "$GAME" 2>/dev/null || break
    [ -s "$PPM" ] && grep -q "presents [0-9]* (+[1-9]" "$LOG" 2>/dev/null && break
done

if ! kill -0 "$GAME" 2>/dev/null; then
    echo "native_shot: the run ENDED before it was photographed (after ${waited}s)."
    echo "  The last lines of $LOG:"
    grep -v '^\[TRACE\]' "$LOG" | tail -8
    [ -s "$PPM" ] || exit 1
    echo "  A frame HAD been written before it ended; reporting on that."
fi

# A few more frames, so the shot is of a settled frame rather than the first
# one that happened to be written.
sleep 4
# By PID. Several agents and the user run this same binary, so `pkill x2native`
# would kill their runs too.
kill "$GAME" 2>/dev/null
sleep 1
kill -9 "$GAME" 2>/dev/null

[ -s "$PPM" ] || {
    echo "native_shot: no frame was ever written to $PPM -- captured NOTHING." >&2
    echo "  Either the run never rendered, or X2_SHOT is not wired up. The log:" >&2
    grep -v '^\[TRACE\]' "$LOG" | tail -6 >&2
    exit 2; }

command -v convert >/dev/null 2>&1 && convert "$PPM" "$PNG" 2>/dev/null

# What is IN it, not just that a file exists. An all-one-colour image is what
# "the renderer drew nothing" looks like, and it has to be distinguishable from
# a picture.
python3 - "${PNG:-$PPM}" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
cols = im.getcolors(maxcolors=1 << 24) or []
cols.sort(reverse=True)
top = cols[0] if cols else (0, (0, 0, 0))
pct = 100.0 * top[0] / (im.width * im.height)
print("native_shot: %s  %dx%d  %d distinct colour(s); the most common is "
      "%s at %.1f%%" % (sys.argv[1], im.width, im.height, len(cols),
                        top[1], pct))
if len(cols) < 4 or pct > 99.0:
    print("  THAT IS ONE FLAT COLOUR -- nothing was drawn into the frame. It "
          "is NOT evidence that the renderer works.")
EOF
grep -E "presents [0-9]+ \(\+" "$LOG" | tail -2
grep -E "gpu draws" "$LOG" | tail -1
