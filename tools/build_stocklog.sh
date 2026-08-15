#!/usr/bin/env bash
# Build the logging proxy d3d8.dll and stage a STOCK run directory that uses
# it, so the ORIGINAL engine's light and material calls can be read directly.
#
#   tools/build_stocklog.sh [rundir-name]        (default: stocklog)
#   X2_KEYS="..." tools/run_shim.sh stocklog 540
#   -> scratch/run/<name>/d3d8_lightlog.txt
#
# WHY this exists. The port's own instrumentation says the engine hands D3D8
# four point lights with a diffuse of exactly zero (C199), and every check on
# our side of the boundary says the D3D8 layer reports them faithfully. That
# leaves one question that cannot be answered from inside the port: does the
# REAL engine, running under Wine on the real d3d8, set the same values? A
# memory-scanning probe was tried first and could not anchor itself (issue #77,
# tools/light_probe.py); intercepting the call is exact where a scan is a guess.
#
# Nothing in the game install is written to: the run directory is a symlink
# farm, exactly as build_recomp.sh stages one.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
NAME=${1:-stocklog}
RUN=$ROOT/scratch/run/$NAME
SRC=$ROOT/tools/proxy_d3d8

[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
: "${GAME_PC_DIR:?set GAME_PC_DIR in .env (see .env.example)}"
export WINEPREFIX=${WINEPREFIX:-${WINE_PREFIX:-}}
[ -n "$WINEPREFIX" ] || { echo "build_stocklog: set WINE_PREFIX in .env" >&2; exit 2; }
[ -d "$GAME_PC_DIR" ] || { echo "build_stocklog: GAME_PC_DIR=$GAME_PC_DIR is not a directory" >&2; exit 2; }

CC=i686-w64-mingw32-gcc
command -v "$CC" >/dev/null || {
  echo "build_stocklog: $CC not found -- built NOTHING (dnf install mingw32-gcc)" >&2
  exit 2; }

# ---- the real d3d8 ---------------------------------------------------------
#
# 32-bit, because the game is. Named explicitly rather than searched for: a
# proxy that forwarded to the WRONG d3d8 would log a run the control never had.
REAL=$WINEPREFIX/drive_c/windows/syswow64/d3d8.dll
[ -f "$REAL" ] || {
  echo "build_stocklog: no 32-bit d3d8 at $REAL." >&2
  echo "  This prefix has no DXVK d3d8; the stock game cannot start without" >&2
  echo "  one (run_shim.sh forces d3d8=native). Built NOTHING." >&2
  exit 2; }
python3 "$ROOT/tools/pe.py" exports "$REAL" | grep -q ' Direct3DCreate8$' || {
  echo "build_stocklog: $REAL has no Direct3DCreate8 export -- that is not a" >&2
  echo "  d3d8 implementation. Built NOTHING." >&2
  exit 2; }

# ---- nothing may be imported from d3d8 BY ORDINAL --------------------------
#
# pe.py proxydef emits name forwarders only, so an ordinal-only import would
# reach a proxy that does not export it. Checked rather than assumed, because
# the failure is a load error minutes away from anything that mentions d3d8.
ORD=$(python3 "$ROOT/tools/pe.py" imports "$GAME_PC_DIR/libIGGfx.dll" 2>/dev/null \
      | awk '$1 == "d3d8.dll" && $2 ~ /^@/' | wc -l)
[ "$ORD" -eq 0 ] || {
  echo "build_stocklog: libIGGfx imports $ORD symbol(s) from d3d8 BY ORDINAL;" >&2
  echo "  the generated .def forwards names only. Built NOTHING." >&2
  exit 2; }

mkdir -p "$ROOT/scratch/build-proxy" "$RUN"
DEF=$ROOT/scratch/build-proxy/d3d8.def
OUT=$ROOT/scratch/build-proxy/d3d8.dll

# ---- the .def: forward everything EXCEPT the one export we implement -------
#
# Direct3DCreate8 is REPLACED in place, not deleted: supplying a .def turns off
# ld's auto-export, so an export merely absent from the file is absent from the
# DLL -- which is what a first attempt produced, a proxy the game could not
# import. The right-hand side is the DECORATED stdcall symbol proxy.c defines.
python3 "$ROOT/tools/pe.py" proxydef "$REAL" d3d8_real > "$DEF.all" || exit 1
sed 's|^  "Direct3DCreate8" = "d3d8_real\.Direct3DCreate8"$|  Direct3DCreate8 = Direct3DCreate8@4|' \
    "$DEF.all" > "$DEF"
FWD=$(grep -c '= "d3d8_real\.' "$DEF")
OWN=$(grep -c '^  Direct3DCreate8 = Direct3DCreate8@4$' "$DEF")
[ "$OWN" -eq 1 ] && [ "$FWD" -eq "$(( $(grep -c '^  "' "$DEF.all") - 1 ))" ] || {
  echo "build_stocklog: the generated .def was not rewritten as expected --" >&2
  echo "  $OWN local Direct3DCreate8 (want 1), $FWD forwarder(s) of" >&2
  echo "  $(grep -c '^  "' "$DEF.all") exports. pe.py proxydef's output format" >&2
  echo "  changed. Built NOTHING." >&2
  exit 2; }
echo "build_stocklog: $FWD export(s) forwarded to d3d8_real, Direct3DCreate8 implemented"

# ---- compile ---------------------------------------------------------------
"$CC" -shared -O2 -Wall -Wextra -o "$OUT" \
    "$SRC/proxy.c" "$SRC/fwd.S" "$DEF" \
    -static-libgcc || { echo "build_stocklog: compile FAILED" >&2; exit 1; }

# The proxy is worthless if it does not export what the game imports; and a
# missing forwarder shows up as a load failure with no mention of d3d8, so the
# built artifact is checked against the real one rather than trusted.
python3 "$ROOT/tools/pe.py" exports "$OUT" | grep -q ' Direct3DCreate8$' || {
  echo "build_stocklog: the BUILT $OUT does not export Direct3DCreate8." >&2
  exit 2; }
N_REAL=$(python3 "$ROOT/tools/pe.py" exports "$REAL" | grep -c '^ ')
N_OURS=$(python3 "$ROOT/tools/pe.py" exports "$OUT"  | grep -c '^ ')
[ "$N_OURS" -ge "$N_REAL" ] || {
  echo "build_stocklog: the proxy exports $N_OURS symbol(s); the real d3d8" >&2
  echo "  exports $N_REAL. A missing export is a load failure later." >&2
  exit 2; }

# ---- stage the run directory ----------------------------------------------
for e in "$GAME_PC_DIR"/*; do
    b=$(basename "$e")
    [ "$b" = "alchemy.ini" ] && continue
    ln -sfn "$e" "$RUN/$b"
done
# FSAA off: llvmpipe cannot multisample headless. Same as build_recomp.sh.
sed 's/^multiSampleType = 4/multiSampleType = 0/' \
    "$GAME_PC_DIR/alchemy.ini" > "$RUN/alchemy.ini"
cp "$REAL" "$RUN/d3d8_real.dll"
cp "$OUT"  "$RUN/d3d8.dll"
# A stale log from a previous run reads as this run's output. Start empty.
rm -f "$RUN/d3d8_lightlog.txt"

echo "build_stocklog: staged $RUN"
echo "  d3d8.dll      = the logging proxy ($(stat -c%s "$OUT") bytes)"
echo "  d3d8_real.dll = $REAL"
echo "  run it:  X2_KEYS=... tools/run_shim.sh $NAME 540"
echo "  read it: scratch/run/$NAME/d3d8_lightlog.txt"
