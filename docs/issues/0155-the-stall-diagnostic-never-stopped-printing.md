# 0155 — the stall diagnostic never stopped printing, so it caused the stall

- **State items:** S021
- **Status:** fixed in `src/native/x86rt_native.c`

## What it was

`x86_ring_dump` prints the last 96 boundary crossings. Its loop bound was the
live counter:

```c
unsigned long n = g_ring_n < RING ? g_ring_n : RING, i;
...
for (i = g_ring_n - n; i < g_ring_n; i++)
```

`g_ring_n` is written by the guest every time it crosses to the host. On the
desktop the guest shares this thread, so it cannot append while the dump runs
and the loop ends after 96 lines. In the browser the guest runs on its own
worker: it kept appending faster than the dump could print, `i` never caught
`g_ring_n`, and one call became an endless stream.

The header said `last 96 of N crossings` exactly once, which is why the output
did not look like a dump at all — it looked like live tracing that somebody had
left armed. Nothing was armed. It was one call that had not returned.

## Why it was self-inflicting

The dump's only caller during a run is the heartbeat's stall detector:

> the guest is EXECUTING but has presented nothing for 10.0s. Dumping the
> boundary ring as a snapshot.

Each printed line in the browser is a cross-thread console post that the
running worker waits behind. So the moment the port noticed the guest was not
reaching `Present`, it began an unbounded stream of blocking posts on the very
thread that had to reach `Present` — and the stall it had reported became
permanent. The diagnostic was the cause.

Measured with `tools/web_console.py` on the Dead Zone route:
**117,318 console lines in 20 seconds**, about 6,000 a second, of which 54,780
were the dump's `(no registered module)` continuation lines.

## The fix

Read the end of the ring once, into a local, and iterate to that. A snapshot
taken during a run reports the ring as it was at entry; that is what "snapshot"
means, and it is also the only bound that terminates.

## Measured, same route, same page command line

| | before | after |
|---|---|---|
| console lines | 117,318 in 20 s | 9,551 in 300 s |
| ring dumps that completed | 0 | 1, exactly 96 entries |
| the run after the stall notice | never presented again | kept presenting and drawing |

## What found it

`tools/web_console.py`, written for this. WebLua's `console` reads a small ring
that the flood overran, so the evidence asked for was always gone: a Dead Zone
route left 50 lines in it, none of them the heartbeat. Attaching to Chrome over
CDP and recording every console line is what made the 117,318 visible, and the
absence of a second `last 96 of N crossings` header is what identified one
non-returning call rather than repeated dumps.
