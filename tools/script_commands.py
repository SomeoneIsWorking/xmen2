#!/usr/bin/env python3
"""Extract XMen2.exe's BehavEd script-command table.

Every `.PY` a level runs -- `lockControls(-1.000)`, `act("spwnr_nightcrawler")`,
`startConversation(...)` -- is dispatched through one table of 289 commands.
Knowing which C function implements a command is what makes a script-level
question answerable at all: without it, "did startConversation run?" cannot be
instrumented.

THE LAYOUT, and the trap in it. Each entry is four dwords:

    { handler, name, returnType, argSpec }

and the table starts at 0x0068a908 -- the address FUN_0049fe30 hands to the
registrar. Split one dword later and every name pairs with the PREVIOUS
command's handler: the names still look right, the handlers are all wrong, and
an override registered on one never fires. The tell is the argument spec. Read
correctly `lockControls` takes `f` and `act` takes `aa`, which is what the
shipped scripts pass; read one field out, both come back empty.

    tools/script_commands.py                 # the whole table
    tools/script_commands.py lockControls    # one command
    tools/script_commands.py --selftest      # what ctest runs

A run that cannot find the game exits 77 (ctest SKIP) and says so.
"""

import argparse
import os
import re
import struct
import sys

TABLE_VA = 0x0068A908
ENTRY = 16
EXPECT = 289

# What this file pins, and why each is not circular.
#
# HANDLERS: only the two confirmed at RUNTIME, by registering an override on
# the address and watching it fire exactly where the shipped scripts say it
# should -- lockControls when tutorial1.py locks the controls at level entry,
# startConversation at both of the tutorial's conversations with the matching
# conversation-manager flag transitions. Pinning a handler this tool itself
# read would only check the tool against itself.
KNOWN_HANDLERS = {
    "lockControls":      0x0049F8C0,
    "startConversation": 0x004A5660,
}

# ARG SPECS: taken from the shipped scripts, which are data this tool never
# reads, so they are an independent check on the field split. Reading the table
# one dword out makes every one of these come back empty.
KNOWN_ARGS = {
    "lockControls":      "f",     # tutorial1.py    lockControls(-1.000)
    "startConversation": "s",     # tutorial1.py    startConversation("act0/...")
    "act":               "aa",    # nightcrawler_spawn.PY  act("spwnr_...", "spwnr_...")
    "remove":            "aa",    # conv_0020b_end.PY      remove("px", "px")
    "playanim":          "sass",  # nightcrawler_walk.PY   playanim("EA_ZONE2", "_OWNER_", "NONE", "")
    "setAIActive":       "as",    # nightcrawler_walk.PY   setAIActive("_OWNER_", "FALSE")
    "cameraFade":        "ff",    # conv_0020b_end.PY      cameraFade(1.000, 0.500)
    "setNoClip":         "as",    # nightcrawler_walk.PY   setNoClip("_OWNER_", "TRUE")
}

# `waittimed` is deliberately absent: it is a VM primitive, not a table
# command, and expecting it here was wrong when this file was first written.
# The selftest caught that, which is the point of pinning anything at all.


def load_game_dir(root):
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

    def string(self, va, limit=64):
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


def read_table(image):
    off = image.offset(TABLE_VA)
    if off is None:
        raise SystemExit("script_commands: 0x%08x is not in this image -- this "
                         "is not the XMen2.exe the port was read against.\n"
                         % TABLE_VA)
    out = []
    while True:
        handler, name, ret, args = struct.unpack_from("<4I", image.data,
                                                      off + len(out) * ENTRY)
        text = image.string(name)
        if text is None or not (0x401000 <= handler < 0x006C0000):
            break
        out.append((text, handler, image.string(ret) or "",
                    image.string(args) or ""))
    return out


def check(table):
    """The size, the runtime-verified handlers, and the script-verified arity."""
    bad = 0
    if len(table) != EXPECT:
        print("script_commands: read %d entries, expected %d"
              % (len(table), EXPECT), file=sys.stderr)
        bad += 1
    by_name = {t[0]: t for t in table}
    for name, handler in KNOWN_HANDLERS.items():
        got = by_name.get(name)
        if got is None or got[1] != handler:
            print("script_commands: %-20s handler %s, EXPECTED 0x%08x"
                  % (name, "absent" if got is None else "0x%08x" % got[1],
                     handler), file=sys.stderr)
            bad += 1
    for name, spec in KNOWN_ARGS.items():
        got = by_name.get(name)
        if got is None or got[3] != spec:
            print("script_commands: %-20s args %r, EXPECTED %r  (the shipped "
                  "scripts call it that way)"
                  % (name, None if got is None else got[3], spec),
                  file=sys.stderr)
            bad += 1
    total = len(KNOWN_HANDLERS) + len(KNOWN_ARGS)
    print("script_commands: %d command(s); %d of %d pinned fact(s) agree"
          % (len(table), total - bad, total))
    return 1 if bad else 0


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__,
             formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", nargs="?", help="print only this command")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    game = load_game_dir(root)
    exe = os.path.join(game, "XMen2.exe") if game else None
    if not exe or not os.path.isfile(exe):
        print("script_commands: SKIPPING -- GAME_PC_DIR is unset or has no "
              "XMen2.exe, so there is no table to read.\n"
              "  This is a skip, not a pass.", file=sys.stderr)
        return 77

    table = read_table(Image(exe))
    if args.selftest:
        return check(table)
    if args.name:
        for name, handler, ret, spec in table:
            if name == args.name:
                print("%-28s fn=0x%08x  ret=%-3s args=%s"
                      % (name, handler, ret, spec))
                return 0
        print("script_commands: no command named %r (%d in the table)"
              % (args.name, len(table)), file=sys.stderr)
        return 1
    for name, handler, ret, spec in table:
        print("%-28s fn=0x%08x  ret=%-3s args=%s" % (name, handler, ret, spec))
    print("# %d command(s)" % len(table))
    return 0


if __name__ == "__main__":
    sys.exit(main())
