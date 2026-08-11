#!/usr/bin/env bash
#
# Export one module's functions to JSON for the offline recompiler.
#
#   tools/ghidra_export.sh <module-basename> [--reanalyze]
#   tools/ghidra_export.sh <module-basename> --seed <file-of-hex-addresses>
#   tools/ghidra_export.sh <module-basename> --split-at <file-of-hex-addresses>
#   tools/ghidra_export.sh <module-basename> --merge <file-of-truncated-fns>
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

# Run one headless step, and PROVE it ran.
#
# Every step below appends to the SAME log and then greps the WHOLE file, so a
# step that died still printed the PREVIOUS run's lines and read as success.
# That is not hypothetical: a Jython SyntaxError in RecreateFunction.py (one
# non-ASCII character in a comment) announced "recreate 42 function bodies",
# showed six lines from the run before it, and the pipeline went on to export,
# re-emit and rebuild as though 42 bodies had been repaired. analyzeHeadless
# exits 0 when a script fails to compile, so the `||` guard never fired.
#
# So: remember where the log ended, run, and look ONLY at what this run added.
# No lines matching the step's own prefix means the step did nothing, which is
# a refusal -- never a silent pass onto the next stage.
#
#   run_step <label> <line-prefix regex> <lines to show> <command...>
run_step() {
    _label=$1; _prefix=$2; _keep=$3; shift 3
    _before=$(wc -l < "$LOG" 2>/dev/null || echo 0)
    "$@" >>"$LOG" 2>&1 || {
        echo "ghidra_export: $_label failed, see $LOG" >&2; exit 1; }
    _added=$(tail -n +$((_before + 1)) "$LOG")
    # MATCH ONCE, INTO A VARIABLE -- never `... | grep -q`.
    #
    # `grep -q` exits at the FIRST match and closes the pipe; with `pipefail`
    # the SIGPIPE that kills the writer (141) becomes the pipeline's status, so
    # the guard reported "this step produced NOTHING" for a step whose output
    # was merely LARGE. It fired for the first time on a seeding run that
    # created 1267 functions -- the guard against a step that did nothing,
    # misfiring precisely on the step that did the most. Nothing about the
    # message hinted at volume, so it read as a real refusal.
    _errs=$(printf '%s\n' "$_added" |
            grep -E '^(SyntaxError|Traceback|[A-Za-z]*Error):' || true)
    if [ -n "$_errs" ]; then
        echo "ghidra_export: $_label -- the Ghidra script did not RUN:" >&2
        printf '%s\n' "$_errs" | head -3 >&2
        exit 1
    fi
    _hits=$(printf '%s\n' "$_added" | grep -E "$_prefix" || true)
    if [ -z "$_hits" ]; then
        echo "ghidra_export: $_label produced NO '$_prefix' output of its own." >&2
        echo "  Whatever is in $LOG above belongs to an EARLIER run. Refusing" >&2
        echo "  rather than exporting a database this step did not change." >&2
        exit 1
    fi
    printf '%s\n' "$_hits" | tail -"$_keep"
}

if [ "${1:-}" = "--selftest" ]; then
    # Proof that the step guard FIRES. It exists because the failure it catches
    # is invisible by construction: a step that dies still prints an earlier
    # run's lines, so "it looked fine" is exactly what a broken run looks like.
    # Needs no Ghidra and no game install, so it is wired in as a ctest.
    LOG=$(mktemp); fails=0
    run_step_selftest() {                 # <name> <expect-exit> <prefill> <emit>
        printf '%s' "$3" > "$LOG"
        if ( run_step "$1" '^ADD:' 3 sh -c "printf '%s' \"\$0\"" "$4" ) \
                >/dev/null 2>&1; then got=0; else got=1; fi
        if [ "$got" != "$2" ]; then
            echo "  FAIL: $1 -- run_step exited $got, expected $2"; fails=$((fails+1))
        else
            echo "  ok: $1"
        fi
    }
    echo "ghidra_export --selftest: does the step guard fire?"
    run_step_selftest "a step that reports its work"    0 ""                 "ADD: 3 created
"
    run_step_selftest "a step that reports NOTHING"     1 ""                 "some noise
"
    run_step_selftest "a Jython script that would not compile" 1 ""          "SyntaxError: Non-ASCII character
"
    run_step_selftest "a silent step after an EARLIER run's output" \
                                                        1 "ADD: 7 created
"                                                                            "some noise
"
    # The volume case, and why it is written differently from the four above.
    #
    # A step that emits a LOT used to be refused as a step that emitted
    # NOTHING: `grep -q` closes the pipe at the first match and `pipefail`
    # turns the writer's SIGPIPE into the pipeline's status. It misfired on a
    # seeding run that created 1267 functions -- the guard against a step that
    # did nothing, firing on the step that did the most.
    #
    # The output is GENERATED IN THE CHILD rather than passed as an argument,
    # because ~700 KB in one argv element is past this shell's limit and the
    # case would fail for a reason that has nothing to do with the guard.
    _big_cmd="seq 1 20000 | sed 's/^/ADD: created function at 0x0000/'"
    printf '' > "$LOG"
    if ( run_step "volume" '^ADD:' 3 sh -c "$_big_cmd" ) >/dev/null 2>&1
    then got=0; else got=1; fi
    if [ "$got" != 0 ]; then
        echo "  FAIL: a step that reports a LOT of work -- run_step exited $got, expected 0"
        fails=$((fails+1))
    else
        echo "  ok: a step that reports a LOT of work (700 KB, past the pipe buffer)"
    fi
    echo "ghidra_export --selftest: $([ $fails -eq 0 ] && echo PASSED || echo FAILED) ($fails failure(s))"
    rm -f "$LOG"
    exit $fails
fi

MOD=${1:?usage: ghidra_export.sh <module-basename> [--reanalyze|--seed <file>]}
REANALYZE=""
SEEDFILE=""
SPLITFILE=""
MERGEFILE=""
case ${2:-} in
    --reanalyze) REANALYZE=1 ;;
    --seed)      SEEDFILE=${3:?--seed needs a file of hex addresses} ;;
    --split-at)  SPLITFILE=${3:?--split-at needs a file of hex addresses} ;;
    --merge)     MERGEFILE=${3:?--merge needs a file of hex addresses} ;;
    --recreate)  RECREATE=${3:?--recreate needs a comma-separated hex address list} ;;
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
    ADD_FUNCS="$ADDRS" run_step seeding '^ADD:' 3 \
        "$HEADLESS" "$PROJ" xmen2 \
        -process "$(basename "$BIN")" -noanalysis \
        -scriptPath "$ROOT/tools/ghidra_scripts" \
        -postScript AddFunctions.py
fi

# Splitting: an address Ghidra swallowed into an EARLIER function. Seeding
# cannot fix these -- AddFunctions.py reports "already inside a function" and
# creates nothing, so a discovery loop spins on them forever.
if [ -n "$SPLITFILE" ]; then
    [ -s "$SPLITFILE" ] || { echo "ghidra_export: $SPLITFILE is empty -- split NOTHING" >&2; exit 2; }
    ADDRS=$(tr -s ' \n' ',' < "$SPLITFILE" | sed 's/,$//')
    echo "== split $(grep -c . "$SPLITFILE") address(es) out of their containing functions =="
    SPLIT_FUNCS="$ADDRS" run_step splitting '^SPLIT:' 4 \
        "$HEADLESS" "$PROJ" xmen2 \
        -process "$(basename "$BIN")" -noanalysis \
        -scriptPath "$ROOT/tools/ghidra_scripts" \
        -postScript SplitFunction.py
fi

# Bulk seeding from vtables and dispatch tables. The runtime finds indirect
# call targets one at a time, which for virtual calls means one function per
# rebuild; the tables holding them can be enumerated statically instead.
if [ -n "${SEED_TABLES:-}" ]; then
    echo "== seed from pointer tables (runs of >=${SEED_MIN_RUN:-3} code pointers) =="
    SEED_MIN_RUN=${SEED_MIN_RUN:-3} SEED_MAX=${SEED_MAX:-0} \
    SEED_SCAN_DATA=${SEED_SCAN_DATA:-} \
    SEED_INSIDE_OUT="$ROOT/scratch/recomp/$MOD.split" \
    run_step "table seeding" '^SEED:' 3 \
        "$HEADLESS" "$PROJ" xmen2 \
        -process "$(basename "$BIN")" -noanalysis \
        -scriptPath "$ROOT/tools/ghidra_scripts" \
        -postScript SeedPointerTables.py
fi

# Merging: a function cut off by a spurious one starting inside it. The
# inverse of --split-at, and the check that keeps it honest lives in the
# script: an inner function with real CALL references is a genuine entry point
# and is skipped rather than deleted.
if [ -n "$MERGEFILE" ]; then
    [ -s "$MERGEFILE" ] || { echo "ghidra_export: $MERGEFILE is empty -- merged NOTHING" >&2; exit 2; }
    ADDRS=$(tr -s ' \n' ',' < "$MERGEFILE" | sed 's/,$//')
    echo "== merge $(grep -c . "$MERGEFILE") truncated function(s) =="
    MERGE_FUNCS="$ADDRS" run_step merging '^MERGE: [0-9]+ repaired' 1 \
        "$HEADLESS" "$PROJ" xmen2 \
        -process "$(basename "$BIN")" -noanalysis \
        -scriptPath "$ROOT/tools/ghidra_scripts" \
        -postScript MergeTruncated.py
fi

# Rebuilding a body that has HOLES -- a switch whose case blocks belong to no
# function. Neither --split-at nor --merge can fix that: one carves, the other
# absorbs inner FUNCTIONS, and orphaned blocks are not functions. See
# RecreateFunction.py and issue #21.
if [ -n "${RECREATE:-}" ]; then
    echo "== recreate $(echo "$RECREATE" | tr ',' '\n' | grep -c .) function body/bodies from control flow =="
    RECREATE_FUNCS="$RECREATE" RECREATE_EXPECT="${RECREATE_EXPECT:-}" \
    RECREATE_JUMPTABLES="${RECREATE_JUMPTABLES:-1}" \
    run_step recreate '^RECREATE: ([0-9]+ recreated|POSTCONDITION|postcondition|no case label|[0-9]+ function\(s\) LEFT)' 4 \
        "$HEADLESS" "$PROJ" xmen2 \
        -process "$(basename "$BIN")" -noanalysis \
        -scriptPath "$ROOT/tools/ghidra_scripts" \
        -postScript RecreateFunction.py
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
