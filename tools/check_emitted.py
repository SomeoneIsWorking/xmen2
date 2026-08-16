#!/usr/bin/env python3
"""Refuse a build whose recompiled C is older than the translator that makes it.

    tools/check_emitted.py            report, exit non-zero if anything is stale
    tools/check_emitted.py --list     one line per module, always exit 0
    tools/check_emitted.py --selftest prove it detects a stale stamp

WHY THIS EXISTS. src/recomp/*.c is generated and gitignored, so nothing that is
tracked ever shows that a module has fallen behind tools/recomp.py. A stale
module compiles, links, runs, and is WRONG in exactly the way the fix it missed
was about to correct -- there is no error, no warning, and no diff to notice.

It cost days. libIGSg was last emitted before two translator fixes landed: a
tail-call ABI change touching 209 sites in that module alone, and a reversed
x87 FSUBR/FDIVR at three more. The port drew warped characters the whole time
(issue #80), and every instrument built to find it was aimed at source code
that was already correct -- the defect was in a build artifact nobody had
regenerated. It came right the moment the module was re-emitted for an
unrelated reason, which is the worst way to learn any of this.

The stamp is a CONTENT hash of recomp.py, not a git hash: an uncommitted edit
changes what it emits just as much as a commit does. A file with no stamp at
all was emitted before stamping existed and is therefore stale by definition --
it is reported as stale, never skipped.
"""
import glob
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RECOMP = os.path.join(ROOT, "tools", "recomp.py")
GEN = os.path.join(ROOT, "src", "recomp")
STAMP = re.compile(r"/\* recomp-fingerprint: ([0-9a-f]+) \*/")


def fingerprint(path=RECOMP):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()[:16]


def stamp_of(path):
    """The stamp in an emitted file, or None if it carries none."""
    with open(path, errors="replace") as f:
        for _ in range(8):                      # it is in the first few lines
            line = f.readline()
            if not line:
                break
            m = STAMP.search(line)
            if m:
                return m.group(1)
    return None


def modules():
    """-> {module: [(path, stamp)]}, over the emitted chunks only."""
    out = {}
    for p in sorted(glob.glob(os.path.join(GEN, "*.c"))):
        b = os.path.basename(p)
        m = re.match(r"(.+?)_(\d{3}|native)\.c$", b)
        if not m:
            continue
        out.setdefault(m.group(1), []).append((p, stamp_of(p)))
    return out


def main(argv):
    want = fingerprint()
    mods = modules()
    if not mods:
        sys.stderr.write(
            "check_emitted: %s holds NO emitted modules. Nothing was checked --\n"
            "  that is not a pass. Generate them (see CLAUDE.md) before building.\n"
            % GEN)
        return 2

    stale, ok = [], []
    for mod in sorted(mods):
        got = set(s for _, s in mods[mod])
        if got == {want}:
            ok.append(mod)
        else:
            stale.append((mod, got))

    if "--list" in argv or stale:
        for mod in sorted(mods):
            got = set(s for _, s in mods[mod])
            mark = "current" if got == {want} else "STALE"
            shown = ", ".join(sorted(g or "(unstamped)" for g in got))
            print("  %-14s %-8s %d chunk(s)  %s" % (mod, mark, len(mods[mod]),
                                                    shown))
    if "--list" in argv:
        print("translator fingerprint now: %s" % want)
        return 0

    if not stale:
        print("check_emitted: %d module(s), all emitted by the current "
              "translator (%s)" % (len(ok), want))
        return 0

    sys.stderr.write(
        "\ncheck_emitted: %d of %d module(s) were emitted by a DIFFERENT "
        "tools/recomp.py\n  than the one in the tree (now %s).\n\n"
        "  These build and run and are wrong in whatever way the translator "
        "fixes they\n  missed were about to correct. Re-emit them:\n\n"
        % (len(stale), len(mods), want))
    for mod, _ in stale:
        sys.stderr.write(
            "    python3 tools/recomp.py emit scratch/recomp/%s.json "
            "src/recomp/%s.c --split 1500 \\\n"
            "        --isolate scratch/recomp/%s.isolate\n" % (mod, mod, mod))
    sys.stderr.write("\n  and re-run tools/recomp.py native for each, then "
                     "rebuild.\n")
    return 1


def selftest():
    """The check must fire on a stale stamp AND pass on a current one. A
    checker only ever run against the good case is not a checker."""
    import tempfile
    ok = True
    d = tempfile.mkdtemp(dir=os.path.join(ROOT, "scratch"))
    good = os.path.join(d, "m_000.c")
    with open(good, "w") as f:
        f.write("/* generated */\n/* recomp-fingerprint: %s */\nint x;\n"
                % fingerprint())
    if stamp_of(good) != fingerprint():
        ok = False
        print("  FAIL  a current stamp was not read back")
    else:
        print("  pass  a current stamp is read back")

    bad = os.path.join(d, "n_000.c")
    with open(bad, "w") as f:
        f.write("/* generated */\n/* recomp-fingerprint: deadbeefdeadbeef */\n")
    if stamp_of(bad) == fingerprint():
        ok = False
        print("  FAIL  a stale stamp read as current")
    else:
        print("  pass  a stale stamp is seen as stale (%s)" % stamp_of(bad))

    none = os.path.join(d, "o_000.c")
    with open(none, "w") as f:
        f.write("/* generated by something older */\nint y;\n")
    if stamp_of(none) is not None:
        ok = False
        print("  FAIL  an UNSTAMPED file produced a stamp")
    else:
        print("  pass  an unstamped file reads as None, which counts as stale")

    # And the fingerprint must MOVE when the translator changes, or none of
    # the above means anything.
    tweaked = os.path.join(d, "recomp_tweaked.py")
    with open(RECOMP, "rb") as f:
        body = f.read()
    with open(tweaked, "wb") as f:
        f.write(body + b"\n# a change\n")
    if fingerprint(tweaked) == fingerprint():
        ok = False
        print("  FAIL  editing the translator did not change the fingerprint")
    else:
        print("  pass  editing the translator changes the fingerprint")

    for f in os.listdir(d):
        os.unlink(os.path.join(d, f))
    os.rmdir(d)
    print("\nSELFTEST", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv else main(sys.argv[1:]))
