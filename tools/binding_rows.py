#!/usr/bin/env python3
"""Re-read the game's binding rows and cutscene-skip actions, and diff the port.

`src/input/binding_rows.c` ships each executable persistence key beside its PC
English display label. Constants read from shipped data have to be checked
against that data BY CODE: "SreenGrab" is a typo the registry ABI depends on,
while showing that key instead of igct.bnx's "Screenshot" was a UI defect.

The names are `MOV dword ptr [ESP+disp], <string>` immediates inside
FUN_0061b030, the function that fills the keyboard defaults and formats each
row into `Controls\\Player%d\\<name>1`. Ordering is by ESP displacement, which
is the row order.

The executable also owns both retail cutscene-skip routes. Its keyboard
defaults bind row 17 (Pause) to DIK_ESCAPE, and FUN_00619c40 maps both the FMV
menu's action 19 and scripted cinematics' action 20 to that row. Those facts
are decoded from the machine code here rather than copied into a test-only
implementation.

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
KEYBOARD_DEFAULTS_DISP = 0x14
PAUSE_ROW = 17
DIK_ESCAPE_BINDING = 0x00010001
ACTION_MAP_START = 0x00619C40
ACTION_TABLE = 0x00619D54
ACTIONS = 0x34
FMV_SKIP_ACTION = 19
CINEMATIC_SKIP_ACTION = 20


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


def esp_immediate_stores(body):
    """Yield (ESP displacement, immediate) for MOV [ESP+disp], imm32."""
    i = 0
    while i < len(body) - 6:
        if body[i:i + 3] == b"\xC7\x44\x24":
            disp = body[i + 3]
            imm = struct.unpack_from("<I", body, i + 4)[0]
            width = 8
        elif body[i:i + 3] == b"\xC7\x84\x24":
            disp = struct.unpack_from("<I", body, i + 3)[0]
            imm = struct.unpack_from("<I", body, i + 7)[0]
            width = 11
        else:
            i += 1
            continue
        yield disp, imm
        i += width


def binding_function_body(image):
    start = image.offset(FN_START)
    if start is None:
        raise SystemExit("binding_rows: 0x%08x is not in this image -- this is "
                         "not the XMen2.exe the port was read against.\n"
                         % FN_START)
    return image.data[start:start + FN_BYTES]


def names_from_exe(image):
    """The row names, in ESP-displacement order, plus what was scanned."""
    body = binding_function_body(image)
    found, scanned = {}, 0
    for disp, imm in esp_immediate_stores(body):
        scanned += 1
        text = image.string(imm)
        if text:
            found[disp] = text
    ordered = [found[d] for d in sorted(found)]
    return ordered, scanned


def keyboard_defaults_from_exe(image):
    """Decode the first kind/code value initialized for each binding row."""
    found = {}
    for disp, imm in esp_immediate_stores(binding_function_body(image)):
        if imm >> 16 == 1:
            found.setdefault(disp, imm)
    return [found.get(KEYBOARD_DEFAULTS_DISP + row * 4) for row in range(ROWS)]


def action_rows_from_exe(image):
    """Decode FUN_00619c40's action jump table through its return stubs."""
    start = image.offset(ACTION_MAP_START)
    table = image.offset(ACTION_TABLE)
    if start is None or table is None:
        raise SystemExit("binding_rows: the action map/table is outside this "
                         "image -- this is not the expected XMen2.exe.")
    prefix = image.data[start:start + 20]
    expected = bytes.fromhex("8b44240483f8330f8700010000ff2485")
    if (prefix[:16] != expected or
            struct.unpack_from("<I", prefix, 16)[0] != ACTION_TABLE):
        raise SystemExit("binding_rows: FUN_00619c40 no longer has the "
                         "measured 52-way jump-table shape; refusing to guess "
                         "its actions.")
    rows = []
    for action in range(ACTIONS):
        target = struct.unpack_from("<I", image.data, table + action * 4)[0]
        off = image.offset(target)
        if off is None:
            raise SystemExit("binding_rows: action %d targets 0x%08x outside "
                             "the image." % (action, target))
        code = image.data[off:off + 6]
        if code[:1] == b"\xB8" and code[5:6] == b"\xC3":
            rows.append(struct.unpack_from("<I", code, 1)[0])
        elif code[:3] == b"\x33\xC0\xC3":
            rows.append(0)
        elif code[:4] == b"\x83\xC8\xFF\xC3":
            rows.append(0xFFFFFFFF)
        else:
            raise SystemExit("binding_rows: action %d target 0x%08x is not "
                             "a recognized return stub; refusing to guess."
                             % (action, target))
    return rows


def rows_from_c(path):
    text = open(path).read()
    m = re.search(r"ROWS\[INPUT_BINDING_ROWS\]\s*=\s*\{(.*?)\};",
                  text, re.S)
    if not m:
        raise SystemExit("binding_rows: no ROWS descriptor table in %s -- the port "
                         "moved it, and this check has to move with it.\n"
                         % path)
    return re.findall(r'\{"([^"]*)",\s*"([^"]*)"\}', m.group(1))


def labels_from_igct(path):
    labels = {}
    with open(path, encoding="latin-1") as source:
        for line in source:
            line = line.rstrip("\r\n")
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            if key in labels:
                raise SystemExit("binding_rows: duplicate localization key %r "
                                 "in %s" % (key, path))
            labels[key] = value
    return labels


def report(image_path, localization_path, c_path):
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

    keyboard = keyboard_defaults_from_exe(image)
    missing_defaults = sum(value is None for value in keyboard)
    if missing_defaults:
        print("binding_rows: recovered %d of %d keyboard defaults. REFUSING "
              "to infer Pause from a partial read."
              % (ROWS - missing_defaults, ROWS), file=sys.stderr)
        return 1
    pause_ok = keyboard[PAUSE_ROW] == DIK_ESCAPE_BINDING
    print("binding_rows: Pause row %d defaults to kind/code 0x%08x%s"
          % (PAUSE_ROW, keyboard[PAUSE_ROW],
             " (DIK_ESCAPE)" if pause_ok else " (expected DIK_ESCAPE)"))

    actions = action_rows_from_exe(image)
    skip_actions = (FMV_SKIP_ACTION, CINEMATIC_SKIP_ACTION)
    bad_skip_actions = [action for action in skip_actions
                        if actions[action] != PAUSE_ROW]
    print("binding_rows: %d of %d retail cutscene skip actions map to Pause "
          "row %d (FMV action %d, scripted cinematic action %d)"
          % (len(skip_actions) - len(bad_skip_actions), len(skip_actions),
             PAUSE_ROW, FMV_SKIP_ACTION, CINEMATIC_SKIP_ACTION))

    if c_path is None:
        for i, n in enumerate(names):
            print("  row %2d  %s" % (i, n))
        return 0 if pause_ok and not bad_skip_actions else 1
    shipped = rows_from_c(c_path)
    if len(shipped) != ROWS:
        print("binding_rows: %s ships %d descriptors, not %d."
              % (c_path, len(shipped), ROWS), file=sys.stderr)
        return 1
    shipped_keys = [key for key, _ in shipped]
    bad = [(i, a, b)
           for i, (a, b) in enumerate(zip(names, shipped_keys, strict=True))
           if a != b]
    for i, a, b in bad:
        print("  row %2d: exe says %r, the port ships %r" % (i, a, b),
              file=sys.stderr)
    print("binding_rows: %d of %d storage keys agree with the exe"
          % (ROWS - len(bad), ROWS))
    localized = labels_from_igct(localization_path)
    bad_labels = [(i, key, label, localized.get(key))
                  for i, (key, label) in enumerate(shipped)
                  if localized.get(key) != label]
    for i, key, label, expected in bad_labels:
        print("  row %2d %s: igct.bnx says %r, the port ships %r"
              % (i, key, expected, label), file=sys.stderr)
    print("binding_rows: %d of %d display labels agree with igct.bnx"
          % (ROWS - len(bad_labels), ROWS))
    if not pause_ok:
        print("  Pause row %d: exe default is 0x%08x, expected keyboard "
              "DIK_ESCAPE (0x%08x)"
              % (PAUSE_ROW, keyboard[PAUSE_ROW], DIK_ESCAPE_BINDING),
              file=sys.stderr)
    for action in bad_skip_actions:
        print("  cutscene skip action %d maps to row %d, expected Pause row %d"
              % (action, actions[action], PAUSE_ROW), file=sys.stderr)
    return 1 if (bad or bad_labels or not pause_ok or
                 bad_skip_actions) else 0


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
        check = os.path.join(root, "src", "input", "binding_rows.c")

    game = load_env(root)
    exe = os.path.join(game, "XMen2.exe") if game else None
    localization = os.path.join(game, "igct.bnx") if game else None
    if (not exe or not os.path.isfile(exe) or not localization or
            not os.path.isfile(localization)):
        print("binding_rows: SKIPPING -- GAME_PC_DIR is unset or has no "
              "XMen2.exe/igct.bnx, so there is nothing to read the row "
              "metadata OUT of.\n"
              "  This is a skip, not a pass: the shipped table was not "
              "checked against anything.", file=sys.stderr)
        return 77
    return report(exe, localization, check)


if __name__ == "__main__":
    sys.exit(main())
