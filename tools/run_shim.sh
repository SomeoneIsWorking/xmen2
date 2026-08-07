#!/usr/bin/env bash
# Run XMen2.exe from a run-directory under Wine, headless on Xvfb, and capture
# a frame. Used to A/B a stock run against a run with one libIG*.dll replaced.
#
#   tools/run_shim.sh <rundir-name> [seconds]
#
# Uses the Lutris-provisioned prefix (it has the DXVK d3d8 the game needs); a
# bare `wineboot -u` prefix does NOT and fails at import_dll on d3d8.dll.
# Artifacts land in scratch/logs and scratch/screenshots, never /tmp.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
NAME=${1:?usage: run_shim.sh <rundir-name> [seconds]}
SECS=${2:-40}
RUNDIR=$ROOT/scratch/run/$NAME
[ -f "$RUNDIR/${X2_EXE:-XMen2.exe}" ] || {
  echo "run_shim: $RUNDIR/${X2_EXE:-XMen2.exe} missing -- ran NOTHING" >&2; exit 2; }

# Machine-specific paths live only in .env (gitignored).
[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
export WINEPREFIX=${WINEPREFIX:-${WINE_PREFIX:-}}
[ -n "$WINEPREFIX" ] || { echo "run_shim: set WINE_PREFIX in .env (see .env.example)" >&2; exit 2; }
[ -d "$WINEPREFIX" ] || { echo "run_shim: WINEPREFIX $WINEPREFIX missing" >&2; exit 2; }
mkdir -p "$ROOT/scratch/logs" "$ROOT/scratch/screenshots"
LOG=$ROOT/scratch/logs/$NAME.log
SHOT=$ROOT/scratch/screenshots/$NAME.png

# The game asks for 800x600 FULLSCREEN; Xvfb cannot mode-switch, so
# ChangeDisplaySettings fails and D3D9 aborts fullscreen init. A Wine virtual
# desktop satisfies the mode change without touching the X screen, and pins the
# framebuffer size so A/B frames are comparable.
: "${X2_RES:=800x600}"
: "${X2_EXE:=XMen2.exe}"          # which image to launch in the run dir
: "${RUN_ARGS:=}"    # the game's command line. NOT X2_ARGS: that is the
                     # runtime argument watch (an entry-point list).

DISP=:$((90 + RANDOM % 8))
Xvfb "$DISP" -screen 0 "${X2_RES}x24" >/dev/null 2>&1 &
XPID=$!
trap 'kill -9 $XPID 2>/dev/null' EXIT
sleep 2

# d3d8 MUST be native (DXVK): this Wine build ships no builtin d3d8 at all, so
# `d3d8=b` fails at import_dll. DXVK needs Vulkan, and under Xvfb there is no
# hardware ICD (no DRI3), so point the loader at lavapipe -- the 32-bit ICD,
# because the game is a 32-bit process.
# Headless runs must be SILENT: the game plays its FMV audio through the real
# device otherwise, which is audible to whoever is at the machine. Disabling
# Wine's audio driver DLLs does this per-run, via the environment only -- the
# user's prefix registry is never modified. X2_MUTE= (empty) re-enables sound.
: "${X2_MUTE=winepulse.drv,winealsa.drv,wineoss.drv,winecoreaudio.drv=d}"
: "${X2_D3D:=n}"
: "${X2_VK_ICD:=/usr/share/vulkan/icd.d/lvp_icd.i686.json:/usr/share/vulkan/icd.d/lvp_icd.x86_64.json}"
# X2_TRACE, if set, is the shim's trace-log path (relative to the run dir).
export X2_TRACE=${X2_TRACE:-}
( cd "$RUNDIR" && DISPLAY=$DISP WINEDEBUG=+loaddll \
    WINEDLLOVERRIDES="d3d8,d3d9=$X2_D3D;$X2_MUTE" \
    VK_DRIVER_FILES="$X2_VK_ICD" VK_ICD_FILENAMES="$X2_VK_ICD" \
    wine explorer /desktop=x2,"$X2_RES" "$(cd "$RUNDIR" && winepath -w ./$X2_EXE)" $RUN_ARGS \
      >"$LOG" 2>&1 ) &
RUNPID=$!

# Sample several frames, not one. A single capture at a fixed instant lands on
# a fade or a movie boundary often enough to read as "black screen = broken":
# that happened, and nearly cost a bisection of a regression that did not exist.
# The most varied frame is kept as the representative, and ALL samples are
# reported so a genuinely black run is still visible as black at every sample.
NSAMP=${X2_SAMPLES:-3}
i=1
while [ "$i" -le "$NSAMP" ]; do
  sleep "$(( SECS / NSAMP ))"
  DISPLAY=$DISP import -window root "$SHOT.$i" 2>/dev/null
  i=$((i + 1))
done

# RUNPID is the `wine explorer` wrapper, NOT the game -- it stays alive even if
# XMen2.exe never loaded, so asking it is worthless. Ask the log whether the
# game image was actually mapped.
if grep -q "$X2_EXE\" at" "$LOG"; then GAME_LOADED=yes; else GAME_LOADED=no; fi
if kill -0 $RUNPID 2>/dev/null; then ALIVE=yes; else ALIVE=no; fi
kill -TERM $RUNPID 2>/dev/null
sleep 1
kill -9 $RUNPID 2>/dev/null
DISPLAY=$DISP wineserver -k 2>/dev/null

# Report the negative loudly: a black frame means the capture proved nothing.
python3 - "$SHOT" "$NSAMP" <<'PY'
import sys, os
from PIL import Image
p, n = sys.argv[1], int(sys.argv[2])
best, shots = None, []
for i in range(1, n + 1):
    f = "%s.%d" % (p, i)
    if not os.path.exists(f):
        shots.append((i, None)); continue
    im = Image.open(f).convert("RGB")
    cols = im.getcolors(maxcolors=1 << 24) or []
    tot = im.width * im.height
    top = sorted(cols, reverse=True)[:1]
    frac = top[0][0] / tot if top else 1.0
    shots.append((i, (len(cols), frac, im)))
    if best is None or len(cols) > best[0]:
        best = (len(cols), frac, im, f)
for i, d in shots:
    if d is None:
        print("SHOT %d: capture FAILED -- shows NOTHING" % i)
    else:
        print("SHOT %d: distinct_colors=%d dominant %.1f%%%s"
              % (i, d[0], d[1] * 100,
                 "  <-- uniform" if d[1] > 0.995 else ""))
if best is None:
    print("SHOT: every sample failed -- this run proves nothing"); sys.exit(0)
best[2].save(p)
print("SHOT: kept sample with %d colours as %s%s"
      % (best[0], p,
         "  <-- ALL SAMPLES UNIFORM: nothing rendered" if best[1] > 0.995 else ""))
PY
echo "RUN: $NAME game_image_loaded=$GAME_LOADED wrapper_alive=$ALIVE log=$LOG"
