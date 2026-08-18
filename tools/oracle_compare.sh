#!/usr/bin/env bash
# The oracle comparison harness: record the same guest functions in the port
# and in the stock game, then diff the two streams.
#
#   tools/oracle_compare.sh check          is the harness wired, end to end?
#   tools/oracle_compare.sh build          regenerate + build both sides
#   tools/oracle_compare.sh port [args]    run the port, recording
#   tools/oracle_compare.sh stock [secs]   run the stock game, recording
#   tools/oracle_compare.sh diff           compare the two streams
#
# The two runs do not have to be driven identically. Records are lined up by
# (probe, call index), and a call whose INPUTS differ is reported as drift
# rather than as a defect -- what matters is the first call that got the same
# numbers as the real engine and returned different ones.
#
# WHY `check` EXISTS. Every part of this can fail silently in the same shape:
# the wrap not binding, the isolate list not regenerated, the stock hooks
# refusing to install, a stale manifest on one side. All of them produce an
# empty or short stream, and an empty stream compared against anything reads as
# agreement unless something refuses. `check` proves each link BEFORE a run is
# spent on it -- these runs cost five to nine minutes each.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
CMD=${1:-check}
shift 2>/dev/null || true

PORT_BIN=$ROOT/scratch/logs/probe_port.bin
STOCK_RUN=${X2_STOCK_RUN:-stocklog}
STOCK_BIN=$ROOT/scratch/run/$STOCK_RUN/probe_stock.bin
NATIVE=$ROOT/scratch/build-native/x2native

fail() { echo "oracle_compare: $*" >&2; exit 1; }

regen() {
    # gen_probes.py writes the probe wraps AND the --isolate lists (the only
    # remaining user of the isolate mechanism -- native overrides route through
    # the dispatcher's override slot now).
    python3 "$ROOT/tools/gen_probes.py" >/dev/null \
        || fail "tools/gen_probes.py refused; NOTHING was regenerated"
}

case "$CMD" in
check)
    echo "== manifest and generated artifacts =="
    python3 "$ROOT/tools/gen_probes.py" --selftest | tail -1
    python3 "$ROOT/tools/gen_probes.py" --check \
        || fail "the generated artifacts are stale -- run tools/gen_probes.py"
    python3 "$ROOT/tools/oraclediff.py" --selftest | tail -1

    echo
    echo "== the port's hooks actually bound =="
    [ -x "$NATIVE" ] || fail "$NATIVE does not exist; run '$0 build'"
    # NOT a grep of the disassembly for call sites. These functions are
    # exported and reached from other modules through the guest's own import
    # tables, so an intra-module call count of zero is normal and proves
    # nothing. What must be true is that the entry the DISPATCHER resolves for
    # each address is the wrapper -- which only the linked binary can say, so
    # it says it, in the shipping artifact.
    bindout=$("$NATIVE" --no-window --probe-selftest 2>&1) || true
    echo "$bindout" | grep -E "probe .* (NOT WRAPPED|IS NOT LINKED|no entry)" \
        && fail "at least one probe did not bind; capturing now would produce
  a stream with silent gaps, which compares as agreement"
    echo "$bindout" | grep -E "bound to their wrapper" \
        || fail "the binding check did not run at all"

    echo
    echo "== the recorder writes what the guest holds =="
    "$NATIVE" --no-window --probe-selftest 2>&1 | tail -3

    echo
    echo "== the stock side =="
    if [ -f "$ROOT/scratch/build-proxy/d3d8.dll" ]; then
        echo "  built: scratch/build-proxy/d3d8.dll"
        i686-w64-mingw32-nm "$ROOT/scratch/build-proxy/d3d8.dll" 2>/dev/null \
            | grep -q probe_stub_0 \
            && echo "  ok    the probe stubs are linked into it" \
            || echo "  NOTE  no probe_stub symbols; rebuild with '$0 build'"
    else
        echo "  NOT BUILT -- run '$0 build' (needs mingw32 + a Wine prefix)"
    fi
    ;;

build)
    regen
    echo "== re-emitting the probed modules so --wrap can bind =="
    # A probed function must be in its own translation unit. gen_probes.py
    # has just written the isolate lists; the emit has to be redone or the
    # wrap silently binds at compile time.
    for m in $(python3 - <<'EOF'
import sys, os
sys.path.insert(0, "tools")
import gen_probes
print(" ".join(sorted(set(m for m, _ in gen_probes.isolate_eps()))))
EOF
    ); do
        [ -f "$ROOT/scratch/recomp/$m.json" ] \
            || fail "scratch/recomp/$m.json is missing; run tools/ghidra_export.sh $m"
        echo "  $m"
        python3 "$ROOT/tools/recomp.py" emit "$ROOT/scratch/recomp/$m.json" \
            "$ROOT/src/recomp/$m.c" --split 1500 \
            --isolate "$ROOT/scratch/recomp/$m.isolate" >/dev/null \
            || fail "re-emitting $m FAILED"
    done
    cmake -S "$ROOT" -B "$ROOT/scratch/build-native" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null || fail "cmake configure FAILED"
    cmake --build "$ROOT/scratch/build-native" --target x2native \
        -j"$(nproc)" >/dev/null || fail "building x2native FAILED"
    echo "  built $NATIVE"
    if command -v i686-w64-mingw32-gcc >/dev/null; then
        "$ROOT/tools/build_stocklog.sh" "$STOCK_RUN" \
            || fail "building the stock proxy FAILED"
    else
        echo "  SKIPPED the stock side: i686-w64-mingw32-gcc is not installed."
        echo "  Only the port can be recorded, and one stream compares to nothing."
    fi
    ;;

port)
    regen
    mkdir -p "$ROOT/scratch/logs"
    rm -f "$PORT_BIN"
    LOG=$ROOT/scratch/logs/oracle_port.log
    # DRIVEN and headless -- nobody has to sit at it. The same profile
    # smoke_loop.sh uses to close the loop, so this run reaches gameplay rather
    # than photographing the intro. X2_MAX_FRAMES ends it cleanly a little past
    # the last press, which gives a real exit status instead of a timeout.
    echo "oracle_compare: recording the port to $PORT_BIN, DRIVEN by"
    echo "  $(tools/drive.sh port)"
    X2_PROBE=$PORT_BIN \
    X2_INPUT_SCRIPT="$(tools/drive.sh port)" \
    X2_MAX_FRAMES=${X2_MAX_FRAMES:-4200} X2_UNPACED=1 X2_HEARTBEAT=60 \
        timeout "${X2_TIMEOUT:-420}" "$NATIVE" --no-window --d3d8 --run \
        > "$LOG" 2>&1
    echo "oracle_compare: exit $?, $(stat -c%s "$PORT_BIN" 2>/dev/null || echo 0) byte(s) recorded"
    grep -E "oracle_trace|INJECTING" "$LOG" | head -20
    grep -E "probe .* fired" "$LOG" | tail -1
    ;;

stock)
    SECS=${1:-540}
    [ -f "$ROOT/scratch/run/$STOCK_RUN/d3d8.dll" ] \
        || fail "scratch/run/$STOCK_RUN is not staged; run '$0 build'"
    rm -f "$STOCK_BIN"
    # Driven too, by the same profile in seconds. An undriven control run only
    # ever sees the intro, which is no control for anything past the menu.
    echo "oracle_compare: the control is DRIVEN by $(tools/drive.sh stock)"
    X2_KEYS="$(tools/drive.sh stock)" "$ROOT/tools/run_shim.sh" "$STOCK_RUN" "$SECS"
    echo "oracle_compare: $(stat -c%s "$STOCK_BIN" 2>/dev/null || echo 0) byte(s) recorded"
    sed -n '1,40p' "$ROOT/scratch/run/$STOCK_RUN/probe_stock.log" 2>/dev/null
    ;;

diff)
    for f in "$PORT_BIN" "$STOCK_BIN"; do
        [ -f "$f" ] || fail "$f does not exist. Both sides must have been
  recorded; one stream compares to nothing. See '$0 port' and '$0 stock'."
    done
    python3 "$ROOT/tools/oraclediff.py" "$PORT_BIN" "$STOCK_BIN" "$@"
    ;;

*)
    sed -n '2,20p' "$0"
    exit 2
    ;;
esac
