#!/usr/bin/env python3
"""Diff two oracle probe streams and name the FIRST call that differs.

    tools/oraclediff.py port.bin stock.bin [--tol 1e-4] [--show 3]
    tools/oraclediff.py --selftest

Both sides record the same guest functions in the same format (src/oracle/
probe_rec.h): the port through ld --wrap, the stock game through the inline
hooks tools/proxy_d3d8 installs. This reads the two streams, lines them up by
(probe, call index), and reports where they first stop agreeing.

WHY THE FIRST ONE AND NOT ALL OF THEM. Once one bone matrix is wrong every
matrix downstream of it is wrong too, so a list of 30,000 differences says
nothing that its first entry does not. What is worth reading is the earliest
call whose INPUTS match and whose OUTPUTS do not: that call is a function that
was handed the same numbers as the real engine and produced different ones,
which is a defect in the translation of that function and nowhere else.

WHAT IS REFUSED, rather than reported as a difference:

  * a stream that is not one (wrong magic);
  * two streams recorded under different manifests -- a field that moved is not
    a disagreement between the port and the game;
  * two streams from the SAME side, which would trivially agree;
  * an empty stream: a run in which no probe fired is a harness that did not
    install, and calling that "no differences" is the exact lie this tool
    exists to prevent.

A short final record (a run ended by a kill, which every run here is) is
counted and dropped, and the count is printed.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

MAGIC = b"X2PROBE1"
HDR = len(MAGIC) + 16
UNREADABLE = 0xDB
SIDE = {0: "port", 1: "stock"}


class Refuse(Exception):
    pass


def read_stream(path):
    """-> (hash, side, {(probe,seq): bytes}, short_tail_bytes)"""
    try:
        with open(path, "rb") as f:
            blob = f.read()
    except OSError as e:
        raise Refuse("oraclediff: cannot read %s: %s" % (path, e))
    if len(blob) < HDR or blob[:len(MAGIC)] != MAGIC:
        raise Refuse(
            "oraclediff: %s does not start with %r, so it is not a probe\n"
            "  stream. %d byte(s) read." % (path, MAGIC.decode(), len(blob)))
    h, side, nprobes, _ = struct.unpack_from("<IIII", blob, len(MAGIC))
    recs, i, short = {}, HDR, 0
    while i < len(blob):
        if i + 12 > len(blob):
            short = len(blob) - i
            break
        pid, seq, n = struct.unpack_from("<III", blob, i)
        if i + 12 + n > len(blob):
            short = len(blob) - i
            break
        recs[(pid, seq)] = blob[i + 12:i + 12 + n]
        i += 12 + n
    return h, side, recs, short, nprobes


def manifest():
    """The probe table, for field names. Absent is not fatal: the diff still
    works on raw bytes, and says so rather than pretending to name fields."""
    try:
        import gen_probes
        return gen_probes.build(gen_probes.load_manifest()), None
    except Exception as e:
        return None, str(e).splitlines()[0]


def fields_of(p):
    return ([f for f in p["fields"] if f["when"] == "in"],
            [f for f in p["fields"] if f["when"] == "out"])


def split(data, flds):
    out, i = [], 0
    for f in flds:
        out.append(data[i:i + f["len"]])
        i += f["len"]
    return out, data[i:]


def as_floats(b):
    n = len(b) // 4
    return list(struct.unpack("<%df" % n, b[:n * 4]))


def fmt_field(f, b):
    if all(x == UNREADABLE for x in b) and b:
        return "%-10s UNREADABLE (%d bytes)" % (f["name"], len(b))
    if f["kind"] == "DWORD":
        v = struct.unpack("<I", b)[0]
        return "%-10s 0x%08x  (as float %g)" % (f["name"], v, as_floats(b)[0])
    vals = as_floats(b)
    rows = []
    per = 4 if len(vals) % 4 == 0 else len(vals)
    for r in range(0, len(vals), per):
        rows.append(" ".join("%10.5f" % v for v in vals[r:r + per]))
    return "%-10s %s" % (f["name"], ("\n%22s" % "").join(rows))


def unreadable(b):
    """A field the recorder could not read is written as PROBE_UNREADABLE
    across every byte, never as zeros -- so "could not read" and "read zeros"
    stay different in the stream. Such a field carries no information about
    either side and must not be counted as a difference: comparing an
    unreadable field against a real one would manufacture a divergence out of
    a recorder problem. It is counted and reported separately instead, loudly,
    because a capture that is mostly unreadable is worthless and must not read
    as agreement -- one whole control capture was exactly that."""
    return len(b) > 0 and all(x == UNREADABLE for x in b)


def compare_fields(flds, a, b, tol):
    """-> (n_compared, n_differ, n_unreadable). Field by field, so an
    unreadable one is excluded rather than poisoning the whole record."""
    av, _ = split(a, flds)
    bv, _ = split(b, flds)
    comp = diff = unread = 0
    for f, x, y in zip(flds, av, bv):
        if len(x) != f["len"] or len(y) != f["len"]:
            diff += 1                      # a short record IS a difference
            continue
        if unreadable(x) or unreadable(y):
            unread += 1
            continue
        comp += 1
        if differs(x, y, tol)[1]:
            diff += 1
    return comp, diff, unread


def differs(a, b, tol):
    """-> (any_bits_differ, beyond_tolerance)"""
    if a == b:
        return False, False
    if len(a) != len(b):
        return True, True
    fa, fb = as_floats(a), as_floats(b)
    if len(fa) * 4 != len(a):
        return True, True
    worst = 0.0
    for x, y in zip(fa, fb):
        if x != x or y != y:                       # a NaN on either side
            if (x != x) != (y != y):
                return True, True
            continue
        d = abs(x - y)
        scale = max(1.0, abs(y))
        worst = max(worst, d / scale)
    return True, worst > tol


def report(port_path, stock_path, tol, show):
    ph, pside, precs, pshort, pn = read_stream(port_path)
    sh, sside, srecs, sshort, sn = read_stream(stock_path)

    if ph != sh:
        raise Refuse(
            "oraclediff: the two streams were recorded under DIFFERENT probe\n"
            "  manifests (0x%08x and 0x%08x). A field that moved is not a\n"
            "  disagreement between the port and the game. Rebuild both sides\n"
            "  from the same tools/probes.json and re-capture." % (ph, sh))
    if pside == sside:
        raise Refuse(
            "oraclediff: both streams say they are the %s side. Comparing a\n"
            "  capture with itself finds nothing, which would read as success."
            % SIDE.get(pside, pside))
    if pside != 0:
        port_path, stock_path = stock_path, port_path
        precs, srecs = srecs, precs
        pshort, sshort = sshort, pshort
    for name, recs in ((port_path, precs), (stock_path, srecs)):
        if not recs:
            raise Refuse(
                "oraclediff: %s holds NO records. No probed function was ever\n"
                "  called in that run, which means the hooks did not install --\n"
                "  not that the two sides agree. Check the install report\n"
                "  (probe_stock.log, or the port's oracle_trace line) before\n"
                "  reading anything into this." % name)

    probes, why = manifest()
    print("port  %s: %d record(s)%s" % (port_path, len(precs),
          ", %d trailing byte(s) dropped" % pshort if pshort else ""))
    print("stock %s: %d record(s)%s" % (stock_path, len(srecs),
          ", %d trailing byte(s) dropped" % sshort if sshort else ""))
    print("manifest 0x%08x, %d probe(s) declared\n" % (ph, pn))
    if why:
        print("  NOTE: tools/probes.json could not be loaded (%s), so fields\n"
              "        are compared as raw bytes and not named.\n" % why)

    ids = sorted(set(k[0] for k in precs) | set(k[0] for k in srecs))
    total_diff = total_blind = 0
    first_bad = None
    for pid in ids:
        p = probes[pid] if probes and pid < len(probes) else None
        name = p["name"] if p else "probe %d" % pid
        pk = set(s for (i, s) in precs if i == pid)
        sk = set(s for (i, s) in srecs if i == pid)
        common = sorted(pk & sk)
        nin = nout = 0
        same_in_diff_out = []
        diff_in = n_unread = n_blind = 0
        for seq in common:
            a, b = precs[(pid, seq)], srecs[(pid, seq)]
            if p:
                fin, fout = fields_of(p)
                nbin = sum(f["len"] for f in fin)
                _, in_bad, in_un = compare_fields(fin, a[:nbin], b[:nbin], tol)
                nc, out_bad, out_un = compare_fields(fout, a[nbin:], b[nbin:],
                                                     tol)
                n_unread += in_un + out_un
                if nc == 0 and out_un:
                    # Nothing about this call could be compared at all.
                    n_blind += 1
                    continue
            else:
                in_bad = differs(a, b, tol)[1]
                out_bad = 0
            if in_bad:
                diff_in += 1
                continue
            nin += 1
            if out_bad:
                same_in_diff_out.append(seq)
        nout = len(same_in_diff_out)
        total_diff += nout
        print("%-52s port %6d  stock %6d  common %6d"
              % (name.split("::", 1)[-1], len(pk), len(sk), len(common)))
        print("     %6d call(s) received the SAME inputs; of those %d "
              "produced different outputs%s"
              % (nin, nout, "" if not diff_in else
                 "   (%d more had different inputs already)" % diff_in))
        if n_unread or n_blind:
            print("     %6d field(s) could not be READ by one side or the "
                  "other, and %d call(s) had\n            no comparable output "
                  "at all. Those are recorder failures, not\n            "
                  "disagreements, and are excluded from the counts above."
                  % (n_unread, n_blind))
        total_blind += n_blind
        if nout and first_bad is None:
            first_bad = (pid, same_in_diff_out[0])
        for seq in same_in_diff_out[:show]:
            a, b = precs[(pid, seq)], srecs[(pid, seq)]
            print("\n     --- call %d ---" % seq)
            if not p:
                print("       port  %s\n       stock %s" % (a.hex(), b.hex()))
                continue
            fin, fout = fields_of(p)
            av, rest = split(a, fin + fout)
            bv, _ = split(b, fin + fout)
            for f, x, y in zip(fin + fout, av, bv):
                tag = "  " if x == y else ">>"
                print("     %s in " % tag if f["when"] == "in" else
                      "     %s out" % tag, fmt_field(f, x))
                if x != y:
                    print("     %s    " % "  ", fmt_field(f, y).split(None, 1)[1]
                          if len(fmt_field(f, y).split(None, 1)) > 1 else "")
        print()

    if total_blind:
        print("WARNING: %d call(s) had no comparable output at all -- one side "
              "could not read\nthe memory it was asked to record. A verdict "
              "below rests on the calls that DID\ncompare; if that number is "
              "small, this capture is not evidence of anything.\n"
              % total_blind)
    if not total_diff:
        print("NO DIFFERENCES. Every call that received the same inputs "
              "produced the same outputs,\nto %g relative. The probed "
              "functions are not where the port diverges -- widen\n"
              "tools/probes.json rather than reading this as the port being "
              "correct overall." % tol)
        return 0
    pid, seq = first_bad
    nm = probes[pid]["name"] if probes and pid < len(probes) else "probe %d" % pid
    print("FIRST DIVERGENCE: %s, call %d." % (nm, seq))
    print("That call was handed the same numbers as the real engine and "
          "returned different ones.\nThe defect is in the translation of that "
          "function.")
    return 1


# ---------------------------------------------------------------------------
def _stream(side, hash_, recs):
    b = MAGIC + struct.pack("<IIII", hash_, side, 1, 0)
    for (pid, seq), data in recs:
        b += struct.pack("<III", pid, seq, len(data)) + data
    return b


def selftest():
    import tempfile
    ok = [True]
    # Not /tmp: it is a small tmpfs here and run artifacts have filled it.
    root = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "scratch", "tmp")
    os.makedirs(root, exist_ok=True)
    d = tempfile.mkdtemp(dir=root)

    # Records must have the layout the MANIFEST declares for probe 0, or the
    # in/out split below lands in the wrong half and this test silently stops
    # testing what it says it tests -- which is exactly what it did first.
    probes, _ = manifest()
    if probes:
        fin, fout = fields_of(probes[0])
        n_in = sum(f["len"] for f in fin) // 4
        n_out = sum(f["len"] for f in fout) // 4
    else:
        n_in, n_out = 4, 4
    print("  (records shaped from the manifest: %d in float(s), %d out)"
          % (n_in, n_out))

    def rec(seq, out_val=None):
        ins = [1.0, 2.0, 3.0, float(seq)] * ((n_in + 3) // 4)
        outs = [(out_val if out_val is not None else float(seq))] * n_out
        return (struct.pack("<%df" % n_in, *ins[:n_in])
                + struct.pack("<%df" % n_out, *outs))

    def rec_other_in(seq):
        ins = [5.0] * n_in
        return struct.pack("<%df" % n_in, *ins) + struct.pack("<%df" % n_out,
                                                              *([9.0] * n_out))

    def w(name, blob):
        p = os.path.join(d, name)
        open(p, "wb").write(blob)
        return p

    def check(what, cond):
        if cond:
            print("  pass  %s" % what)
        else:
            ok[0] = False
            print("  FAIL  %s" % what)

    def refuses(what, a, b):
        try:
            report(a, b, 1e-4, 1)
        except Refuse as e:
            print("  pass  %-38s refused: %s" % (what, str(e).splitlines()[0]))
            return
        ok[0] = False
        print("  FAIL  %-38s was ACCEPTED" % what)

    good = [((0, i), rec(i)) for i in range(20)]
    a = w("a.bin", _stream(0, 0xABCD, good))
    b = w("b.bin", _stream(1, 0xABCD, good))
    check("identical streams -> 0", report(a, b, 1e-4, 1) == 0)

    bad = list(good)
    bad[7] = ((0, 7), rec(7, out_val=9.0))
    c = w("c.bin", _stream(1, 0xABCD, bad))
    print("  -- one differing output at call 7:")
    check("a differing output is found", report(a, c, 1e-4, 1) == 1)

    # A difference in the INPUTS must not be reported as a defect: the two runs
    # simply were not at the same point. This is the discriminator -- without
    # it every drifting run would read as a broken function.
    drift = list(good)
    drift[7] = ((0, 7), rec_other_in(7))
    e = w("e.bin", _stream(1, 0xABCD, drift))
    print("  -- differing INPUTS at call 7 (a drifted run, not a defect):")
    check("differing inputs are NOT a divergence", report(a, e, 1e-4, 1) == 0)

    # An UNREADABLE field must not manufacture a divergence. This is the case
    # that made a whole 9-minute control capture worthless: the stock recorder
    # failed every read, and without this the diff would have called each of
    # those fields a difference and pointed at a defect that was not there.
    un = list(good)
    n_out_bytes = n_out * 4
    un[7] = ((0, 7), rec(7)[:-n_out_bytes] + bytes([UNREADABLE]) * n_out_bytes)
    u = w("u.bin", _stream(1, 0xABCD, un))
    print("  -- one UNREADABLE output field at call 7:")
    check("an unreadable field is not a divergence", report(a, u, 1e-4, 1) == 0)

    refuses("a mismatched manifest hash", a, w("h.bin", _stream(1, 0x1234, good)))
    refuses("two streams from the same side", a, w("s.bin", _stream(0, 0xABCD, good)))
    refuses("an empty stream", a, w("z.bin", _stream(1, 0xABCD, [])))
    refuses("a file that is not a stream", a, w("n.bin", b"not a probe stream"))
    refuses("a missing file", a, os.path.join(d, "nope.bin"))

    # A truncated tail must be counted, not silently swallowed.
    t = _stream(1, 0xABCD, good)[:-5]
    _, _, recs, short, _ = read_stream(w("t.bin", t))
    check("a truncated final record is counted (%d byte(s), %d kept)"
          % (short, len(recs)), short > 0 and len(recs) == 19)

    print("\nSELFTEST", "PASSED" if ok[0] else "FAILED")
    return 0 if ok[0] else 1


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?")
    ap.add_argument("stock", nargs="?")
    ap.add_argument("--tol", type=float, default=1e-4,
                    help="relative tolerance on float fields (default 1e-4)")
    ap.add_argument("--show", type=int, default=3,
                    help="differing calls to print per probe (default 3)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.port or not a.stock:
        ap.error("both streams are required (or --selftest)")
    try:
        return report(a.port, a.stock, a.tol, a.show)
    except Refuse as e:
        sys.stderr.write(str(e) + "\n")
        return 2


if __name__ == "__main__":
    sys.exit(main())
