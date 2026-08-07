#!/bin/sh
# Screenshot the NATIVE build, headless.
#
# "18 draws per frame" is a count. It is not a picture, and a renderer that
# draws eighteen wrong triangles produces the same count as one that draws the
# game. The Wine path has had tools/run_shim.sh for exactly this reason since
# the beginning; this is its counterpart for x2native.
#
# Headless on Xvfb, so it can run from an agent or a cron job and so the shot
# is the same size every time. The guest asks for 800x600 and cannot mode-set
# on Xvfb, which is why the screen is created at that size.
#
#   tools/native_shot.sh [seconds] [name]
#
# Artifacts: scratch/screenshots/<name>.png and scratch/logs/<name>.log.
# Never /tmp.
set -u
cd "$(dirname "$0")/.." || exit 2
ROOT=$PWD
[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
: "${GAME_PC_DIR:?set GAME_PC_DIR in .env (see .env.example)}"

SECS=${1:-45}
NAME=${2:-native}
BIN=$ROOT/scratch/build-native/x2native
RES=${X2_RES:-800x600}

[ -x "$BIN" ] || { echo "native_shot: $BIN is not built -- captured NOTHING" >&2; exit 2; }
mkdir -p "$ROOT/scratch/screenshots" "$ROOT/scratch/logs"
SHOT=$ROOT/scratch/screenshots/$NAME.png
LOG=$ROOT/scratch/logs/$NAME.log

# A display nobody else is using. :99 is the conventional one and is exactly
# what a second agent would also pick, so the number comes from the PID.
DISP=:$(( 90 + $$ % 60 ))
Xvfb "$DISP" -screen 0 "${RES}x24" >/dev/null 2>&1 &
XVFB=$!
sleep 1
kill -0 "$XVFB" 2>/dev/null || {
    echo "native_shot: Xvfb did not start on $DISP -- captured NOTHING" >&2
    exit 2; }

# SDL_VIDEODRIVER=x11, and WAYLAND_DISPLAY out of the environment.
#
# Not belt-and-braces: SDL3 PREFERS Wayland, so on a Wayland session it ignores
# DISPLAY entirely and puts the window on the user's real desktop. The first
# version of this script did exactly that -- Xvfb ran, the game rendered, the
# capture of the Xvfb root was 100% black, and the window was on the screen
# behind it the whole time. An empty picture that means "you photographed the
# wrong display" is the worst kind of negative.
DISPLAY=$DISP SDL_VIDEODRIVER=x11 WAYLAND_DISPLAY= \
    "$BIN" --no-window --d3d8 --run >"$LOG" 2>&1 &
GAME=$!

# Wait for the run to actually reach frames rather than sleeping a guess: the
# heartbeat prints a present count, so "it is drawing" is observable. A fixed
# sleep would photograph a black window and call it a result.
waited=0
while [ "$waited" -lt "$SECS" ]; do
    sleep 2
    waited=$((waited + 2))
    kill -0 "$GAME" 2>/dev/null || break
    grep -q "presents [0-9]* (+[1-9]" "$LOG" 2>/dev/null && break
done

if ! kill -0 "$GAME" 2>/dev/null; then
    echo "native_shot: the run ENDED before it was photographed (after ${waited}s)."
    echo "  The last lines of $LOG:"
    grep -v '^\[TRACE\]' "$LOG" | tail -8
    kill "$XVFB" 2>/dev/null
    exit 1
fi

# One more heartbeat period, so the shot is of a settled frame rather than the
# first one that happened to present.
sleep 6
DISPLAY=$DISP import -window root "$SHOT" 2>/dev/null

# By PID. Several agents and the user run this same binary, so `pkill x2native`
# would kill their runs too.
kill "$GAME" 2>/dev/null
sleep 1
kill -9 "$GAME" 2>/dev/null
kill "$XVFB" 2>/dev/null

[ -s "$SHOT" ] || {
    echo "native_shot: no image was written to $SHOT -- captured NOTHING" >&2
    exit 2; }

# What is IN it, not just that a file exists. An all-one-colour image is what
# both "the renderer drew nothing" and "the capture missed the window" look
# like, and they have to be distinguishable from a picture.
python3 - "$SHOT" <<'EOF'
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
    print("  THAT IS ONE FLAT COLOUR -- either nothing was drawn, or the "
          "capture did not catch the window. It is NOT evidence that the "
          "renderer works.")
EOF
grep -E "presents [0-9]+ \(\+" "$LOG" | tail -2
