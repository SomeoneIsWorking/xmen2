#!/usr/bin/env python3
"""Compare the light path of the PORT against the STOCK control, line for line.

Both sides write the SAME format:

  the control  tools/proxy_d3d8   a logging d3d8.dll the real game loads under
                                  Wine; build_stocklog.sh stages it, run_shim.sh
                                  runs it, and it writes d3d8_lightlog.txt
  the port     X2_LIGHTLOG=<path> (src/d3d8/d3d8_device.c)

so "does the original engine set the same lights" is a diff rather than an
argument. C199 established that the port's D3D8 layer faithfully reports what
the recompiled engine hands it, and that those lights are nearly black; the
open question was always what the ORIGINAL engine hands over for the same
scene. A memory-scanning probe was tried first (tools/light_probe.py) and could
not anchor itself in the control's heap.

  lightlog_diff.py <control.txt> <port.txt>
  lightlog_diff.py --selftest

WHAT IS NOT COMPARED, said out loud rather than left to be discovered:

  * `t=`. The control's is GetTickCount (ms since Windows booted), the port's
    is ms since its first logged line. Same units, different origins.
  * The ORDER and COUNT of calls. The two runs are not frame-locked and do not
    reach the same scene at the same moment, so a difference in how many times
    an index was set says nothing on its own. What is compared is the SET of
    values each index ever carried, and which indices were ever ENABLED --
    facts a run either exhibits or does not.
  * Anything outside the light path. Nothing here can see geometry, textures
    or the shader.

A comparison is only as good as the scenes behind it. Two logs from different
scenes will differ for reasons that are not defects, so the tool refuses to
grade and instead states what each side did; the SUMMARY names the differences
worth explaining, and it is on the reader to have driven both runs to the same
place.
"""
import os
import sys
from collections import OrderedDict

KINDS = ("SETLIGHT", "LIGHTENABLE", "SETMATERIAL", "SETRENDERSTATE")
IGNORED_KINDS = ("PROXY", "DIRECT3DCREATE8", "CREATEDEVICE", "CAPS",
                 "CREATEVERTEXSHADER", "SETVERTEXSHADER")


def parse(path):
    """-> (per-kind records, unparsed-count, total). Refuses nothing; the
    caller decides, because 'this file has no lights' is itself an answer that
    must be distinguishable from 'this file could not be read'."""
    if not os.path.exists(path):
        sys.exit("lightlog_diff: %s does not exist -- compared NOTHING" % path)
    lights = OrderedDict()     # idx -> {values: {tuple: count}, calls: n}
    enables = OrderedDict()    # idx -> {"on": n, "off": n, "last": bool}
    materials = {}             # value tuple -> count
    states = {}                # "LIGHTING=1" -> count
    unparsed = []
    total = 0
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            total += 1
            kind = line.split(" ", 1)[0]
            # The proxy writes these diagnostics into the same file. They are
            # not light-path records, but they are a closed, named vocabulary;
            # arbitrary unknown lines still refuse below.
            if kind in IGNORED_KINDS:
                continue
            fields = {}
            for tok in line.split()[1:]:
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    fields[k] = v
            if kind == "SETLIGHT":
                if "idx" not in fields:
                    unparsed.append(line)
                    continue
                idx = int(fields["idx"])
                e = lights.setdefault(idx, {"values": {}, "calls": 0})
                e["calls"] += 1
                if "type" not in fields:        # NULL-POINTER line
                    key = ("NULL-POINTER",)
                else:
                    key = (fields.get("type"), fields.get("diffuse"),
                           fields.get("ambient"), fields.get("range"),
                           fields.get("atten"))
                e["values"][key] = e["values"].get(key, 0) + 1
            elif kind == "LIGHTENABLE":
                if "idx" not in fields or "on" not in fields:
                    unparsed.append(line)
                    continue
                idx = int(fields["idx"])
                e = enables.setdefault(idx, {"on": 0, "off": 0, "last": None})
                on = fields["on"] not in ("0", "false")
                e["on" if on else "off"] += 1
                e["last"] = on
            elif kind == "SETMATERIAL":
                key = (fields.get("diffuse"), fields.get("ambient"),
                       fields.get("emissive"))
                materials[key] = materials.get(key, 0) + 1
            elif kind == "SETRENDERSTATE":
                for k in ("LIGHTING", "AMBIENT"):
                    if k in fields:
                        s = "%s=%s" % (k, fields[k])
                        states[s] = states.get(s, 0) + 1
                        break
                else:
                    unparsed.append(line)
            else:
                unparsed.append(line)
    return {"lights": lights, "enables": enables, "materials": materials,
            "states": states, "unparsed": unparsed, "total": total,
            "path": path}


def refuse_unless_usable(side, d):
    """FAIL FAST. A log with no SetLight in it cannot answer the question, and
    reporting 'no difference' from one is the exact failure this project keeps
    hitting: a negative that the method could never have contradicted."""
    if d["total"] == 0:
        sys.exit("lightlog_diff: the %s log %s is EMPTY -- the run wrote no "
                 "lines at all. Compared NOTHING." % (side, d["path"]))
    if not d["lights"]:
        sys.exit("lightlog_diff: the %s log %s has %d line(s) but NO SETLIGHT "
                 "-- either the run never reached a lit scene or the logger is "
                 "not wired in. Compared NOTHING."
                 % (side, d["path"], d["total"]))
    if d["unparsed"]:
        sys.exit("lightlog_diff: the %s log %s has %d line(s) this tool could "
                 "not parse, the first being:\n  %s\nA silently skipped line is "
                 "a fact dropped from the comparison. Compared NOTHING."
                 % (side, d["path"], len(d["unparsed"]), d["unparsed"][0]))


def fmt_values(entry, limit=4):
    """The distinct (type, diffuse, ...) tuples an index carried, commonest
    first. Truncation is REPORTED, never silent."""
    items = sorted(entry["values"].items(), key=lambda kv: -kv[1])
    out = []
    for key, n in items[:limit]:
        if key == ("NULL-POINTER",):
            out.append("    x%-6d NULL-POINTER" % n)
        else:
            t, dif, amb, rng, att = key
            out.append("    x%-6d type=%s diffuse=%s ambient=%s range=%s "
                       "atten=%s" % (n, t, dif, amb, rng, att))
    if len(items) > limit:
        out.append("    ... and %d more distinct value(s), not shown"
                   % (len(items) - limit))
    return out


def report(control, port):
    idxs = sorted(set(control["lights"]) | set(port["lights"]) |
                  set(control["enables"]) | set(port["enables"]))
    print("LIGHT INDICES: %d in the control, %d in the port, %d in either"
          % (len(control["lights"]), len(port["lights"]), len(idxs)))
    print()

    diffs = []
    for idx in idxs:
        cl = control["lights"].get(idx)
        pl = port["lights"].get(idx)
        ce = control["enables"].get(idx)
        pe = port["enables"].get(idx)

        def enabled(e):
            if e is None:
                return "never called"
            return "on x%d, off x%d, last=%s" % (e["on"], e["off"],
                                                 "ON" if e["last"] else "off")

        print("index %d" % idx)
        print("  control: %s SetLight call(s); LightEnable %s"
              % (cl["calls"] if cl else 0, enabled(ce)))
        if cl:
            for line in fmt_values(cl):
                print(line)
        print("  port:    %s SetLight call(s); LightEnable %s"
              % (pl["calls"] if pl else 0, enabled(pe)))
        if pl:
            for line in fmt_values(pl):
                print(line)

        # The two facts that do not depend on scene timing.
        c_on = bool(ce and ce["on"])
        p_on = bool(pe and pe["on"])
        if c_on != p_on:
            diffs.append("index %d was ever ENABLED in the %s but NEVER in the "
                         "%s" % (idx, "control" if c_on else "port",
                                 "port" if c_on else "control"))
        if cl and pl:
            cv = set(cl["values"])
            pv = set(pl["values"])
            if not (cv & pv):
                diffs.append("index %d carried NO value in common: control %s, "
                             "port %s" % (idx, sorted(cv)[0], sorted(pv)[0]))
        elif cl and not pl:
            diffs.append("index %d was SET in the control and never in the port"
                         % idx)
        elif pl and not cl:
            diffs.append("index %d was SET in the port and never in the control"
                         % idx)
        print()

    print("RENDER STATES")
    for side, d in (("control", control), ("port", port)):
        if d["states"]:
            print("  %-8s %s" % (side, ", ".join(
                "%s x%d" % (k, v) for k, v in sorted(d["states"].items()))))
        else:
            print("  %-8s no LIGHTING or AMBIENT state was ever set" % side)
    print()
    print("MATERIALS: %d distinct in the control, %d in the port"
          % (len(control["materials"]), len(port["materials"])))
    print()

    print("SUMMARY -- %d difference(s) that do not depend on run timing"
          % len(diffs))
    for d in diffs:
        print("  * %s" % d)
    if not diffs:
        print("  (none: every index enabled in one was enabled in the other,")
        print("   and every index set on both sides shared at least one value)")
    print()
    print("NOT COMPARED: t=, call counts, call order -- the two runs are not")
    print("frame-locked. A difference above is only a DEFECT if both runs were")
    print("driven to the same scene; this tool cannot check that and does not")
    print("claim to.")
    return diffs


# ------------------------------------------------------------------ selftest
#
# An instrument is trusted only once it has shown the OTHER answer. These feed
# it a pair that MUST come out different and a pair that MUST come out
# identical -- both through the same code path the real comparison uses.

SELF_A = """PROXY LOADED t=1 real_d3d8=x
CAPS BLOCK from IDirect3D8::GetDeviceCaps -- 53 field(s)
CAPS MaxActiveLights = 0x00000008
SETVERTEXSHADER t=1 handle=0x00000142 an FVF code
SETRENDERSTATE t=2 LIGHTING=1
SETLIGHT t=3 idx=4 type=1 diffuse=1.0000,1.0000,1.0000,1.0000 \
specular=0,0,0,0 ambient=0,0,0,0 pos=1,2,3 dir=0,0,1 range=500.00 \
falloff=1.00 atten=1.000000,0.000000,0.00000000 theta=0.000 phi=0.000
LIGHTENABLE t=4 idx=4 on=1
SETMATERIAL t=5 diffuse=1,1,1,1 ambient=0,0,0,1 emissive=0,0,0,1 \
specular=0,0,0,1 power=0.00
"""

# Same shape, black diffuse, and never enabled: the two differences the whole
# investigation turns on.
SELF_B = SELF_A.replace("diffuse=1.0000,1.0000,1.0000,1.0000",
                        "diffuse=0.0000,0.0000,0.0000,1.0000") \
               .replace("LIGHTENABLE t=4 idx=4 on=1",
                        "LIGHTENABLE t=4 idx=4 on=0")


def selftest():
    import tempfile
    ok = True
    with tempfile.TemporaryDirectory() as td:
        a = os.path.join(td, "a.txt")
        b = os.path.join(td, "b.txt")
        open(a, "w").write(SELF_A)
        open(b, "w").write(SELF_B)

        da, db = parse(a), parse(b)
        for side, d in (("a", da), ("b", db)):
            if d["unparsed"]:
                print("SELFTEST FAIL: log %s had %d unparsed line(s): %s"
                      % (side, len(d["unparsed"]), d["unparsed"][0]))
                ok = False

        if da["total"] != 8:
            print("SELFTEST FAIL: known non-light proxy records were not read "
                  "from the same stream (got %d total line(s), want 8)"
                  % da["total"])
            ok = False

        # MUST differ: black diffuse and never-enabled.
        diffs = report(da, db)
        want = 2
        if len(diffs) != want:
            print("SELFTEST FAIL: a black-and-disabled light against a white "
                  "enabled one must produce %d difference(s); produced %d"
                  % (want, len(diffs)))
            ok = False

        # MUST NOT differ: a log against itself.
        same = report(parse(a), parse(a))
        if same:
            print("SELFTEST FAIL: a log compared against ITSELF produced %d "
                  "difference(s)" % len(same))
            ok = False

        # MUST refuse: a file with lines but no SetLight.
        c = os.path.join(td, "c.txt")
        open(c, "w").write("SETRENDERSTATE t=1 LIGHTING=1\n")
        pid = os.fork()
        if pid == 0:
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, 1)
            os.dup2(devnull, 2)
            try:
                refuse_unless_usable("test", parse(c))
            except SystemExit:
                os._exit(1)
            os._exit(0)
        _, status = os.waitpid(pid, 0)
        if os.WEXITSTATUS(status) == 0:
            print("SELFTEST FAIL: a log with no SETLIGHT was ACCEPTED; it must "
                  "be refused, because 'no difference' from it is a negative "
                  "the method could not have contradicted")
            ok = False

        # MUST still refuse an unknown record. Recognising the proxy's closed
        # diagnostics vocabulary must not turn this into a skip-anything parser.
        open(c, "w").write(SELF_A + "MYSTERY t=9 value=1\n")
        pid = os.fork()
        if pid == 0:
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, 1)
            os.dup2(devnull, 2)
            try:
                refuse_unless_usable("test", parse(c))
            except SystemExit:
                os._exit(1)
            os._exit(0)
        _, status = os.waitpid(pid, 0)
        if os.WEXITSTATUS(status) == 0:
            print("SELFTEST FAIL: an unknown proxy record was silently skipped")
            ok = False

    print("SELFTEST: %s" % ("PASS -- the comparison shows both answers and "
                            "refuses an unusable log" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        sys.exit(selftest())
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    ctrl = parse(sys.argv[1])
    prt = parse(sys.argv[2])
    refuse_unless_usable("control", ctrl)
    refuse_unless_usable("port", prt)
    print("control: %s  (%d line(s))" % (ctrl["path"], ctrl["total"]))
    print("port:    %s  (%d line(s))" % (prt["path"], prt["total"]))
    print()
    report(ctrl, prt)
