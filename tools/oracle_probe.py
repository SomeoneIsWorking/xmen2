#!/usr/bin/env python3
"""
Read RUN DATA out of a live control run, instead of reading its pixels.

## Why this exists

The oracle keeps frames. Frames answer "what did it look like", and every
question worth asking of the control lately has been "what did it DO" -- did
the conversation advance, did the script launch, when did the party empty. The
only way that was being answered was by taking more screenshots and bisecting
them by eye, which is slow, needs a human, and cannot see anything that is not
drawn.

XMen2.exe maps at 0x00400000 under Wine exactly as it does natively, and this
host allows same-user process_vm_readv (ptrace_scope 0). So the control's own
state can simply be READ, at whatever rate is useful, from outside, without
stopping it and without a debugger. The port already reports these same fields
about itself, so the two become directly comparable numbers.

## What it refuses to do

  * It will not guess which process to read. `--pid` names one; without it,
    discovery must match EXACTLY one process or it refuses and lists what it
    found. Several agents and the user run the same binaries here.
  * It verifies the image before sampling: 'MZ' at the base, and the PE's
    SizeOfImage equal to the installed XMen2.exe's. A wrong process would
    otherwise produce a column of plausible zeros.
  * It reports the read-failure count and the distinct values seen for every
    field, ALWAYS -- including zero. "The conversation flags never changed" and
    "the flags were never readable" must not look alike, which is the failure
    mode every diagnostic here has had at least once.

## Use

    tools/oracle_probe.py --pid 12345 --seconds 300 --out scratch/logs/ctl.csv
    tools/oracle_probe.py --discover --seconds 300 --out ...
    tools/oracle_probe.py --selftest

The field set is CONVERSATION-specific and named below; it is the set
src/native/conversation.c reports about the port, so a comparison is a diff of
two CSVs rather than a judgement about two pictures.
"""
import argparse
import ctypes
import ctypes.util
import os
import sys
import time
from typing import ClassVar

BASE = 0x00400000

# (name, kind, chain) -- chain is a list of offsets; the FIRST element is an
# absolute guest address, each later one is added after a dword dereference.
# kind is 'u32', 'u16', 'i16' or 'u8'.
#
# These are the conversation manager's fields, documented in
# docs/RE/conversations.md. The singleton pointer lives at 0x00717aac.
FIELDS = [
    ("conv",        "u32", [0x00717AAC]),
    ("flags",       "u8",  [0x00717AAC, 0x21B24]),
    ("cur_line",    "u32", [0x00717AAC, 0x004BC]),
    ("resp_count",  "u32", [0x00717AAC, 0x004E0]),
    ("chosen",      "u32", [0x00717AAC, 0x239A0]),
    ("tag_index",   "i16", [0x00717AAC, 0x21B26]),
    ("tag_count",   "i16", [0x00717AAC, 0x21B28]),
    ("voice",       "u32", [0x00717AAC, 0x21B80]),
    # CPopupDialog (0x008b13ec). Its slot +0x78 predicate -- "is a dialog up" --
    # is the FIRST gate in the level state machine's frame, so a dialog that
    # stays up stops the conversation update, the party update and everything
    # else behind it. Up to three dialogs of stride 0x1560 live at +0x18, with
    # the current index at +0x403c and each one's active bit at +0x155d.
    ("dlg",         "u32", [0x008B13EC]),
    ("dlg_index",   "u32", [0x008B13EC, 0x0403C]),
    ("dlg0_active", "u8",  [0x008B13EC, 0x18 + 0x155D]),
    ("dlg1_active", "u8",  [0x008B13EC, 0x18 + 0x1560 + 0x155D]),
    # The game-over trigger, FUN_0041c9b0. It is guarded by two things and both
    # are plain globals, so both can simply be watched:
    #   0x0070b70d  set to 1 the first time it fires; a second call returns
    #   0x00782728  FUN_004c87a0 returns (byte != 0xFF), and a TRUE return
    #               SKIPS the game over -- so the prompt can only appear while
    #               this byte is 0xFF
    ("go_fired",    "u8",  [0x0070B70D]),
    ("go_guard",    "u8",  [0x00782728]),
]

# Fields whose every CHANGE is worth a line of its own, not just a CSV row.
EVENT_FIELDS = ("flags", "cur_line", "chosen", "resp_count",
                "dlg_index", "dlg0_active", "dlg1_active",
                "go_fired", "go_guard")


class Reader(object):
    """process_vm_readv, wrapped so an unreadable address is an answer."""

    def __init__(self, pid):
        self.pid = pid
        libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
        self._readv = libc.process_vm_readv
        self._readv.restype = ctypes.c_ssize_t

        class IOVec(ctypes.Structure):
            _fields_: ClassVar[list[tuple[str, object]]] = [
                ("base", ctypes.c_void_p), ("len", ctypes.c_size_t)
            ]

        self.IOVec = IOVec
        self.fails = 0
        self.reads = 0

    def read(self, addr, n):
        buf = (ctypes.c_char * n)()
        local = self.IOVec(ctypes.cast(buf, ctypes.c_void_p), n)
        remote = self.IOVec(ctypes.c_void_p(addr), n)
        self.reads += 1
        got = self._readv(self.pid, ctypes.byref(local), 1,
                          ctypes.byref(remote), 1, 0)
        if got != n:
            self.fails += 1
            return None
        return bytes(buf)

    def u32(self, addr):
        b = self.read(addr, 4)
        return None if b is None else int.from_bytes(b, "little")

    def u16(self, addr):
        b = self.read(addr, 2)
        return None if b is None else int.from_bytes(b, "little")

    def u8(self, addr):
        b = self.read(addr, 1)
        return None if b is None else b[0]


def read_field(rd, kind, chain):
    addr = chain[0]
    for off in chain[1:]:
        v = rd.u32(addr)
        if not v:
            return None
        addr = v + off
    if kind == "u32":
        return rd.u32(addr)
    if kind == "u16":
        return rd.u16(addr)
    if kind == "i16":
        v = rd.u16(addr)
        return None if v is None else (v - 0x10000 if v >= 0x8000 else v)
    if kind == "u8":
        return rd.u8(addr)
    raise SystemExit("oracle_probe: unknown kind %r" % kind)


def installed_size_of_image():
    """SizeOfImage from the XMen2.exe on disk, for the identity check."""
    d = os.environ.get("GAME_PC_DIR")
    if not d:
        return None
    p = os.path.join(d, "XMen2.exe")
    if not os.path.exists(p):
        return None
    with open(p, "rb") as f:
        blob = f.read(0x400)
    pe = int.from_bytes(blob[0x3C:0x40], "little")
    return int.from_bytes(blob[pe + 24 + 56:pe + 24 + 60], "little")


def verify(rd):
    """Refuse a process that is not the game. Returns a reason, or None."""
    mz = rd.read(BASE, 2)
    if mz is None:
        return ("nothing is mapped at 0x%08x in pid %d -- either this is not "
                "the game, or its memory is not readable from here" % (BASE, rd.pid))
    if mz != b"MZ":
        return ("0x%08x in pid %d does not start with 'MZ' (found %r), so the "
                "exe is not mapped at its preferred base" % (BASE, rd.pid, mz))
    want = installed_size_of_image()
    if want is None:
        return None            # cannot check; say so rather than pretend
    pe_off = rd.u32(BASE + 0x3C)
    if pe_off is None or pe_off > 0x1000:
        return "the PE header offset at 0x%08x did not read back" % (BASE + 0x3C)
    got = rd.u32(BASE + pe_off + 24 + 56)
    if got != want:
        return ("SizeOfImage in pid %d is 0x%x but the installed XMen2.exe says "
                "0x%x -- this is a different image" % (rd.pid, got or 0, want))
    return None


def discover():
    """Every pid whose cmdline mentions XMen2.exe. Ambiguity is refused."""
    hits = []
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            with open("/proc/%s/cmdline" % name, "rb") as f:
                cmd = f.read().replace(b"\0", b" ").decode("utf-8", "replace")
        except (IOError, OSError):
            continue
        if "XMen2.exe" in cmd or "x2native" in cmd:
            hits.append((int(name), cmd.strip()))
    return hits


def sample(pid, seconds, hz, out, quiet=False):
    rd = Reader(pid)
    why = verify(rd)
    if why:
        sys.exit("oracle_probe: REFUSING to sample -- %s" % why)

    names = [f[0] for f in FIELDS]
    seen = dict((n, set()) for n in names)
    last = dict((n, "<unset>") for n in names)
    events = []
    rows = 0
    t0 = time.time()
    period = 1.0 / hz

    fh = open(out, "w")
    fh.write("# oracle_probe: pid %d, %g s at %g Hz, fields from "
             "docs/RE/conversations.md\n" % (pid, seconds, hz))
    fh.write("t," + ",".join(names) + "\n")
    try:
        while time.time() - t0 < seconds:
            if not os.path.exists("/proc/%d" % pid):
                events.append((time.time() - t0,
                               "the process exited; sampling stopped here"))
                break
            t = time.time() - t0
            vals = []
            for name, kind, chain in FIELDS:
                v = read_field(rd, kind, chain)
                vals.append("" if v is None else str(v))
                seen[name].add(v)
                if name in EVENT_FIELDS and v != last[name]:
                    events.append((t, "%s %s -> %s" % (name, last[name], v)))
                    last[name] = v
            fh.write("%.3f,%s\n" % (t, ",".join(vals)))
            rows += 1
            time.sleep(period)
    finally:
        fh.close()

    if quiet:
        return rows, rd, seen, events

    print("oracle_probe: %d row(s) over %.1f s from pid %d; %d of %d read(s) "
          "failed" % (rows, time.time() - t0, pid, rd.fails, rd.reads))
    for name in names:
        vs = seen[name]
        known = sorted(v for v in vs if v is not None)
        print("  %-11s %d distinct value(s)%s%s"
              % (name, len(known),
                 "" if None not in vs else " (and some reads failed)",
                 "" if not known else ": " + ", ".join(
                     str(v) for v in known[:8]) + ("" if len(known) <= 8 else " ...")))
    print("  %d state change(s) in %s:"
          % (len(events), ", ".join(EVENT_FIELDS)))
    if not events:
        print("    NONE -- every watched field held one value for the whole "
              "sample. That is a measurement, not a missing instrument: the "
              "read-failure count above is how you tell them apart.")
    for t, msg in events[:200]:
        print("    %8.3fs  %s" % (t, msg))
    if len(events) > 200:
        print("    ... and %d more" % (len(events) - 200))
    print("  the rows are in %s" % out)
    return rows, rd, seen, events


def selftest():
    """Prove the reader gives BOTH answers: a value it can read, and a refusal.

    It probes THIS process, whose memory it knows, because an instrument that
    has only ever been shown succeeding is one that will report a column of
    zeros for a process it cannot see.
    """
    ok = True

    def check(name, cond):
        nonlocal ok
        print("  %-52s %s" % (name, "ok" if cond else "FAILED"))
        ok = ok and cond

    rd = Reader(os.getpid())

    buf = ctypes.create_string_buffer(8)
    buf.raw = (0x12345678).to_bytes(4, "little") + b"\0\0\0\0"
    addr = ctypes.addressof(buf)
    check("reads a known dword out of its own memory",
          rd.u32(addr) == 0x12345678)

    buf.raw = (0x9ABCDEF0).to_bytes(4, "little") + b"\0\0\0\0"
    check("sees the value CHANGE (so it is not caching)",
          rd.u32(addr) == 0x9ABCDEF0)

    before = rd.fails
    check("an unmapped address returns None, not a plausible zero",
          rd.u32(0x10) is None)
    check("...and is COUNTED as a failed read", rd.fails == before + 1)

    check("a signed 16-bit field sign-extends",
          (lambda v: v == -2)(
              (lambda raw: (raw - 0x10000 if raw >= 0x8000 else raw))(0xFFFE)))

    check("verify() refuses a process with no MZ at the base",
          verify(Reader(os.getpid())) is not None)

    hits = discover()
    check("discovery returns a list (it may legitimately be empty here)",
          isinstance(hits, list))

    print("selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def dump(pid, spec, nbytes):
    """One-shot hex/pointer view of a guest object, for finding a field.

    A field map has to come from somewhere, and guessing offsets out of a
    disassembly is slower than looking. Every dword that lands inside the exe's
    .rdata is annotated with the NUL-terminated string it points at, which is
    how a dialog's message token is found without knowing which slot holds it.
    """
    rd = Reader(pid)
    why = verify(rd)
    if why:
        sys.exit("oracle_probe: REFUSING to dump -- %s" % why)
    chain = [int(x, 0) for x in spec.split(",")]
    addr = chain[0]
    for off in chain[1:]:
        v = rd.u32(addr)
        if not v:
            sys.exit("oracle_probe: the chain went NULL at 0x%08x; there is "
                     "nothing to dump." % addr)
        addr = v + off
    print("oracle_probe: %d byte(s) at 0x%08x in pid %d" % (nbytes, addr, pid))
    shown = 0
    for i in range(0, nbytes, 4):
        v = rd.u32(addr + i)
        if v is None:
            print("  +0x%04x  <unreadable>" % i)
            continue
        note = ""
        if 0x00400000 <= v < 0x00A80000:
            b = rd.read(v, 48)
            if b:
                t = b.split(b"\0")[0]
                if len(t) >= 3 and all(0x20 <= c < 0x7f for c in t):
                    note = "  -> %r" % t
        if v or note:
            print("  +0x%04x  0x%08x%s" % (i, v, note))
            shown += 1
    print("  %d non-zero dword(s) of %d; the zeros are omitted."
          % (shown, nbytes // 4))


def main():
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("--pid", type=int)
    ap.add_argument("--discover", action="store_true")
    ap.add_argument("--seconds", type=float, default=300.0)
    ap.add_argument("--hz", type=float, default=10.0)
    ap.add_argument("--out", default="scratch/logs/oracle_probe.csv")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--dump", metavar="ADDR[,OFF...]",
                    help="one-shot dump of a guest object; the chain is an "
                         "absolute address followed by offsets applied after "
                         "each dword dereference")
    ap.add_argument("--dump-bytes", type=lambda x: int(x, 0), default=0x200)
    a = ap.parse_args()

    if a.selftest:
        return selftest()

    pid = a.pid
    if pid is None:
        hits = discover()
        if len(hits) != 1:
            print("oracle_probe: REFUSING to guess -- %d candidate process(es) "
                  "match. Several agents and the user run the same binaries "
                  "here, so pass --pid." % len(hits), file=sys.stderr)
            for p, cmd in hits:
                print("    %6d  %s" % (p, cmd[:110]), file=sys.stderr)
            return 2
        pid = hits[0][0]
        print("oracle_probe: the only matching process is pid %d" % pid)

    if a.dump:
        dump(pid, a.dump, a.dump_bytes)
        return 0

    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    sample(pid, a.seconds, a.hz, a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
