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

# X2_KEYS="<seconds>:<key>,..." -- drive the game, in wall-clock seconds.
#
# The native build has X2_INPUT_SCRIPT and can be steered to any scene; this
# path had nothing, so the stock control could only ever photograph whatever
# the intro happened to be showing. That made it useless as a control for
# anything past the menu -- and "settle it against stock" is this project's
# rule for every rendering question.
#
# Seconds, not frames: nothing here can see the game's frame counter. Keys are
# xdotool keysyms (Return, Escape, Down, space).
#
# Every press is REPORTED, and so is pressing nothing, because a control run
# that silently failed to drive the game looks exactly like one that did.
if [ -n "${X2_KEYS:-}" ]; then
  ( prev=0
    # "<t>:<key>" fires once; "<from>-<to>/<step>:<key>" fires repeatedly.
    #
    # Exact instants are too brittle here: the six intro movies take a
    # different wall-clock time on each run, and two control runs were lost to
    # a press landing before the menu appeared and after it had moved on. A
    # repeat window blankets the uncertainty -- pressing Enter every few
    # seconds through the menu is harmless and lands whatever the timing.
    printf '%s' "$X2_KEYS" | tr ',' '\n' | while IFS=: read -r at key; do
      [ -n "$at" ] && [ -n "$key" ] || continue
      case "$at" in
        *-*/*)
          from=${at%%-*}; rest=${at#*-}; to=${rest%%/*}; step=${rest#*/}
          t=$from
          while [ "$t" -le "$to" ]; do
            sleep "$(awk -v a="$t" -v p="$prev" 'BEGIN{d=a-p; print (d>0)?d:0}')"
            prev=$t
            w=$(DISPLAY=$DISP xdotool search --onlyvisible --name '.*' 2>/dev/null | tail -1)
            [ -n "$w" ] && DISPLAY=$DISP xdotool windowactivate --sync "$w" 2>/dev/null
            [ -n "$w" ] && DISPLAY=$DISP xdotool key --clearmodifiers "$key" 2>/dev/null
            echo "run_shim: KEY $key sent at t=${t}s (repeat ${from}-${to}/${step})" >&2
            t=$((t + step))
          done
          continue
          ;;
      esac
      sleep "$(awk -v a="$at" -v p="$prev" 'BEGIN{d=a-p; print (d>0)?d:0}')"
      prev=$at
      # The Wine virtual desktop's window is not reliably named "x2" -- looking
      # for that name found nothing and the whole driven run sent no keys at
      # all. Take any visible window on this private display instead; there is
      # only the game on it. The window list is printed the first time so a
      # future mismatch is diagnosable from the log rather than by guessing.
      win=$(DISPLAY=$DISP xdotool search --onlyvisible --name '.*' 2>/dev/null | tail -1)
      if [ -z "$win" ]; then
        win=$(DISPLAY=$DISP xdotool getactivewindow 2>/dev/null)
      fi
      if [ -n "$win" ]; then
        DISPLAY=$DISP xdotool windowactivate --sync "$win" 2>/dev/null
        DISPLAY=$DISP xdotool key --clearmodifiers --window "$win" "$key" 2>/dev/null
        DISPLAY=$DISP xdotool key --clearmodifiers "$key" 2>/dev/null
        echo "run_shim: KEY $key sent at t=${at}s (window $win \"$(DISPLAY=$DISP xdotool getwindowname "$win" 2>/dev/null)\")" >&2
      else
        echo "run_shim: KEY $key at t=${at}s NOT SENT -- NO visible window on $DISP." >&2
        DISPLAY=$DISP xdotool search --name '.*' 2>/dev/null \
          | while read -r w; do
              echo "run_shim:   window $w \"$(DISPLAY=$DISP xdotool getwindowname "$w" 2>/dev/null)\"" >&2
            done
      fi
    done ) &
  echo "run_shim: X2_KEYS is set -- this run is DRIVEN: $X2_KEYS"
else
  echo "run_shim: X2_KEYS is unset -- NOTHING drives this run; it photographs"
  echo "          whatever the game reaches on its own."
fi

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
