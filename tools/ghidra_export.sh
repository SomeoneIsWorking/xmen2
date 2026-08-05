#!/usr/bin/env bash
#
# Export one module's functions to JSON for the offline recompiler.
#
#   tools/ghidra_export.sh <module-basename> [--reanalyze]
#   tools/ghidra_export.sh <module-basename> --seed <file-of-hex-addresses>
#   tools/ghidra_export.sh <module-basename> --split-at <file-of-hex-addresses>
#   tools/ghidra_export.sh libIGCore
#
# This existed only as a sequence of commands somebody ran once. The result is
# that scratch/recomp/ holds four JSONs whose provenance is not recorded and
# whose Ghidra project no longer contains three of them -- so "re-export with a
# fixed analysis" was not a thing anyone could do. Every module the recompiler
# eats has to come from a command.
#
# Environment:
#   GAME_PC_DIR   the game install (from .env)
#   GHIDRA_HOME   Ghidra install; otherwise analyzeHeadless is found on PATH
#   GHIDRA_PROJ   project directory (default: scratch/ghidra)
#   SEED_TABLES=1 also create functions from runs of code pointers in .rdata
#                 (vtables); SEED_MIN_RUN tunes how many consecutive it needs
#   FUNCS_MAX     stop after N functions (quick iteration; 0 = all)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD

MOD=${1:?usage: ghidra_export.sh <module-basename> [--reanalyze|--seed <file>]}
REANALYZE=""
SEEDFILE=""
SPLITFILE=""
case ${2:-} in
    --reanalyze) REANALYZE=1 ;;
    --seed)      SEEDFILE=${3:?--seed needs a file of hex addresses} ;;
    --split-at)  SPLITFILE=${3:?--split-at needs a file of hex addresses} ;;
    "")          ;;
    *)           echo "ghidra_export: unknown option $2" >&2; exit 2 ;;
esac

[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
: "${GAME_PC_DIR:?set GAME_PC_DIR in .env (see .env.example)}"

# The binary may be a DLL or the exe; take whichever exists rather than making
# the caller remember the extension.
BIN=""
for cand in "$GAME_PC_DIR/$MOD.dll" "$GAME_PC_DIR/$MOD.exe" "$GAME_PC_DIR/$MOD"; do
    [ -f "$cand" ] && { BIN=$cand; break; }
done
[ -n "$BIN" ] || { echo "ghidra_export: no $MOD.dll/.exe in $GAME_PC_DIR -- exported NOTHING" >&2; exit 2; }

HEADLESS=${GHIDRA_HOME:+$GHIDRA_HOME/support/analyzeHeadless}
HEADLESS=${HEADLESS:-$(command -v analyzeHeadless || true)}
[ -n "$HEADLESS" ] && [ -x "$HEADLESS" ] || {
    echo "ghidra_export: analyzeHeadless not found (set GHIDRA_HOME) -- exported NOTHING" >&2
    exit 2; }

PROJ=${GHIDRA_PROJ:-$ROOT/scratch/ghidra}
OUT=$ROOT/scratch/recomp/$MOD.json
mkdir -p "$PROJ" "$ROOT/scratch/recomp" "$ROOT/scratch/logs"
LOG=$ROOT/scratch/logs/ghidra-$MOD.log

# Provenance. "Already in the project" is NOT the same as "imported from THIS
# file", and treating them as the same silently recompiled a libIGSg.dll that
# was not the shipped one for a whole session (issue #12). The hash of the file
# each program was imported from is recorded here, and a mismatch re-imports
# rather than reusing.
STAMP=$PROJ/imported-from.sha256
mkdir -p "$PROJ"
touch "$STAMP"
WANT=$(sha256sum "$BIN" | cut -d' ' -f1)
HAVE=$(awk -v m="$(basename "$BIN")" '$2==m {print $1}' "$STAMP" | tail -1)
if [ -n "$HAVE" ] && [ "$HAVE" != "$WANT" ]; then
    echo "ghidra_export: the project's $(basename "$BIN") came from a DIFFERENT file"
    echo "  (recorded $HAVE, want $WANT) -- re-importing rather than reusing it." >&2
    REANALYZE=1
fi
if [ -z "$HAVE" ] && grep -q ":$(basename "$BIN"):" "$PROJ/xmen2.rep/idata/~index.dat" 2>/dev/null; then
    echo "ghidra_export: the project already has $(basename "$BIN") but nothing"
    echo "  records which file it came from -- re-importing to be sure." >&2
    REANALYZE=1
fi

# Import once, then reuse the analysed program. Re-importing an already-imported
# binary makes Ghidra create "libIGCore.dll_1", which then gets analysed from
# scratch and silently diverges from the one everything else refers to.
if [ -n "$REANALYZE" ] || ! grep -q ":$(basename "$BIN"):" "$PROJ/xmen2.rep/idata/~index.dat" 2>/dev/null; then
    echo "== import + analyse $(basename "$BIN") (this is the slow part) =="
    "$HEADLESS" "$PROJ" xmen2 \
        -import "$BIN" -overwrite \
        -scriptPath "$ROOT/tools/ghidra_scripts" \
        >"$LOG" 2>&1 || { echo "ghidra_export: import failed, see $LOG" >&2; exit 1; }
    grep -v " $(basename "$BIN")\$" "$STAMP" > "$STAMP.new" 2>/dev/null || true
    mv "$STAMP.new" "$STAMP"
    echo "$WANT $(basename "$BIN")" >> "$STAMP"
else
    echo "== $(basename "$BIN") already in the project; skipping analysis =="
fi

# Seeding: addresses the RUNTIME found that static analysis never marked as
# code. The CRT's static-constructor tables are the usual source -- their
# targets are referenced only by a data pointer in .rdata, so nothing in the
# database points at them and a reference-driven pass cannot find them.
if [ -n "$SEEDFILE" ]; then
    [ -s "$SEEDFILE" ] || { echo "ghidra_export: $SEEDFILE is empty -- seeded NOTHING" >&2; exit 2; }
    ADDRS=$(tr -s ' \n' ',' < "$SEEDFILE" | sed 's/,$//')
    echo "== seed $(grep -c . "$SEEDFILE") address(es) from $SEEDFILE =="
    ADD_FUNCS="$ADDRS" "$HEADLESS" "$PROJ" xmen2 \
        -process "$(basename "$BIN")" -noanalysis \
        -scriptPath "$ROOT/tools/ghidra_scripts" \
        -postScript AddFunctions.py \
        >>"$LOG" 2>&1 || { echo "ghidra_export: seeding failed, see $LOG" >&2; exit 1; }
    grep -E "^ADD:" "$LOG" | tail -3
fi

# Splitting: an address Ghidra swallowed into an EARLIER function. Seeding
# cannot fix these -- AddFunctions.py reports "already inside a function" and
# creates nothing, so a discovery loop spins on them forever.
if [ -n "$SPLITFILE" ]; then
    [ -s "$SPLITFILE" ] || { echo "ghidra_export: $SPLITFILE is empty -- split NOTHING" >&2; exit 2; }
    ADDRS=$(tr -s ' \n' ',' < "$SPLITFILE" | sed 's/,$//')
    echo "== split $(grep -c . "$SPLITFILE") address(es) out of their containing functions =="
    SPLIT_FUNCS="$ADDRS" "$HEADLESS" "$PROJ" xmen2 \
        -process "$(basename "$BIN")" -noanalysis \
        -scriptPath "$ROOT/tools/ghidra_scripts" \
        -postScript SplitFunction.py \
        >>"$LOG" 2>&1 || { echo "ghidra_export: splitting failed, see $LOG" >&2; exit 1; }
    grep -E "^SPLIT:" "$LOG" | tail -4
fi

# Bulk seeding from vtables and dispatch tables. The runtime finds indirect
# call targets one at a time, which for virtual calls means one function per
# rebuild; the tables holding them can be enumerated statically instead.
if [ -n "${SEED_TABLES:-}" ]; then
    echo "== seed from pointer tables (runs of >=${SEED_MIN_RUN:-3} code pointers) =="
    SEED_MIN_RUN=${SEED_MIN_RUN:-3} SEED_MAX=${SEED_MAX:-0} \
    "$HEADLESS" "$PROJ" xmen2 \
        -process "$(basename "$BIN")" -noanalysis \
        -scriptPath "$ROOT/tools/ghidra_scripts" \
        -postScript SeedPointerTables.py \
        >>"$LOG" 2>&1 || { echo "ghidra_export: table seeding failed, see $LOG" >&2; exit 1; }
    grep -E "^SEED:" "$LOG" | tail -3
fi

echo "== export functions -> $OUT =="
FUNCS_OUT=$OUT FUNCS_MAX=${FUNCS_MAX:-0} \
"$HEADLESS" "$PROJ" xmen2 \
    -process "$(basename "$BIN")" -noanalysis \
    -scriptPath "$ROOT/tools/ghidra_scripts" \
    -postScript ExportFuncs.py \
    >>"$LOG" 2>&1 || { echo "ghidra_export: export failed, see $LOG" >&2; exit 1; }

# A missing or empty JSON must not read as success: the recompiler would then
# be handed "this module has no functions" and emit a pure forwarder.
[ -s "$OUT" ] || { echo "ghidra_export: $OUT is missing or empty -- see $LOG" >&2; exit 1; }
python3 - "$OUT" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
n = len(d.get("functions", []))
if n == 0:
    sys.exit("ghidra_export: %s has ZERO functions -- that is a failed export, "
             "not an empty module" % sys.argv[1])
ins = sum(len(f.get("ins", [])) for f in d["functions"])
print("ghidra_export: %s -- %d functions, %d instructions, image_base 0x%x"
      % (sys.argv[1], n, ins, d.get("image_base", 0)))
PY
