#!/usr/bin/env python3
"""Compare the PORT's frame geometry against the STOCK engine's, mesh by mesh.

Both sides write the same OBJ: the port with X2_DRAW_OBJ (F9 arms it), the
stock game through tools/proxy_d3d8 (F9 too, polled at Present). Each `o`
group is one draw, named for its FVF, stride and primitive count, and holds the
draw's declared vertex range in OBJECT space -- before any matrix of ours.

That is what makes this decisive. The vertices in these files are what the
ENGINE's own skinning produced. If a mesh is already flat here in the port and
round in the control, the recompiled x86 computed it wrong and the renderer is
innocent; if both are round, the flattening happens downstream in the
transform. Nothing else this project can measure separates those two.

Draws are matched by SIGNATURE (fvf, stride, prims), not by position, because
the two runs will not be on the same frame and need not issue the same number
of draws.

    objcmp.py port.obj stock.obj
    objcmp.py port.obj stock.obj --render out.png --group draw28
    objcmp.py --selftest
"""

import argparse
import math
import re
import struct
import sys
import zlib

GROUP_RE = re.compile(r"^o\s+(\S+)")
SIG_RE = re.compile(r"_fvf([0-9a-fA-F]+)_stride(\d+)_prims(\d+)")


def load(path):
    """[(name, signature, [(x,y,z), ...]), ...] -- refusing rather than
    returning something smaller that looks like an answer."""
    try:
        with open(path) as f:
            text = f.read()
    except OSError as e:
        sys.exit(f"objcmp: cannot read {path}: {e}")

    groups, name, sig, verts = [], None, None, []
    unparsed = 0
    for line in text.splitlines():
        m = GROUP_RE.match(line)
        if m:
            if name is not None:
                groups.append((name, sig, verts))
            name = m.group(1)
            s = SIG_RE.search(name)
            sig = (s.group(1).lower(), int(s.group(2)), int(s.group(3))) if s else None
            verts = []
            continue
        if line.startswith("v "):
            p = line.split()
            if len(p) != 4:
                unparsed += 1
                continue
            try:
                verts.append((float(p[1]), float(p[2]), float(p[3])))
            except ValueError:
                unparsed += 1
            continue
        if line.strip() and not line.startswith("#"):
            unparsed += 1
    if name is not None:
        groups.append((name, sig, verts))

    if not groups:
        sys.exit(
            f"objcmp: {path} holds NO draw groups. That is a file which was "
            f"opened and never written, not a frame with no geometry in it -- "
            f"the dump was not armed, or the run never reached a draw."
        )
    if unparsed:
        sys.exit(
            f"objcmp: {path} has {unparsed} line(s) this cannot parse. "
            f"Refusing rather than comparing the part it happened to "
            f"understand."
        )
    total = sum(len(v) for _, _, v in groups)
    if total == 0:
        sys.exit(f"objcmp: {path} has {len(groups)} group(s) and 0 vertices.")
    return groups


def extents(verts):
    xs = [p[0] for p in verts]
    ys = [p[1] for p in verts]
    zs = [p[2] for p in verts]
    return (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))


def centroid(verts):
    n = len(verts)
    return tuple(sum(p[i] for p in verts) / n for i in range(3))


def flatness(verts):
    """smallest extent / largest. 0 is a plane, 1 is a cube."""
    e = sorted(extents(verts))
    return (e[0] / e[2]) if e[2] > 1e-9 else 0.0


def rms_after_centring(a, b):
    """Point-for-point RMS with centroids aligned. Meaningful only when the
    two meshes have the same vertex count and order, which the same draw of
    the same engine does."""
    if len(a) != len(b) or not a:
        return None
    ca, cb = centroid(a), centroid(b)
    acc = 0.0
    for p, q in zip(a, b):
        for i in range(3):
            d = (p[i] - ca[i]) - (q[i] - cb[i])
            acc += d * d
    return math.sqrt(acc / len(a))


def scale_of(verts):
    e = extents(verts)
    return max(e) or 1.0


def compare(port, stock, tol):
    by_sig = {}
    for name, sig, verts in stock:
        by_sig.setdefault(sig, []).append((name, verts))

    matched, only_port, differing = [], [], []
    for name, sig, verts in port:
        cand = by_sig.get(sig)
        if not cand:
            only_port.append((name, verts))
            continue
        sname, sverts = cand.pop(0)
        if not cand:
            by_sig.pop(sig)
        rms = rms_after_centring(verts, sverts)
        rel = (rms / scale_of(sverts)) if rms is not None else None
        rec = (name, sname, verts, sverts, rms, rel)
        matched.append(rec)
        if rel is None or rel > tol:
            differing.append(rec)
    only_stock = [(n, v) for lst in by_sig.values() for n, v in lst]
    return matched, differing, only_port, only_stock


def report(port_path, stock_path, tol):
    port, stock = load(port_path), load(stock_path)
    matched, differing, only_port, only_stock = compare(port, stock, tol)

    print(f"port  {port_path}: {len(port)} draw(s), "
          f"{sum(len(v) for _, _, v in port)} vertices")
    print(f"stock {stock_path}: {len(stock)} draw(s), "
          f"{sum(len(v) for _, _, v in stock)} vertices")
    print(f"matched by signature (fvf, stride, prims): {len(matched)}; "
          f"only in port: {len(only_port)}; only in stock: {len(only_stock)}")
    print(f"of the matched, {len(differing)} differ by more than "
          f"{tol:.4%} of the mesh's own size, {len(matched) - len(differing)} "
          f"agree.")
    if matched and not differing:
        print("  -- every matched mesh is the SAME shape on both sides. The "
              "engine's skinning is not what warps them; look downstream, at "
              "the transform or the pipeline.")
    if not matched:
        print("  -- NOTHING matched. The two dumps have no draw signature in "
              "common, so this comparison says nothing at all about the "
              "geometry; it says the two frames were not the same scene.")

    for name, sname, verts, sverts, rms, rel in sorted(
            differing, key=lambda r: -(r[5] or 9e9))[:20]:
        pe, se = extents(verts), extents(sverts)
        print(f"\n  {name}  vs  {sname}")
        if rms is None:
            print(f"    vertex COUNT differs: port {len(verts)}, "
                  f"stock {len(sverts)} -- not the same mesh")
            continue
        print(f"    rms {rms:.4f} ({rel:.3%} of size) over {len(verts)} vertices")
        print(f"    port  extent {pe[0]:9.3f} x {pe[1]:9.3f} x {pe[2]:9.3f}"
              f"   flatness {flatness(verts):.4f}")
        print(f"    stock extent {se[0]:9.3f} x {se[1]:9.3f} x {se[2]:9.3f}"
              f"   flatness {flatness(sverts):.4f}")
        if flatness(sverts) > 0.05 and flatness(verts) < 0.05:
            print("    ^^ ROUND in the control and FLAT in the port: this mesh "
                  "is collapsed before any matrix of ours touches it.")
    return 0 if not differing else 1


# ------------------------------------------------------------------ render

def png(path, w, h, rgb):
    raw = b"".join(b"\0" + bytes(rgb[y * w * 3:(y + 1) * w * 3])
                  for y in range(h))
    comp = zlib.compress(raw, 9)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", comp))
        f.write(chunk(b"IEND", b""))


def render(path, pairs, cell=260):
    """Three orthographic views (XY, XZ, ZY) per mesh, port above stock."""
    rows = len(pairs) * 2
    w, h = cell * 3, cell * rows
    buf = bytearray(w * h * 3)
    for i in range(0, len(buf), 3):
        buf[i:i + 3] = b"\x12\x12\x16"

    for r, (label, verts, colour) in enumerate(pairs):
        if not verts:
            continue
        c = centroid(verts)
        s = max(extents(verts)) or 1.0
        for vi, (ax, ay) in enumerate(((0, 1), (0, 2), (2, 1))):
            ox, oy = vi * cell, r * cell
            for p in verts:
                x = (p[ax] - c[ax]) / s * (cell * 0.86) + cell / 2
                y = -(p[ay] - c[ay]) / s * (cell * 0.86) + cell / 2
                px, py = int(ox + x), int(oy + y)
                if ox <= px < ox + cell and oy <= py < oy + cell:
                    o = (py * w + px) * 3
                    buf[o:o + 3] = colour
    png(path, w, h, buf)
    print(f"objcmp: wrote {path} -- rows are "
          + ", ".join(l for l, _, _ in pairs)
          + "; columns are the XY, XZ and ZY views.")


# ---------------------------------------------------------------- selftest

def selftest():
    import os
    import subprocess
    import tempfile

    d = tempfile.mkdtemp()

    def write(name, groups):
        p = os.path.join(d, name)
        with open(p, "w") as f:
            for gname, verts in groups:
                f.write(f"o {gname}\n")
                for v in verts:
                    f.write("v %.4f %.4f %.4f\n" % v)
        return p

    cube = [(x, y, z) for x in (0.0, 1.0) for y in (0.0, 1.0) for z in (0.0, 1.0)]
    flat = [(x, y, 0.0) for (x, y, _) in cube]
    sig = "_fvf00112_stride32_prims12"

    ok = True

    # 1. identical input must report NO differences.
    a = write("a.obj", [("draw1" + sig, cube)])
    b = write("b.obj", [("draw1" + sig, cube)])
    m, diff, op, os_ = compare(load(a), load(b), 0.001)
    if len(m) != 1 or diff or op or os_:
        ok = False
        print(f"FAIL identical: matched={len(m)} differing={len(diff)}")
    else:
        print("pass  identical meshes -> 0 differences")

    # 2. a mesh flattened on Z -- the defect being hunted -- MUST be reported.
    c = write("c.obj", [("draw1" + sig, flat)])
    m, diff, _, _ = compare(load(c), load(b), 0.001)
    if len(diff) != 1:
        ok = False
        print(f"FAIL flattened: {len(diff)} difference(s), expected 1")
    else:
        rel = diff[0][5]
        print(f"pass  a mesh flattened on Z -> reported, {rel:.1%} of size")
        if flatness(flat) >= 0.05 or flatness(cube) <= 0.05:
            ok = False
            print("FAIL flatness does not separate the plane from the cube")
        else:
            print(f"pass  flatness separates them: flat {flatness(flat):.3f} "
                  f"vs round {flatness(cube):.3f}")

    # 3. a signature present on one side only must NOT be silently matched.
    e = write("e.obj", [("draw1_fvf00042_stride16_prims2", cube)])
    m, diff, op, os_ = compare(load(e), load(b), 0.001)
    if m or len(op) != 1 or len(os_) != 1:
        ok = False
        print(f"FAIL unmatched: matched={len(m)} port-only={len(op)} "
              f"stock-only={len(os_)}")
    else:
        print("pass  a draw with no counterpart is reported, not matched")

    # 4. an empty or absent corpus must REFUSE, not return "no differences".
    empty = os.path.join(d, "empty.obj")
    open(empty, "w").close()
    for path, what in ((empty, "an empty file"),
                       (os.path.join(d, "nope.obj"), "a missing file")):
        r = subprocess.run([sys.executable, __file__, path, b],
                           capture_output=True, text=True)
        if r.returncode == 0:
            ok = False
            print(f"FAIL {what} was accepted (exit 0)")
        else:
            print(f"pass  {what} is refused: {r.stdout.strip() or r.stderr.strip()}")

    print("\nSELFTEST", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", help="the port's X2_DRAW_OBJ file")
    ap.add_argument("stock", nargs="?", help="the proxy's d3d8_frame.obj")
    ap.add_argument("--tol", type=float, default=0.002,
                    help="relative rms below which two meshes are the same")
    ap.add_argument("--render", metavar="PNG")
    ap.add_argument("--group", help="substring of the draw group to render")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    if not a.port or not a.stock:
        ap.error("both OBJ files are required (or --selftest)")

    rc = report(a.port, a.stock, a.tol)

    if a.render:
        port, stock = load(a.port), load(a.stock)
        pick = lambda gs: [(n, v) for n, _, v in gs
                           if not a.group or a.group in n]
        pp, ss = pick(port), pick(stock)
        if not pp and not ss:
            sys.exit(f"objcmp: no group matches {a.group!r} in either file.")
        rows = []
        for n, v in pp[:4]:
            rows.append((f"PORT {n}", v, b"\x66\xdd\x88"))
        for n, v in ss[:4]:
            rows.append((f"STOCK {n}", v, b"\xdd\x99\x55"))
        render(a.render, rows)
    return rc


if __name__ == "__main__":
    sys.exit(main())
