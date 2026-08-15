#!/usr/bin/env python3
"""
Find the D3DLIGHT8 records in a LIVE process, and say what colour they are.

## Why this exists

The port's own instruments now agree that its renderer is faithful: the lights
reaching a draw are exactly the ones the engine handed over (85,156 comparisons,
0 disagreements -- C199). Those lights are four black point lights and one dim
directional, and the characters lit by them come out black.

What no instrument here can answer is whether the CONTROL's engine computes the
same values for the same scene. If it does, the port's lights are faithful and
the darkness has another cause; if it does not, the divergence is upstream in
the recompiled engine, and this stops being a rendering question.

Wine runs XMen2.exe in an ordinary process on this host, and same-user
process_vm_readv is permitted, so the control's own light records can simply be
READ -- no debugger, no proxy DLL, no perturbation. The port can be read the
same way with the same code, which is the point: one instrument, two subjects,
directly comparable numbers.

## How a light is recognised

By SHAPE, not by address: the engine heap-allocates its light records, so the
addresses differ between the two runs and between runs of the same build. A
D3DLIGHT8 is 26 dwords:

    +0x00 Type (1 point, 2 spot, 3 directional)
    +0x04 Diffuse  r,g,b,a      (floats, 0..1 in practice)
    +0x14 Specular r,g,b,a
    +0x24 Ambient  r,g,b,a
    +0x34 Position x,y,z
    +0x40 Direction x,y,z
    +0x4c Range
    +0x50 Falloff
    +0x54 Attenuation0,1,2
    +0x60 Theta
    +0x64 Phi

The colour channels being in 0..1, the type being one of three values, and the
range and attenuations being non-negative and finite make a pattern that random
memory very rarely satisfies -- and `--selftest` measures how rarely rather than
asserting it.

## What it refuses to do

  * It will not guess a process. `--pid` names one; `--discover <substr>` must
    match exactly one, or it lists what it found and exits 2.
  * It reports the bytes scanned, the regions skipped, and the candidates
    rejected at each stage, ALWAYS -- so "this run has no coloured lights" and
    "nothing was scanned" can never read alike.
  * `--selftest` runs the matcher against BOTH classes: a buffer that MUST
    match and a buffer of real non-light memory that must not, and it fails if
    either comes out wrong.

## Use

    tools/light_probe.py --pid 12345
    tools/light_probe.py --discover x2native --repeat 5 --interval 2
    tools/light_probe.py --selftest
"""
import argparse
import ctypes
import ctypes.util
import os
import struct
import sys

LIGHT_BYTES = 26 * 4


class Reader(object):
    """process_vm_readv, wrapped so an unreadable address is an answer."""

    def __init__(self, pid):
        self.pid = pid
        libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
        self._readv = libc.process_vm_readv
        self._readv.restype = ctypes.c_ssize_t

        class IOVec(ctypes.Structure):
            _fields_ = [("base", ctypes.c_void_p), ("len", ctypes.c_size_t)]

        self.IOVec = IOVec
        self.fails = 0
        self.bytes_read = 0

    def read(self, addr, n):
        buf = (ctypes.c_char * n)()
        local = self.IOVec(ctypes.cast(buf, ctypes.c_void_p), n)
        remote = self.IOVec(ctypes.c_void_p(addr), n)
        got = self._readv(self.pid, ctypes.byref(local), 1,
                          ctypes.byref(remote), 1, 0)
        if got != n:
            self.fails += 1
            return None
        self.bytes_read += n
        return bytes(buf)


def finite(x):
    return x == x and abs(x) != float("inf")


def match_light(b, off):
    """A D3DLIGHT8 at b[off:]? Returns a dict, or a rejection REASON string.

    Returning the reason rather than None is what lets --selftest and the
    report say WHERE candidates die, instead of only how many survived.
    """
    if off + LIGHT_BYTES > len(b):
        return "short"
    w = struct.unpack_from("<I", b, off)[0]
    if w not in (1, 2, 3):
        return "type"
    f = struct.unpack_from("<25f", b, off + 4)
    for v in f:
        if not finite(v):
            return "nan"
    diffuse, specular, ambient = f[0:4], f[4:8], f[8:12]
    for c in list(diffuse) + list(specular) + list(ambient):
        if c < 0.0 or c > 1.0:
            return "colour range"
    pos, direction = f[12:15], f[15:18]
    rng, falloff = f[18], f[19]
    atten = f[20:23]
    if rng <= 0.0 or rng > 1e12:
        return "range"
    if falloff < 0.0 or falloff > 1e6:
        return "falloff"
    for a in atten:
        if a < 0.0 or a > 1e6:
            return "attenuation"
    for p in pos:
        if abs(p) > 1e7:
            return "position"
    if w == 3:
        dl = sum(d * d for d in direction) ** 0.5
        if dl < 1e-6 or dl > 1e3:
            return "direction"
    # An all-zero record satisfies everything above except range; keep the
    # guard anyway, because a cleared record is not a light the engine set.
    if max(diffuse[:3]) == 0.0 and max(ambient[:3]) == 0.0 \
            and max(specular[:3]) == 0.0 and w != 1:
        return "all channels zero"
    return {
        "type": w, "diffuse": diffuse[:3], "ambient": ambient[:3],
        "specular": specular[:3], "position": pos, "direction": direction,
        "range": rng, "atten": atten,
    }


def regions(pid, want_all=False):
    """Readable, private, anonymous or heap regions of the process."""
    out = []
    try:
        with open("/proc/%d/maps" % pid) as fh:
            lines = fh.readlines()
    except OSError as e:
        sys.exit("light_probe: cannot read /proc/%d/maps (%s)" % (pid, e))
    for ln in lines:
        parts = ln.split()
        if len(parts) < 5:
            continue
        rng, perms = parts[0], parts[1]
        path = parts[5] if len(parts) > 5 else ""
        if "r" not in perms:
            continue
        # Only the kernel's own pseudo-mappings are skipped by default.
        #
        # The first version of this filtered out /usr and /dev as well, and
        # scanned 0.8 MiB of a multi-gigabyte process -- then reported five
        # records as though that were the answer. A scan that covers a
        # thousandth of a process and does not say so is worse than no scan,
        # so the coverage is now reported next to every result and the filter
        # excludes almost nothing.
        if path in ("[vvar]", "[vsyscall]", "[vdso]"):
            continue
        if not want_all and path.startswith("/dev/"):
            continue
        a, b = rng.split("-")
        out.append((int(a, 16), int(b, 16), path))
    return out


def scan(pid, want_all=False, chunk=1 << 20, cap=(3 << 30)):
    rd = Reader(pid)
    found = {}
    rejected = {}
    scanned = skipped = 0
    skipped_regions = []
    total = 0
    regs = regions(pid, want_all)
    total = sum(hi - lo for lo, hi, _ in regs)
    for lo, hi, path in regs:
        size = hi - lo
        if size > cap:
            skipped += size
            skipped_regions.append((lo, hi, path))
            continue
        addr = lo
        while addr < hi:
            n = min(chunk, hi - addr)
            b = rd.read(addr, n)
            if b is None:
                skipped += n
                addr += n
                continue
            scanned += n
            for off in range(0, max(0, len(b) - LIGHT_BYTES), 4):
                m = match_light(b, off)
                if isinstance(m, str):
                    rejected[m] = rejected.get(m, 0) + 1
                    continue
                key = (m["type"],
                       tuple(round(c, 4) for c in m["diffuse"]),
                       tuple(round(c, 4) for c in m["ambient"]),
                       round(m["range"], 2),
                       tuple(round(a, 8) for a in m["atten"]))
                e = found.setdefault(key, {"n": 0, "addr": addr + off,
                                           "light": m})
                e["n"] += 1
            addr += n
    return {"found": found, "rejected": rejected, "scanned": scanned,
            "skipped": skipped, "read_fails": rd.fails, "total": total,
            "skipped_regions": skipped_regions, "regions": len(regs)}


def report(res, label):
    print("== %s ==" % label)
    cov = (100.0 * res["scanned"] / res["total"]) if res["total"] else 0.0
    print("   scanned %.1f MiB of %.1f MiB mapped (%.1f%% COVERAGE, %d "
          "regions), skipped %.1f MiB, %d read failure(s)"
          % (res["scanned"] / 1048576.0, res["total"] / 1048576.0, cov,
             res["regions"], res["skipped"] / 1048576.0, res["read_fails"]))
    if cov < 90.0:
        print("   COVERAGE IS PARTIAL -- anything not found may simply be in "
              "the %.1f MiB not looked at." % (res["skipped"] / 1048576.0))
    for lo, hi, path in res["skipped_regions"][:5]:
        print("     skipped 0x%x..0x%x (%.0f MiB) %s"
              % (lo, hi, (hi - lo) / 1048576.0, path or "anon"))
    rej = res["rejected"]
    print("   candidates rejected: " +
          (", ".join("%s %d" % (k, v) for k, v in
                     sorted(rej.items(), key=lambda kv: -kv[1])) or "none"))
    found = res["found"]
    if not found:
        print("   NO D3DLIGHT8-shaped record was found. With %.1f MiB actually "
              "scanned this is a measurement; with 0 MiB it would mean the "
              "scan never ran." % (res["scanned"] / 1048576.0))
        return
    black = sum(1 for k in found if max(k[1]) == 0.0)
    print("   %d distinct light record(s); %d of them have a BLACK diffuse"
          % (len(found), black))
    for key in sorted(found, key=lambda k: -max(k[1])):
        e = found[key]
        m = e["light"]
        print("     type %d  diffuse %.4f %.4f %.4f  ambient %.4f %.4f %.4f  "
              "range %.3f  atten %.4f %.6f %.8f  x%d  @0x%x"
              % (m["type"], m["diffuse"][0], m["diffuse"][1], m["diffuse"][2],
                 m["ambient"][0], m["ambient"][1], m["ambient"][2],
                 m["range"], m["atten"][0], m["atten"][1], m["atten"][2],
                 e["n"], e["addr"]))




# ---- the light ARRAY, which is what a single record could never be ---------
#
# The port reports (X2_LIGHT_ADDR=1) that the engine hands D3D8 its lights from
# a contiguous array: index 6 at guest 0x04674cd4 and index 7 at 0x04674d60,
# which differ by exactly 0x8c = 140 bytes -- the record size this project's RE
# already documented, with the D3DLIGHT8 inside it.
#
# That stride is the signature worth searching for. A single D3DLIGHT8-shaped
# record is far too weak: validated against RANDOM bytes the matcher had zero
# false positives, and then found six million of them in a live process,
# because real program memory is full of 0.0 and 1.0 floats. A RUN of records
# at a fixed 140-byte stride, each independently valid, is a different
# proposition -- and the port's known addresses make it checkable rather than
# merely plausible.
RECORD_STRIDE = 140


def find_arrays(pid, want_all=False, min_run=4, chunk=(1 << 20)):
    """Runs of >= min_run light records at RECORD_STRIDE. Returns runs+coverage."""
    rd = Reader(pid)
    runs = []
    scanned = skipped = 0
    regs = regions(pid, want_all)
    total = sum(hi - lo for lo, hi, _ in regs)
    for lo, hi, path in regs:
        if hi - lo > (3 << 30):
            skipped += hi - lo
            continue
        addr = lo
        while addr < hi:
            n = min(chunk + RECORD_STRIDE * min_run, hi - addr)
            b = rd.read(addr, n)
            if b is None:
                skipped += n
                addr += chunk
                continue
            scanned += min(n, chunk)
            limit = min(len(b) - LIGHT_BYTES, chunk)
            off = 0
            while off < limit:
                m = match_light(b, off)
                if isinstance(m, str):
                    off += 4
                    continue
                # A candidate. How many records follow it at the stride?
                run = [m]
                k = off + RECORD_STRIDE
                while k + LIGHT_BYTES <= len(b):
                    mk = match_light(b, k)
                    if isinstance(mk, str):
                        break
                    run.append(mk)
                    k += RECORD_STRIDE
                if len(run) >= min_run:
                    runs.append({"addr": addr + off, "n": len(run),
                                 "lights": run})
                    off = k
                else:
                    off += 4
            addr += chunk
    return {"runs": runs, "scanned": scanned, "skipped": skipped,
            "total": total, "read_fails": rd.fails}


def report_arrays(res, label, expect=None):
    print("== %s ==" % label)
    cov = (100.0 * res["scanned"] / res["total"]) if res["total"] else 0.0
    print("   scanned %.1f MiB of %.1f MiB (%.1f%% coverage), %d read failure(s)"
          % (res["scanned"] / 1048576.0, res["total"] / 1048576.0, cov,
             res["read_fails"]))
    runs = res["runs"]
    if not runs:
        print("   NO run of %d+ light records at a %d-byte stride was found. "
              "With %.1f MiB scanned that is a measurement; the engine either "
              "holds its lights elsewhere in this build, or holds fewer than "
              "%d." % (4, RECORD_STRIDE, res["scanned"] / 1048576.0, 4))
        return
    print("   %d run(s) of light records found" % len(runs))
    for r in sorted(runs, key=lambda r: -r["n"])[:6]:
        hit = ""
        if expect is not None:
            for i, l in enumerate(r["lights"]):
                if r["addr"] + i * RECORD_STRIDE == expect:
                    hit = "   <- CONTAINS the address the port reported"
        print("     %d record(s) at 0x%x%s" % (r["n"], r["addr"], hit))
        for i, m in enumerate(r["lights"]):
            print("       [%d] type %d  diffuse %.4f %.4f %.4f  ambient "
                  "%.4f %.4f %.4f  range %.1f  atten %.4f %.6f %.8f"
                  % (i, m["type"], m["diffuse"][0], m["diffuse"][1],
                     m["diffuse"][2], m["ambient"][0], m["ambient"][1],
                     m["ambient"][2], m["range"], m["atten"][0], m["atten"][1],
                     m["atten"][2]))


def selftest():
    """The matcher against BOTH classes, and it must be wrong about neither."""
    ok = True

    # 1. A record that MUST match: a white point light, laid out by hand.
    good = struct.pack("<I", 1) + struct.pack(
        "<25f",
        0.84, 0.84, 1.0, 1.0,            # diffuse
        0.0, 0.0, 0.0, 0.0,              # specular
        0.1, 0.1, 0.1, 1.0,              # ambient
        100.0, 200.0, 300.0,             # position
        0.0, 0.0, 0.0,                   # direction
        5000.0,                          # range
        0.0,                             # falloff
        1.0, 0.0, 0.0000378,             # attenuation
        0.0, 0.0)                        # theta, phi
    m = match_light(good, 0)
    if isinstance(m, str):
        print("SELFTEST FAIL: a hand-built white point light was rejected "
              "(%s) -- the matcher cannot see the thing it exists to find" % m)
        ok = False
    elif abs(m["diffuse"][0] - 0.84) > 1e-6:
        print("SELFTEST FAIL: matched but read diffuse %r" % (m["diffuse"],))
        ok = False
    else:
        print("SELFTEST ok: the positive control matches, diffuse %.2f %.2f "
              "%.2f" % m["diffuse"])

    # 2. A record that must NOT match: same bytes with an impossible type.
    bad = struct.pack("<I", 7) + good[4:]
    if not isinstance(match_light(bad, 0), str):
        print("SELFTEST FAIL: a record with type 7 was accepted as a light")
        ok = False
    else:
        print("SELFTEST ok: type 7 is rejected")

    # 3. Colour out of range must not match.
    bad2 = struct.pack("<I", 1) + struct.pack("<f", 4.2) + good[8:]
    if not isinstance(match_light(bad2, 0), str):
        print("SELFTEST FAIL: a diffuse of 4.2 was accepted")
        ok = False
    else:
        print("SELFTEST ok: a diffuse outside 0..1 is rejected")

    # 4. THE FALSE-POSITIVE RATE, measured rather than asserted: how often does
    #    random memory satisfy the pattern? This is the number that decides
    #    whether a hit in a live process means anything.
    noise = os.urandom(1 << 20)
    hits = sum(1 for off in range(0, len(noise) - LIGHT_BYTES, 4)
               if not isinstance(match_light(noise, off), str))
    per_mib = hits
    print("SELFTEST: %d false positive(s) in 1 MiB of random bytes" % per_mib)
    if per_mib > 50:
        print("SELFTEST FAIL: the pattern is too weak to be evidence")
        ok = False

    # 5. A buffer of zeros must produce nothing -- a cleared record is not a
    #    light, and this is the shape most of a process's memory has.
    zeros = bytes(1 << 16)
    zh = sum(1 for off in range(0, len(zeros) - LIGHT_BYTES, 4)
             if not isinstance(match_light(zeros, off), str))
    if zh:
        print("SELFTEST FAIL: %d zero-filled records matched" % zh)
        ok = False
    else:
        print("SELFTEST ok: zero-filled memory matches nothing")

    print("SELFTEST: %s" % ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def discover(substr):
    hits = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open("/proc/%s/cmdline" % pid, "rb") as fh:
                cmd = fh.read().replace(b"\0", b" ").decode("utf8", "replace")
        except OSError:
            continue
        if substr in cmd and "light_probe" not in cmd:
            hits.append((int(pid), cmd.strip()))
    if len(hits) != 1:
        print("light_probe: --discover %r matched %d process(es); it will not "
              "guess." % (substr, len(hits)), file=sys.stderr)
        for pid, cmd in hits:
            print("    %d  %s" % (pid, cmd[:110]), file=sys.stderr)
        sys.exit(2)
    return hits[0][0]


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pid", type=int)
    ap.add_argument("--discover", metavar="SUBSTR")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--all-regions", action="store_true",
                    help="scan /dev mappings too (slow)")
    ap.add_argument("--cap-gib", type=float, default=3.0,
                    help="skip single regions larger than this; skipped "
                         "regions are always listed")
    ap.add_argument("--arrays", action="store_true",
                    help="find RUNS of light records at the 140-byte stride "
                         "(the signature that a single record cannot give)")
    ap.add_argument("--min-run", type=int, default=4)
    ap.add_argument("--expect", type=lambda v: int(v, 0), default=None,
                    help="an address the port reported, as a positive control")
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--interval", type=float, default=2.0)
    ap.add_argument("--label", default="")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    pid = a.pid or (discover(a.discover) if a.discover else None)
    if not pid:
        sys.exit("light_probe: give --pid or --discover; it will not guess.")
    if not os.path.isdir("/proc/%d" % pid):
        sys.exit("light_probe: no process %d" % pid)
    import time
    for i in range(a.repeat):
        if a.arrays:
            res = find_arrays(pid, a.all_regions, a.min_run)
            report_arrays(res, "%s pid %d, sample %d of %d"
                          % (a.label or "process", pid, i + 1, a.repeat),
                          a.expect)
        else:
            res = scan(pid, a.all_regions, cap=int(a.cap_gib * (1 << 30)))
            report(res, "%s pid %d, sample %d of %d"
                   % (a.label or "process", pid, i + 1, a.repeat))
        if i + 1 < a.repeat:
            time.sleep(a.interval)
    return 0


if __name__ == "__main__":
    sys.exit(main())
