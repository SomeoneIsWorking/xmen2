#!/usr/bin/env bash
#
# Verify that patches/xboxrecomp/ actually REPRODUCES vendor/xboxrecomp.
#
# vendor/ is gitignored, so the patch files are the only record of the toolkit
# changes this project depends on. If a change lands in the vendor tree but not
# in a patch, everything keeps working here and nowhere else -- and the gap is
# invisible until someone re-clones. It happened: patch 0003 was missing six
# files (tools/disasm/functions.py, tools/recomp/{__main__,disasm,translator,
# test_regressions}.py, tools/validate_ordinals.py), so a fresh clone plus
# patches produced a DIFFERENT recompiler from the one every result came from.
#
# This extracts the pinned upstream commit into a scratch tree, applies the
# patches, and diffs the result against the live vendor tree. It reports what
# is missing, extra, or different -- and exits non-zero on any of them.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR="$REPO/vendor/xboxrecomp"
PATCHES="$REPO/patches/xboxrecomp"
BASE="${BASE:-32da238}"          # upstream commit the patches are cut against
WORK="${WORK:-$REPO/scratch/patchcheck}"

[ -d "$VENDOR/.git" ] || { echo "check_patches: no clone at $VENDOR -- run tools/get_xboxrecomp.sh" >&2; exit 1; }
ls "$PATCHES"/0*.patch >/dev/null 2>&1 || { echo "check_patches: no patches in $PATCHES -- nothing was checked" >&2; exit 1; }

rm -rf "$WORK"; mkdir -p "$WORK"
git -C "$VENDOR" archive "$BASE" | tar -x -C "$WORK"

# Its own repository, because $WORK lives inside THIS one: git apply resolves
# paths against the enclosing work tree's root, so without this it silently
# reports "Skipped patch" for every file and the check passes on nothing.
git -C "$WORK" init -q
git -C "$WORK" -c user.email=check -c user.name=check \
    -c commit.gpgsign=false add -A >/dev/null

echo "check_patches: base $BASE, $(ls "$PATCHES"/0*.patch | wc -l) patches"
for p in "$PATCHES"/0*.patch; do
    if ! git -C "$WORK" apply "$p" 2>&1; then
        echo "check_patches: $(basename "$p") DOES NOT APPLY to $BASE" >&2
        exit 1
    fi
done

# Every file the vendor tree changed vs upstream must now match. src/game is
# generated output, not a toolkit change, so it is excluded by name rather
# than by silence.
changed=$(git -C "$VENDOR" diff "$BASE" --name-only -- . ':!src/game')
rc=0
n=0
for f in $changed; do
    n=$((n + 1))
    if [ ! -e "$WORK/$f" ]; then
        echo "  MISSING  $f -- changed in vendor/, not in any patch" >&2; rc=1; continue
    fi
    if ! diff -q "$VENDOR/$f" "$WORK/$f" >/dev/null; then
        echo "  DIFFERS  $f -- the patches do not reproduce the vendor tree" >&2; rc=1
    fi
done

if [ "$n" -eq 0 ]; then
    echo "check_patches: the vendor tree has NO changes vs $BASE." >&2
    echo "  That is not a pass -- it means the clone is pristine and the" >&2
    echo "  patches were never applied to it. Nothing was verified." >&2
    exit 1
fi

[ "$rc" -eq 0 ] && echo "check_patches: OK -- all $n changed file(s) reproduced from the patches"
exit "$rc"
