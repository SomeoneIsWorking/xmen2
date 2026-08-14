#!/usr/bin/env bash
# Build and stage the Wine-hosted recompiled XMen2.exe runner.
#
# This is the original hybrid front end: translated functions execute as C and
# unresolved code targets fall back to the mapped original image.  It remains
# useful as a same-Wine-environment discriminator while the native port grows.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD

[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
: "${GAME_PC_DIR:?set GAME_PC_DIR in .env (see .env.example)}"

JSON=$ROOT/scratch/recomp/XMen2.json
GEN=$ROOT/scratch/recomp/x2run
RUN=$ROOT/scratch/run/x2run
OUT=$GEN/x2run.exe
OPT=${OPT:--O1}
SPLIT=${SPLIT:-750}
JOBS=${JOBS:-4}
WATCH=${WATCH:-0}

[ -f "$JSON" ] || {
    echo "build_x2run: no $JSON -- run tools/ghidra_export.sh XMen2 first" >&2
    exit 2
}
[ -f "$GAME_PC_DIR/XMen2.exe" ] || {
    echo "build_x2run: no XMen2.exe in \$GAME_PC_DIR" >&2
    exit 2
}
command -v i686-w64-mingw32-gcc >/dev/null || {
    echo "build_x2run: i686-w64-mingw32-gcc is not installed" >&2
    exit 2
}
case "$SPLIT" in
    ''|*[!0-9]*|0) echo "build_x2run: SPLIT must be a positive integer" >&2; exit 2 ;;
esac
case "$WATCH" in
    0) OBJ=$GEN/obj; watch_defs=(); watch_sources=() ;;
    1) OBJ=$GEN/obj-watch; watch_defs=(-DX86_WATCH); watch_sources=(
        src/x86watch.c src/x86watch_memory.c src/x86watch_stack.c
        src/x86watch_trace.c
        src/x86fault.c) ;;
    *) echo "build_x2run: WATCH must be 0 or 1" >&2; exit 2 ;;
esac

mkdir -p "$GEN" "$RUN" "$OBJ"
mkdir -p "$GEN/tmp"
export TMPDIR=$GEN/tmp

echo "== 1/4 emit translated XMen2.exe bodies =="
python3 tools/recomp.py emit "$JSON" "$GEN/XMen2.c" --split "$SPLIT"

echo "== 2/4 emit hosted dispatch and import resolver =="
python3 tools/recomp.py runtime "$JSON" "$GEN/XMen2_runtime.c" hostimports

chunks=("$GEN"/XMen2_[0-9][0-9][0-9].c)
[ -f "${chunks[0]}" ] || {
    echo "build_x2run: emitter produced no $GEN/XMen2_NNN.c chunks" >&2
    exit 1
}

echo "== 3/4 compile ${#chunks[@]} translated chunks =="
# XMen2.exe has no relocations and its data must occupy 0x00400000.  Linking
# the runner at that image base, with its own .text above the guest's 0x674000
# image, makes Wine reserve the low range as MEM_IMAGE so x2run can fill it.
sources=(src/app/x2run.c src/app/x2run_diag.c src/recomp/x87crt.c \
    src/recomp/x87host.c src/recomp/x86callbacks.c \
    "$GEN/XMen2_runtime.c" "${watch_sources[@]}" "${chunks[@]}")
objects=()
running=0
for src in "${sources[@]}"; do
    obj=$OBJ/$(basename "${src%.c}").o
    objects+=("$obj")
    if [ "$obj" -nt "$src" ] && [ "$obj" -nt src/recomp/x86rt.h ]; then
        continue
    fi
    i686-w64-mingw32-gcc "$OPT" -msse2 "${watch_defs[@]}" \
        -I src/recomp -I src/app -c "$src" -o "$obj" &
    running=$((running + 1))
    if [ "$running" -ge "$JOBS" ]; then
        wait -n
        running=$((running - 1))
    fi
done
while [ "$running" -gt 0 ]; do
    wait -n
    running=$((running - 1))
done
i686-w64-mingw32-gcc -o "$OUT" "${objects[@]}" \
    -Wl,--image-base,0x00400000 -Wl,--section-start,.text=0x00b00000 \
    -static-libgcc
[ -f "$OUT" ] || {
    echo "build_x2run: compiler produced no $OUT" >&2
    exit 1
}

# Validate the layout that makes the non-relocatable guest possible.  Merely
# producing a PE is not enough: a default link at 0x00400000 would overwrite
# the runner itself when the original sections are copied in.
layout=$(i686-w64-mingw32-objdump -h "$OUT")
printf '%s\n' "$layout" | grep -Eq '^ +[0-9]+ \.text +[0-9a-f]+ +00b00000 ' || {
    echo "build_x2run: linked .text is not at 0x00b00000 -- refusing to stage" >&2
    exit 1
}

echo "== 4/4 stage immutable game assets and current runner =="
for e in "$GAME_PC_DIR"/*; do
    b=$(basename "$e")
    [ "$b" = XMen2.exe ] && continue
    [ "$b" = alchemy.ini ] && continue
    ln -sfn "$e" "$RUN/$b"
done
cp "$GAME_PC_DIR/XMen2.exe" "$RUN/XMen2_orig.exe"
cp "$OUT" "$RUN/x2run.exe"
sed 's/^multiSampleType = 4/multiSampleType = 0/' \
    "$GAME_PC_DIR/alchemy.ini" > "$RUN/alchemy.ini"

echo "build_x2run: staged $RUN (${#chunks[@]} chunks, $(stat -c%s "$OUT") bytes)"
echo "  run it: X2_EXE=x2run.exe tools/run_shim.sh x2run 30"
