#!/usr/bin/env python3
"""
The oracle cache -- a stock Wine run is answered from disk the second time.

## Why this exists

`tools/run_shim.sh stock 300` takes five minutes, spins up Xvfb, a Wine prefix
and a software Vulkan rasteriser, and produces the SAME frames every time for
the same driving script. In one session the identical control run -- reach the
opening red chamber, photograph it, measure its brightness -- was executed five
times, twenty-five minutes of wall clock, to answer questions that differed
only in what was asked of the pixels afterwards. That is the defect this fixes:
the run is expensive, its output is stable, and nothing was keeping it.

So: run once, keep everything (every sample, the log, the measured numbers),
and key it on what actually determines the result. Ask again with the same key
and you get the stored answer in milliseconds.

## The key, and what invalidates it

    rundir name · seconds · X2_KEYS · X2_SAMPLES · X2_RES · RUN_ARGS · X2_EXE
    + a fingerprint of the run directory (entry names, sizes, symlink targets)

The fingerprint is the part that matters: a run directory is a symlink farm
over the install with the built DLL swapped in, so rebuilding that DLL MUST
miss the cache. Without it the cache would confidently hand back yesterday's
frame for today's binary, which is worse than no cache at all.

## What a hit and a miss each say

A HIT prints the key, the age of the entry, and the parameters it was recorded
under, so a cached answer can never be mistaken for a fresh observation. A MISS
says so before it starts, names the expected cost, and runs. `--force` re-runs
and replaces. Nothing is ever silently served.

## Self-test

`oracle.py --selftest` proves the cache both HITS and MISSES, using a stub
runner instead of Wine (X2_ORACLE_RUNNER), so it needs no game install. A cache
that has never been shown to miss is a cache that will serve a stale frame.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "scratch", "oracle")

# The environment variables that change what a run PRODUCES. Anything not here
# is either irrelevant (log paths) or would make every entry unique.
RUN_ENV = ("X2_KEYS", "X2_SAMPLES", "X2_RES", "RUN_ARGS", "X2_EXE",
           "X2_D3D", "X2_MUTE")


def fingerprint_rundir(rundir):
    """What the run directory contains, cheaply and honestly.

    Top-level entries only, with size and symlink target. The farm points at an
    unchanging install; what DOES change between runs is the handful of real
    files staged into it (the recompiled DLL), and those are top-level. A
    missing directory is an ERROR, never an empty fingerprint -- an empty
    fingerprint would make every miss look like a hit on the same key.
    """
    if not os.path.isdir(rundir):
        raise SystemExit("oracle: run directory %s does not exist -- "
                         "fingerprinted NOTHING" % rundir)
    parts = []
    for name in sorted(os.listdir(rundir)):
        p = os.path.join(rundir, name)
        if os.path.islink(p):
            parts.append("%s -> %s" % (name, os.readlink(p)))
        else:
            try:
                st = os.stat(p)
                parts.append("%s %d %d" % (name, st.st_size, int(st.st_mtime)))
            except OSError as e:
                parts.append("%s ?%s" % (name, e.errno))
    if not parts:
        raise SystemExit("oracle: run directory %s is EMPTY -- refusing to key "
                         "a cache entry on nothing" % rundir)
    return parts


def make_key(name, secs, env, fp):
    blob = json.dumps({"name": name, "secs": secs,
                       "env": {k: env.get(k, "") for k in RUN_ENV},
                       "rundir": fp}, sort_keys=True)
    return hashlib.sha256(blob.encode()).hexdigest()[:16], blob


def measure(png):
    """Brightness of one capture: mean luma and the two tails.

    These are the numbers every issue-62 comparison is written in, so they are
    computed ONCE here and stored. Returns None if the image cannot be read --
    reported by the caller as a failed sample, never as a black frame.
    """
    try:
        out = subprocess.run(
            ["python3", "-c", r"""
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("L")
px = list(im.getdata())
n = len(px)
print("%f %f %f %d" % (sum(px)/n,
                       sum(1 for p in px if p < 16)/n,
                       sum(1 for p in px if p > 128)/n, n))
""", png], capture_output=True, text=True, timeout=120)
        if out.returncode != 0:
            return None
        mean, lo, hi, n = out.stdout.split()
        return {"mean_luma": float(mean), "frac_lt16": float(lo),
                "frac_gt128": float(hi), "pixels": int(n)}
    except Exception:                               # noqa: BLE001 -- reported
        return None


def store(key, blob, name, secs, env, shots, log, seconds_taken):
    d = os.path.join(CACHE, key)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(os.path.join(d, "shots"))
    kept = []
    for src in shots:
        dst = os.path.join(d, "shots", os.path.basename(src))
        shutil.copyfile(src, dst)
        kept.append({"file": os.path.relpath(dst, ROOT),
                     "metrics": measure(dst)})
    if log and os.path.exists(log):
        shutil.copyfile(log, os.path.join(d, "run.log"))
    man = {"key": key, "name": name, "secs": secs,
           "env": {k: env.get(k, "") for k in RUN_ENV},
           "recorded_at": int(time.time()), "took_seconds": round(seconds_taken, 1),
           "shots": kept, "keyed_on": json.loads(blob)}
    with open(os.path.join(d, "manifest.json"), "w") as f:
        json.dump(man, f, indent=2)
    return man


def describe(man, cached):
    age = int(time.time()) - man["recorded_at"]
    src = ("CACHE HIT -- served from disk, recorded %s ago (%s s of Wine "
           "saved)" % (human(age), man["took_seconds"])) if cached else \
          "FRESH RUN -- recorded just now in %s s" % man["took_seconds"]
    print("oracle: %s" % src)
    print("oracle: key %s  rundir %s  %s s" % (man["key"], man["name"], man["secs"]))
    for k in RUN_ENV:
        v = man["env"].get(k, "")
        if v:
            print("oracle:   %s=%s" % (k, v))
    if not man["shots"]:
        print("oracle: NO captures stored -- this entry proves nothing about "
              "what was on screen")
    for s in man["shots"]:
        m = s["metrics"]
        if m is None:
            print("oracle:   %s  UNREADABLE -- not a measurement" % s["file"])
        else:
            print("oracle:   %s  mean %.1f  frac<16 %.3f  frac>128 %.3f  (%d px)"
                  % (s["file"], m["mean_luma"], m["frac_lt16"],
                     m["frac_gt128"], m["pixels"]))


def human(sec):
    if sec < 90:
        return "%d s" % sec
    if sec < 5400:
        return "%d min" % (sec // 60)
    if sec < 172800:
        return "%d h" % (sec // 3600)
    return "%d days" % (sec // 86400)


def cmd_run(argv):
    force = "--force" in argv
    argv = [a for a in argv if a != "--force"]
    if not argv:
        raise SystemExit("usage: oracle.py run <rundir-name> [seconds] [--force]")
    name = argv[0]
    secs = argv[1] if len(argv) > 1 else "40"
    env = dict(os.environ)
    rundir = os.path.join(ROOT, "scratch", "run", name)
    fp = fingerprint_rundir(rundir)
    key, blob = make_key(name, secs, env, fp)
    man_path = os.path.join(CACHE, key, "manifest.json")

    if os.path.exists(man_path) and not force:
        with open(man_path) as f:
            describe(json.load(f), True)
        return 0
    if force and os.path.exists(man_path):
        print("oracle: --force given, discarding the entry for key %s" % key)
    else:
        print("oracle: CACHE MISS for key %s -- running Wine, expect about "
              "%s s" % (key, secs))

    runner = env.get("X2_ORACLE_RUNNER") or os.path.join(ROOT, "tools", "run_shim.sh")
    t0 = time.time()
    r = subprocess.run([runner, name, secs], cwd=ROOT)
    took = time.time() - t0
    if r.returncode != 0:
        raise SystemExit("oracle: the run FAILED (exit %d) -- nothing cached, "
                         "and no measurement exists" % r.returncode)

    shots_dir = os.path.join(ROOT, "scratch", "screenshots")
    shots = sorted(os.path.join(shots_dir, f) for f in os.listdir(shots_dir)
                   if f.startswith(name + ".") and f.endswith(".png")
                   or f == name + ".png")
    shots = [s for s in shots if os.path.getmtime(s) >= t0 - 1]
    if not shots:
        print("oracle: the run produced NO capture newer than its own start -- "
              "caching an entry with no image, which proves nothing")
    log = os.path.join(ROOT, "scratch", "logs", name + ".log")
    man = store(key, blob, name, secs, env, shots, log, took)
    describe(man, False)
    return 0


def cmd_list(argv):
    if not os.path.isdir(CACHE):
        print("oracle: no cache directory yet (%s) -- 0 entries" % CACHE)
        return 0
    ents = []
    for k in os.listdir(CACHE):
        p = os.path.join(CACHE, k, "manifest.json")
        if os.path.exists(p):
            with open(p) as f:
                ents.append(json.load(f))
    if not ents:
        print("oracle: cache directory exists but holds 0 entries")
        return 0
    ents.sort(key=lambda m: -m["recorded_at"])
    print("oracle: %d cached run%s" % (len(ents), "" if len(ents) == 1 else "s"))
    for m in ents:
        print("  %s  %-10s %5ss  %s ago  %d shot(s)  keys=%s"
              % (m["key"], m["name"], m["secs"], human(int(time.time()) - m["recorded_at"]),
                 len(m["shots"]), m["env"].get("X2_KEYS", "(none)") or "(none)"))
    return 0


def cmd_show(argv):
    if not argv:
        raise SystemExit("usage: oracle.py show <key>")
    p = os.path.join(CACHE, argv[0], "manifest.json")
    if not os.path.exists(p):
        raise SystemExit("oracle: no entry %s -- `oracle.py list` shows what "
                         "there is" % argv[0])
    with open(p) as f:
        describe(json.load(f), True)
    return 0


def _selftest():
    """Prove the cache hits, and prove it misses. A stub runner stands in for
    Wine so this needs no game install -- what is under test is the KEYING, not
    the game."""
    import tempfile
    fails = []
    tmp = tempfile.mkdtemp(prefix="oracle-selftest-")
    global CACHE
    real_cache = CACHE
    CACHE = os.path.join(tmp, "cache")
    rundir = os.path.join(ROOT, "scratch", "run", "_selftest")
    os.makedirs(rundir, exist_ok=True)
    with open(os.path.join(rundir, "XMen2.exe"), "w") as f:
        f.write("stub v1")
    shots_dir = os.path.join(ROOT, "scratch", "screenshots")
    os.makedirs(shots_dir, exist_ok=True)
    runner = os.path.join(tmp, "stub.sh")
    with open(runner, "w") as f:
        f.write("#!/bin/sh\ntouch %s/$1.png\necho stub ran >&2\n" % shots_dir)
    os.chmod(runner, 0o755)

    calls = []

    def run_once(env_extra=None):
        env = dict(os.environ)
        env["X2_ORACLE_RUNNER"] = runner
        env.update(env_extra or {})
        old = dict(os.environ)
        os.environ.clear()
        os.environ.update(env)
        import io
        import contextlib
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                cmd_run(["_selftest", "1"])
        finally:
            os.environ.clear()
            os.environ.update(old)
        calls.append(buf.getvalue())
        return buf.getvalue()

    a = run_once()
    if "CACHE MISS" not in a:
        fails.append("the first run was not reported as a miss")
    b = run_once()
    if "CACHE HIT" not in b:
        fails.append("an identical second run did not HIT the cache")

    c = run_once({"X2_KEYS": "10:Return"})
    if "CACHE MISS" not in c:
        fails.append("changing X2_KEYS did not miss -- the cache would serve a "
                     "frame from a different driving script")

    with open(os.path.join(rundir, "XMen2.exe"), "w") as f:
        f.write("stub v2 -- rebuilt, different size")
    d = run_once()
    if "CACHE MISS" not in d:
        fails.append("rebuilding the run directory did not miss -- the cache "
                     "would serve a frame from the OLD binary")

    e = run_once({"X2_SAMPLES": "9"})
    if "CACHE MISS" not in e:
        fails.append("changing X2_SAMPLES did not miss")

    shutil.rmtree(rundir, ignore_errors=True)
    try:
        fingerprint_rundir(rundir)
        fails.append("fingerprinting a MISSING run directory was allowed")
    except SystemExit:
        pass

    CACHE = real_cache
    shutil.rmtree(tmp, ignore_errors=True)
    for f_ in fails:
        print("FAIL selftest: %s" % f_)
    print("oracle selftest: %d of 6 checks passed" % (6 - len(fails)))
    return 1 if fails else 0


def main(argv):
    if len(argv) > 1 and argv[1] == "--selftest":
        return _selftest()
    if len(argv) < 2:
        print(__doc__)
        print("usage: oracle.py run <rundir> [secs] [--force]\n"
              "       oracle.py list\n"
              "       oracle.py show <key>\n"
              "       oracle.py --selftest", file=sys.stderr)
        return 2
    return {"run": cmd_run, "list": cmd_list, "show": cmd_show}[argv[1]](argv[2:])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
