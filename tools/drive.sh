#!/usr/bin/env bash
# The driving profile: what to press to get the game from its intro to
# gameplay, WITHOUT anybody at the keyboard.
#
#   tools/drive.sh port    -> an X2_INPUT_SCRIPT string (the native build)
#   tools/drive.sh stock   -> an X2_KEYS string (the Wine control)
#   tools/drive.sh --check -> prove both are non-empty and well formed
#
# ONE definition, two syntaxes. The port injects into the DirectInput buffer
# and can count FRAMES; the control is driven from outside with xdotool and can
# only count wall-clock SECONDS, because nothing outside the process can see
# the game's frame counter. Those are genuinely different clocks, so the two
# strings are different -- but they are here, next to each other, rather than
# copied into every script that drives a run. smoke_loop.sh and
# oracle_compare.sh both read this, so a profile that stops working gets fixed
# once.
#
# WHY REPEAT WINDOWS AND NOT SINGLE PRESSES. The difficulty dialog opens a few
# frames earlier or later on each run, and the six intro movies take a
# different wall-clock time every time. A press at one exact instant is a coin
# toss: two control runs were lost to one landing before the menu appeared, and
# a smoke run that missed stayed in the menus for good. Blanketing the
# uncertainty is harmless -- past the menu the level ignores the extra Returns.
set -u

# The native build, in FRAMES (the `f` prefix). Measured on runs that close the
# loop; these are a property of the GAME -- how many frames its movies, menus
# and level load take -- not of this machine.
PORT_SCRIPT="f2600-2900/50:Return,f3150-3260/40:Escape,f4044+40:Down,f4135+40:Return"

# The Wine control, in SECONDS. Coarser and more generous for the same reason:
# a software rasteriser under Wine reaches the menu at a time that varies by
# tens of seconds between runs.
STOCK_KEYS="195-300/12:Return,380-500/20:Return"

case "${1:-}" in
port)  printf '%s' "$PORT_SCRIPT" ;;
stock) printf '%s' "$STOCK_KEYS" ;;
--check)
    rc=0
    for side in port stock; do
        s=$("$0" "$side")
        n=$(printf '%s' "$s" | tr ',' '\n' | grep -c .)
        if [ -z "$s" ] || [ "$n" -eq 0 ]; then
            echo "drive: the $side profile is EMPTY -- a run using it would be"
            echo "       undriven, and an undriven run photographs whatever the"
            echo "       intro happens to be showing."
            rc=1
            continue
        fi
        # Every entry must name a key and a time, or it is silently skipped by
        # the consumer -- which is how an "undriven" run looks driven.
        bad=$(printf '%s' "$s" | tr ',' '\n' | grep -vc ':' || true)
        if [ "$bad" -ne 0 ]; then
            echo "drive: $bad entr(y/ies) in the $side profile have no ':'"
            rc=1
        else
            echo "drive: $side -- $n press window(s): $s"
        fi
    done
    exit $rc
    ;;
*)
    sed -n '2,12p' "$0"
    exit 2
    ;;
esac
