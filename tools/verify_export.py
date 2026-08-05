#!/usr/bin/env python3
"""Check that each recompiler JSON describes the binary we actually ship.

Exists because it did not. The Ghidra project held a libIGSg.dll from some
earlier session whose section layout did not match the shipped file, and every
one of its 6118 functions was recompiled from a description of a different
binary (issue #12). Nothing revealed it until an entry point happened to land
in the region where the two images disagreed.

The section table is enough to catch it and costs nothing: a JSON's memory
blocks must line up with the PE's own sections. This does NOT need Ghidra, so
it can run on every build.

    tools/verify_export.py [module ...]

Exits non-zero if any module disagrees, and says which. A module with no JSON
is reported as UNCHECKED rather than skipped silently -- "nothing to check" and
"checked and fine" must not look the same.
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def env_game_dir():
    p = os.path.join(ROOT, ".env")
    if os.path.exists(p):
        for line in open(p):
            if line.startswith("GAME_PC_DIR"):
                return line.split("=", 1)[1].strip().strip('"').strip("'")
    return os.environ.get("GAME_PC_DIR", "")


def pe_sections(path):
    f = open(path, "rb").read()
    pe = struct.unpack_from("<I", f, 0x3C)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", f, opt + 28)[0]
    nsec = struct.unpack_from("<H", f, pe + 6)[0]
    so = opt + struct.unpack_from("<H", f, pe + 20)[0]
    out = {}
    for i in range(nsec):
        s = so + i * 40
        name = f[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
        va = struct.unpack_from("<I", f, s + 12)[0]
        out[name] = base + va
    return base, out


def check(mod, game_dir):
    js = os.path.join(ROOT, "scratch", "recomp", mod + ".json")
    if not os.path.exists(js):
        print("  %-14s UNCHECKED -- no JSON exported yet" % mod)
        return None
    binp = None
    for ext in (".dll", ".exe"):
        cand = os.path.join(game_dir, mod + ext)
        if os.path.exists(cand):
            binp = cand
            break
    if not binp:
        print("  %-14s UNCHECKED -- no binary in GAME_PC_DIR" % mod)
        return None

    d = json.load(open(js))
    base, sects = pe_sections(binp)
    blocks = {b["name"]: b["start"] for b in d.get("blocks", [])}
    if not blocks:
        print("  %-14s FAIL -- the export records no memory blocks" % mod)
        return False
    if d.get("image_base") != base:
        print("  %-14s FAIL -- image base 0x%08x in the JSON, 0x%08x in the file"
              % (mod, d.get("image_base", 0), base))
        return False
    bad = []
    for name, va in sects.items():
        if name in blocks and blocks[name] != va:
            bad.append("%s at 0x%08x in the JSON, 0x%08x in the file"
                       % (name, blocks[name], va))
    if bad:
        print("  %-14s FAIL -- %s" % (mod, "; ".join(bad)))
        return False
    # Truncated bodies. A function whose last instruction is not a terminator
    # was cut off -- almost always clamped at the next detected start, which
    # means that "next" function sits inside this one. The emitted C then just
    # falls off the end and returns with whatever ESP the partial body left,
    # and the failure lands at some later RET popping the wrong word. Measured
    # rather than assumed: it is how the native run's stall was finally
    # located, at FUN_00554ba0.
    trunc = []
    for f in d.get("functions", []):
        ins = f.get("ins")
        if not ins:
            continue
        m = ins[-1]["m"].upper()
        if not (m.startswith("RET") or m.startswith("JMP")
                or m in ("INT3", "UD2", "HLT")):
            trunc.append(f["ep"])
    n = len(d.get("functions", []))
    print("  %-14s ok (%d sections agree, %d functions, %d truncated %s)"
          % (mod, len(sects), n, len(trunc),
             "(%.2f%%)" % (100.0 * len(trunc) / n) if n else ""))
    if trunc and os.environ.get("VERIFY_TRUNC_OUT"):
        fh = open(os.environ["VERIFY_TRUNC_OUT"], "a")
        for t in trunc:
            fh.write("%s 0x%08x\n" % (mod, t))
        fh.close()
    return True


def main(argv):
    game = env_game_dir()
    if not game or not os.path.isdir(game):
        sys.exit("verify_export: GAME_PC_DIR is not set or does not exist -- "
                 "checked NOTHING")
    mods = argv or sorted(
        f[:-5] for f in os.listdir(os.path.join(ROOT, "scratch", "recomp"))
        if f.endswith(".json"))
    print("verify_export: comparing each export's section layout against %s"
          % game)
    results = [check(m, game) for m in mods]
    failed = sum(1 for r in results if r is False)
    unchecked = sum(1 for r in results if r is None)
    print("verify_export: %d ok, %d FAILED, %d unchecked, of %d"
          % (sum(1 for r in results if r), failed, unchecked, len(results)))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main(sys.argv[1:])
