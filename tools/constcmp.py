#!/usr/bin/env python3
"""Compare the PORT's vertex-shader constants against the STOCK engine's.

These registers are the bone matrix palette. The captured shader reads

    mul r2, v2.zyxw, c0.wwww     ; c[0].w = 765.01 = 3 x 255
    mov a0.x, r2.x
    dp4 r3.x, v0, c[a0.x + 6]    ; +7, +8

so the palette starts at c[6] and strides 3 registers per bone, and c[0..5] is
projection and scaling data.

Why this file and not the vertices: in the shader path the vertex buffer holds
the UNSKINNED bind-pose mesh, which the port already reproduces exactly (73 of
75 meshes matched the control). The skinning happens from these constants, and
the engine computes them in code that is recompiled x86 in the port and native
x86 in the control. If they differ, the defect is in the recompiler and not in
the renderer -- and nothing else this project can measure separates those.

    constcmp.py port.txt stock.txt
    constcmp.py --selftest
"""

import argparse
import math
import re
import sys

LINE = re.compile(r"^c\[(\d+)\]\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$")

BONE_BASE = 6
BONE_STRIDE = 3


def load(path):
    try:
        with open(path) as f:
            text = f.read()
    except OSError as e:
        sys.exit(f"constcmp: cannot read {path}: {e}")
    regs, bad = {}, 0
    for line in text.splitlines():
        if not line.strip():
            continue
        m = LINE.match(line)
        if not m:
            bad += 1
            continue
        try:
            regs[int(m.group(1))] = [float(m.group(i)) for i in range(2, 6)]
        except ValueError:
            bad += 1
    if bad:
        sys.exit(f"constcmp: {path} has {bad} line(s) this cannot parse; "
                 f"refusing rather than comparing the part it understood.")
    if not regs:
        sys.exit(f"constcmp: {path} holds NO registers. That is a file that "
                 f"was opened and never written -- the dump was not armed, or "
                 f"the run never reached a shader draw. It is not a palette "
                 f"that happened to be empty.")
    return regs


def is_rigid(rows):
    """rows = three [4] register rows. Returns (det, max row length error)."""
    m0, m1, m2 = rows
    det = (m0[0] * (m1[1] * m2[2] - m1[2] * m2[1])
           - m0[1] * (m1[0] * m2[2] - m1[2] * m2[0])
           + m0[2] * (m1[0] * m2[1] - m1[1] * m2[0]))
    lens = [math.sqrt(r[0] ** 2 + r[1] ** 2 + r[2] ** 2) for r in rows]
    return det, max(abs(l - 1.0) for l in lens)


def bones(regs):
    out = {}
    i = BONE_BASE
    while i + BONE_STRIDE - 1 <= max(regs):
        rows = [regs.get(i + k) for k in range(BONE_STRIDE)]
        if all(r is not None for r in rows):
            out[(i - BONE_BASE) // BONE_STRIDE] = rows
        i += BONE_STRIDE
    return out


def live(rows):
    """A slot the engine actually wrote, rather than a zeroed one."""
    return any(abs(v) > 1e-9 for r in rows for v in r)


def compare(port, stock, tol):
    pb, sb = bones(port), bones(stock)
    common = sorted(set(pb) & set(sb))
    differing, both_live = [], 0
    for b in common:
        if not live(pb[b]) and not live(sb[b]):
            continue
        both_live += 1
        worst = max(abs(p - s)
                    for pr, sr in zip(pb[b], sb[b])
                    for p, s in zip(pr, sr))
        scale = max(1.0, max(abs(v) for r in sb[b] for v in r))
        if worst / scale > tol:
            differing.append((b, worst, worst / scale))
    return pb, sb, common, both_live, differing


def report(port_path, stock_path, tol):
    port, stock = load(port_path), load(stock_path)
    pb, sb, common, both_live, differing = compare(port, stock, tol)

    print(f"port  {port_path}: {len(port)} register(s)")
    print(f"stock {stock_path}: {len(stock)} register(s)")
    print(f"bone slots (from c[{BONE_BASE}], {BONE_STRIDE} register(s) each): "
          f"{len(common)} in common, {both_live} of them written by either side")
    print(f"of those, {len(differing)} differ by more than {tol:.2%} of the "
          f"control's own magnitude, {both_live - len(differing)} agree.")

    for name, regs in (("port", port), ("stock", stock)):
        bs = bones(regs)
        rigid = sum(1 for b, rows in bs.items()
                    if live(rows) and abs(abs(is_rigid(rows)[0]) - 1.0) < 0.05
                    and is_rigid(rows)[1] < 0.05)
        n = sum(1 for rows in bs.values() if live(rows))
        print(f"  {name}: {rigid} of {n} written bone(s) are rigid transforms")

    if both_live and not differing:
        print("\n  -- the palettes AGREE. The engine's animation maths "
              "produces the same matrices here as on a real CPU, so the "
              "recompiler is not what warps the characters and the fault is "
              "downstream, in how these matrices are applied.")
    if not both_live:
        print("\n  -- NOTHING was written on either side, so this comparison "
              "says nothing about the palette at all. Check that both dumps "
              "were armed on a frame with a character in it.")

    for b, worst, rel in sorted(differing, key=lambda r: -r[2])[:12]:
        print(f"\n  bone {b}  (c[{BONE_BASE + b * BONE_STRIDE}]..)"
              f"  worst |port-stock| {worst:.5f} ({rel:.2%})")
        for k in range(BONE_STRIDE):
            p = pb[b][k]
            s = sb[b][k]
            print(f"    port  {p[0]:10.4f} {p[1]:10.4f} {p[2]:10.4f} {p[3]:10.4f}")
            print(f"    stock {s[0]:10.4f} {s[1]:10.4f} {s[2]:10.4f} {s[3]:10.4f}")
    return 0 if not differing else 1


def selftest():
    import os
    import subprocess
    import tempfile

    d = tempfile.mkdtemp()

    def write(name, mutate=None):
        p = os.path.join(d, name)
        with open(p, "w") as f:
            for i in range(256):
                if i < BONE_BASE:
                    v = [1.0, 0.5, 0.0, 765.01]
                else:
                    b = (i - BONE_BASE) // BONE_STRIDE
                    k = (i - BONE_BASE) % BONE_STRIDE
                    t = 0.31 * (b + 1)
                    rot = [[math.cos(t), -math.sin(t), 0.0, 3.0 * b],
                           [math.sin(t), math.cos(t), 0.0, 1.0],
                           [0.0, 0.0, 1.0, -2.0]]
                    v = list(rot[k])
                    if mutate and b == 7:
                        v = [x * 1.9 for x in v]
                f.write("c[%d] %.6f %.6f %.6f %.6f\n" % (i, *v))
        return p

    ok = True
    a, b = write("a.txt"), write("b.txt")
    _, _, _, live_n, diff = compare(load(a), load(b), 0.001)
    if diff or not live_n:
        ok = False
        print(f"FAIL identical: {len(diff)} difference(s) over {live_n} bones")
    else:
        print(f"pass  identical palettes -> 0 differences over {live_n} bones")

    c = write("c.txt", mutate=True)
    _, _, _, _, diff = compare(load(c), load(b), 0.001)
    if len(diff) != 1 or diff[0][0] != 7:
        ok = False
        print(f"FAIL one corrupted bone: got {[x[0] for x in diff]}, "
              f"expected [7]")
    else:
        print(f"pass  one corrupted bone is found, and only it "
              f"(bone {diff[0][0]}, {diff[0][2]:.1%})")

    rows = [[math.cos(0.4), -math.sin(0.4), 0, 9], [math.sin(0.4), math.cos(0.4), 0, 0], [0, 0, 1, 0]]
    det, err = is_rigid(rows)
    if abs(abs(det) - 1.0) > 1e-6 or err > 1e-6:
        ok = False
        print(f"FAIL rigid check rejects a rotation: det {det}, row err {err}")
    else:
        print(f"pass  a rotation reads as rigid (det {det:.6f})")
    det2, err2 = is_rigid([[r * 1.9 for r in row] for row in rows])
    if abs(abs(det2) - 1.0) < 0.05 and err2 < 0.05:
        ok = False
        print("FAIL rigid check accepts a scaled matrix")
    else:
        print(f"pass  a scaled matrix reads as NOT rigid (det {det2:.4f})")

    empty = os.path.join(d, "empty.txt")
    open(empty, "w").close()
    for path, what in ((empty, "an empty file"), (os.path.join(d, "no.txt"), "a missing file")):
        r = subprocess.run([sys.executable, __file__, path, b],
                           capture_output=True, text=True)
        if r.returncode == 0:
            ok = False
            print(f"FAIL {what} was accepted")
        else:
            print(f"pass  {what} is refused")

    print("\nSELFTEST", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?")
    ap.add_argument("stock", nargs="?")
    ap.add_argument("--tol", type=float, default=0.001)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.port or not a.stock:
        ap.error("both files are required (or --selftest)")
    return report(a.port, a.stock, a.tol)


if __name__ == "__main__":
    sys.exit(main())
