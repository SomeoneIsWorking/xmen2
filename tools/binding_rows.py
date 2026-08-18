#!/usr/bin/env python3
"""Re-read XMen2.exe's 42 binding-row names, and diff them against the port.

`src/native/input_bindings.c` ships those names as a C table. A shipped
constant that came from a measurement has to be checked against that
measurement BY CODE, or it drifts silently the first time someone "fixes" a
typo -- and "SreenGrab" is a typo in the original that the registry key depends
on.

The names are `MOV dword ptr [ESP+disp], <string>` immediates inside
FUN_0061b030, the function that fills the keyboard defaults and formats each
row into `Controls\\Player%d\\<name>1`. Ordering is by ESP displacement, which
is the row order.

    tools/binding_rows.py                 # list what the exe says
    tools/binding_rows.py --check FILE    # diff against the C table
    tools/binding_rows.py --selftest      # what ctest runs

A run that cannot find the game exits 77 (ctest SKIP) and says so. A run that
finds the function but recovers the wrong NUMBER of names exits 1: a partial
read must never be reported as agreement.
"""

import argparse
import os
import re
import struct
import sys

FN_START = 0x0061B030
FN_BYTES = 2725
ROWS = 42


def load_env(root):
    """GAME_PC_DIR from the environment, else from the gitignored .env."""
    if os.environ.get("GAME_PC_DIR"):
        return os.environ["GAME_PC_DIR"]
    path = os.path.join(root, ".env")
    if not os.path.isfile(path):
        return None
    for line in open(path):
        m = re.match(r'\s*GAME_PC_DIR\s*=\s*"?([^"\n]*)"?', line)
        if m:
            return m.group(1)
    return None


class Image:
    """Just enough PE to turn a virtual address into a file offset."""

    def __init__(self, path):
        self.data = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        nsec = struct.unpack_from("<H", self.data, pe + 6)[0]
        optsz = struct.unpack_from("<H", self.data, pe + 20)[0]
        self.base = struct.unpack_from("<I", self.data, pe + 24 + 28)[0]
        self.secs = []
        off = pe + 24 + optsz
        for _ in range(nsec):
            vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII",
                                                            self.data, off + 8)
            self.secs.append((vaddr, max(vsize, rsize), raddr))
            off += 40

    def offset(self, va):
        rva = va - self.base
        for vaddr, size, raddr in self.secs:
            if vaddr <= rva < vaddr + size:
                return raddr + (rva - vaddr)
        return None

    def string(self, va, limit=120):
        off = self.offset(va)
        if off is None:
            return None
        end = self.data.find(b"\0", off, off + limit)
        if end < 0:
            return None
        try:
            text = self.data[off:end].decode("ascii")
        except UnicodeDecodeError:
            return None
        return text if text and text.isprintable() else None


def names_from_exe(image):
    """The row names, in ESP-displacement order, plus what was scanned."""
    start = image.offset(FN_START)
    if start is None:
        raise SystemExit("binding_rows: 0x%08x is not in this image -- this is "
                         "not the XMen2.exe the port was read against.\n"
                         % FN_START)
    body = image.data[start:start + FN_BYTES]
    found, scanned = {}, 0
    i = 0
    while i < len(body) - 6:
        if body[i] == 0xC7 and body[i + 1] == 0x44 and body[i + 2] == 0x24:
            disp = body[i + 3]
            imm = struct.unpack_from("<I", body, i + 4)[0]
            width = 8
        elif body[i] == 0xC7 and body[i + 1] == 0x84 and body[i + 2] == 0x24:
            disp = struct.unpack_from("<I", body, i + 3)[0]
            imm = struct.unpack_from("<I", body, i + 7)[0]
            width = 11
        else:
            i += 1
            continue
        scanned += 1
        text = image.string(imm)
        if text:
            found[disp] = text
        i += width
    ordered = [found[d] for d in sorted(found)]
    return ordered, scanned


def names_from_c(path):
    text = open(path).read()
    m = re.search(r"ROW_NAMES\[INPUT_BINDING_ROWS\]\s*=\s*\{(.*?)\};",
                  text, re.S)
    if not m:
        raise SystemExit("binding_rows: no ROW_NAMES table in %s -- the port "
                         "moved it, and this check has to move with it.\n"
                         % path)
    return re.findall(r'"([^"]*)"', m.group(1))


def report(image_path, c_path):
    image = Image(image_path)
    exe, scanned = names_from_exe(image)
    print("binding_rows: scanned %d ESP immediates in FUN_0061b030; %d of them "
          "point at a string" % (scanned, len(exe)))
    # A short read means the scan is wrong, not that the game has fewer rows.
    # Never let that pass as agreement.
    names = exe
    if len(names) != ROWS:
        print("binding_rows: recovered %d row names, expected %d. REFUSING to "
              "report a diff from a partial read." % (len(names), ROWS),
              file=sys.stderr)
        return 1
    if c_path is None:
        for i, n in enumerate(names):
            print("  row %2d  %s" % (i, n))
        return 0
    shipped = names_from_c(c_path)
    if len(shipped) != ROWS:
        print("binding_rows: %s ships %d names, not %d."
              % (c_path, len(shipped), ROWS), file=sys.stderr)
        return 1
    bad = [(i, a, b) for i, (a, b) in enumerate(zip(names, shipped)) if a != b]
    for i, a, b in bad:
        print("  row %2d: exe says %r, the port ships %r" % (i, a, b),
              file=sys.stderr)
    print("binding_rows: %d of %d row names agree with the exe"
          % (ROWS - len(bad), ROWS))
    return 1 if bad else 0


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__,
             formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", metavar="FILE",
                    help="diff against a C file's ROW_NAMES table")
    ap.add_argument("--selftest", action="store_true",
                    help="check the shipped table; SKIP (77) with no install")
    args = ap.parse_args()

    check = args.check
    if args.selftest:
        check = os.path.join(root, "src", "native", "input_bindings.c")

    game = load_env(root)
    exe = os.path.join(game, "XMen2.exe") if game else None
    if not exe or not os.path.isfile(exe):
        print("binding_rows: SKIPPING -- GAME_PC_DIR is unset or has no "
              "XMen2.exe, so there is nothing to read the names OUT of.\n"
              "  This is a skip, not a pass: the shipped table was not "
              "checked against anything.", file=sys.stderr)
        return 77
    return report(exe, check)


if __name__ == "__main__":
    sys.exit(main())
