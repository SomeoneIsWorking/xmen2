#!/usr/bin/env python3
"""
XMLB -- the binary XML the Alchemy engine ships its data in, read and written.

Both halves matter. Reading answers questions about the shipped data (which
codepoint sits where in a font atlas); writing is what lets this port SHIP
data, and a reader alone would have left the button-glyph work stuck at
"we know what we would change".

## The format, read out of the files rather than assumed

    u32  magic          0x000011b1
    u32  version        1
    ---- root node at offset 8 ----
    u32  name offset    absolute file offset of a NUL-terminated name
    u32  next offset    next SIBLING, or 0xffffffff
    u32  child offset   first CHILD, or 0xffffffff
    u32  attr count
    (u32 key offset, u32 value offset) * attr count

Every offset is absolute, and the string pool is simply whatever the offsets
point at -- there is no pool header. What looks at first like a pool pointer at
offset 8 is the ROOT NODE'S NAME, which happens to be the first string in the
file; `fonts_pc.XMLB` gives it away by naming the root `FONTFILES`.

Values are strings. Numbers are decimal text, which is why the writer takes
strings and does no formatting of its own -- rewriting `0.98046875` as
`0.980469` would silently move a glyph.

## Self-test

`python3 tools/xmlb.py --selftest <file.xmlb>...` parses each file, serialises
it again, and requires the result to be byte-identical. It is wired into the
test suite. A writer that has never been proved to reproduce a shipped file is
a writer that will corrupt one.
"""

import struct
import sys

MAGIC = 0x000011b1
NONE = 0xffffffff


class Node:
    __slots__ = ("name", "attrs", "children")

    def __init__(self, name, attrs=None, children=None):
        self.name = name
        self.attrs = list(attrs or [])          # [(key, value)], order preserved
        self.children = list(children or [])

    def get(self, key, default=None):
        for k, v in self.attrs:
            if k == key:
                return v
        return default

    def set(self, key, value):
        for i, (k, _) in enumerate(self.attrs):
            if k == key:
                self.attrs[i] = (k, value)
                return
        self.attrs.append((key, value))

    def __repr__(self):
        return "Node(%r, %d attrs, %d children)" % (
            self.name, len(self.attrs), len(self.children))


def _cstr(data, off):
    if off == NONE:
        return None
    if off >= len(data):
        raise ValueError("string offset 0x%x is past the end of the file "
                         "(%d bytes)" % (off, len(data)))
    end = data.find(b"\0", off)
    if end < 0:
        raise ValueError("string at 0x%x is unterminated -- the file is "
                         "truncated" % off)
    return data[off:end].decode("latin1")


def parse(data):
    """Bytes -> root Node. Raises ValueError on anything it does not understand;
    it never returns a partial tree, because a partly-read font is worse than
    no font."""
    if len(data) < 24:
        raise ValueError("too short to be XMLB (%d bytes)" % len(data))
    magic, version = struct.unpack_from("<2I", data, 0)
    if magic != MAGIC:
        raise ValueError("bad magic 0x%08x, expected 0x%08x" % (magic, MAGIC))
    if version != 1:
        raise ValueError("unsupported XMLB version %d" % version)

    # Siblings are walked iteratively, reading each node's `next` straight out
    # of the file: a 256-glyph font is one sibling chain, and following it by
    # recursion would be 256 frames deep for no reason. `next` is an encoding
    # detail and deliberately does not survive into the Node type.
    def read(off, depth=0):
        if depth > 64:
            raise ValueError("node nesting deeper than 64 at 0x%x" % off)
        if off + 16 > len(data):
            raise ValueError("node at 0x%x runs past end of file" % off)
        name_off, next_off, child_off, n = struct.unpack_from("<4I", data, off)
        if n > 4096:
            raise ValueError("node at 0x%x claims %d attributes" % (off, n))
        node = Node(_cstr(data, name_off))
        for i in range(n):
            k, v = struct.unpack_from("<2I", data, off + 16 + i * 8)
            node.attrs.append((_cstr(data, k), _cstr(data, v)))
        cur = child_off
        seen = set()
        while cur != NONE:
            if cur in seen:
                raise ValueError("sibling cycle at 0x%x" % cur)
            seen.add(cur)
            node.children.append(read(cur, depth + 1))
            cur = struct.unpack_from("<I", data, cur + 4)[0]
        return node

    return read(8)


def _flatten(root):
    """Nodes in the order the file lays them out: a node, then its subtree,
    then its next sibling. That is what the shipped files do, and matching it
    is what makes the round-trip byte-identical."""
    out = []

    def walk(n):
        out.append(n)
        for c in n.children:
            walk(c)
    walk(root)
    return out


def serialise(root):
    """Root Node -> bytes, laid out the way the shipped files are."""
    nodes = _flatten(root)
    # Node sizes and offsets first; the string pool follows the last node.
    size = {id(n): 16 + 8 * len(n.attrs) for n in nodes}
    off = {}
    p = 8
    for n in nodes:
        off[id(n)] = p
        p += size[id(n)]
    pool_base = p

    # String pool: first use wins an offset, and a repeated string is shared.
    # The shipped files share aggressively (every glyph reuses the same ten
    # attribute names), so not sharing would round-trip wrong AND bloat.
    pool = bytearray()
    where = {}

    def intern(s):
        if s is None:
            return NONE
        if s in where:
            return where[s]
        o = pool_base + len(pool)
        where[s] = o
        pool.extend(s.encode("latin1"))
        pool.append(0)
        return o

    # The root name is interned first because it is the first string in every
    # shipped file (offset 8 points at it).
    intern(root.name)

    parent_of = {}
    for n in nodes:
        for c in n.children:
            parent_of[id(c)] = n

    body = bytearray()
    for n in nodes:
        sibs = parent_of[id(n)].children if id(n) in parent_of else [n]
        i = sibs.index(n)
        nxt = off[id(sibs[i + 1])] if i + 1 < len(sibs) else NONE
        child = off[id(n.children[0])] if n.children else NONE
        body.extend(struct.pack("<4I", intern(n.name), nxt, child, len(n.attrs)))
        for k, v in n.attrs:
            body.extend(struct.pack("<2I", intern(k), intern(v)))

    return struct.pack("<2I", MAGIC, 1) + bytes(body) + bytes(pool)


def _synthetic():
    """A document with every shape the shipped files use -- nested children, a
    long sibling chain, attribute names repeated across siblings (which the
    writer must SHARE, as the real files do), and a node with no attributes.

    This is built rather than copied because the shipped .xmlb files are game
    assets and never enter this repository. It is checked against real files
    too, whenever an install is present -- see `main`.
    """
    root = Node("FONT_TABLE", [("ascender", "14"), ("descender", "4"),
                               ("height", "20"), ("pointsize", "16.38")])
    for i in range(64):
        root.children.append(Node("glyph", [
            ("baseline", "0"), ("height", "0.65"), ("horizadvance", "0.5"),
            ("horizoffset", "0"), ("num", str(i)), ("s", "0.0078125"),
            ("s2", "0.078125"), ("t", "0.78515625"), ("t2", "0.87109375"),
            ("width", "0.46875")]))
    kerning = Node("kerning")
    kerning.children.append(Node("pair", [("a", "65"), ("b", "86")]))
    root.children.append(kerning)
    return root


def _builtin_battery():
    """Checks that MUST produce a positive, so a broken writer cannot pass by
    doing nothing."""
    fails = []

    root = _synthetic()
    try:
        blob = serialise(root)
        back = parse(blob)
    except Exception as e:                          # noqa: BLE001 -- reported
        print("FAIL builtin: the writer's own output could not be read back: "
              "%s" % e)
        print("xmlb selftest: builtin battery 0 of 8 checks passed")
        return False
    if serialise(back) != blob:
        fails.append("synthetic document does not survive parse -> serialise")
    if len(back.children) != len(root.children):
        fails.append("child count changed: %d -> %d"
                     % (len(root.children), len(back.children)))
    if back.children[-1].name != "kerning" or not back.children[-1].children:
        fails.append("nested child lost")

    # Strings must be SHARED. Ten attribute names across 64 glyphs stored once
    # each is the difference between 28 KB and 200 KB, and the shipped files
    # share, so not sharing would also break byte-identity with them.
    if blob.count(b"horizadvance\0") != 1:
        fails.append("attribute names are not shared: 'horizadvance' stored %d "
                     "times" % blob.count(b"horizadvance\0"))

    # A change must MOVE bytes -- otherwise `serialise` could be echoing its
    # input and every round-trip check above would pass regardless.
    moved = _synthetic()
    moved.children[0].set("t", "0.5")
    if serialise(moved) == blob:
        fails.append("changing a glyph produced identical bytes: the writer is "
                     "not building the file")

    # Malformed input must be REFUSED, not half-parsed.
    for bad, why in ((b"", "empty"),
                     (b"\x00" * 32, "bad magic"),
                     (struct.pack("<2I", MAGIC, 7) + b"\0" * 32, "bad version")):
        try:
            parse(bad)
            fails.append("parse accepted %s input" % why)
        except ValueError:
            pass

    for f in fails:
        print("FAIL builtin: %s" % f)
    print("xmlb selftest: builtin battery %d of 8 checks passed"
          % (8 - len(fails)))
    return not fails


def _selftest(paths):
    """The builtin battery always, plus byte-identity against any real files
    given. Passing no files is NOT an error -- the battery is the part that
    travels -- but the count says plainly that no shipped file was checked, so
    "it passed" cannot be mistaken for "it matches the game's own files"."""
    ok_builtin = _builtin_battery()
    if not paths:
        print("xmlb selftest: 0 shipped files round-tripped -- none were given. "
              "Pass .xmlb files from a game install to check byte-identity "
              "against the real format.")
        return 0 if ok_builtin else 1
    ok = 0
    for p in paths:
        data = open(p, "rb").read()
        try:
            root = parse(data)
            again = serialise(root)
        except Exception as e:                      # noqa: BLE001 -- reported
            print("FAIL %s: %s" % (p, e))
            continue
        if again == data:
            ok += 1
            print("ok   %s (%d bytes, %d nodes)"
                  % (p, len(data), len(_flatten(root))))
        else:
            n = sum(1 for a, b in zip(again, data) if a != b)
            print("FAIL %s: round-trip differs, %d of %d bytes (%d vs %d long)"
                  % (p, n + abs(len(again) - len(data)), max(len(again), len(data)),
                     len(again), len(data)))
    print("xmlb selftest: %d of %d shipped files round-tripped byte-identically"
          % (ok, len(paths)))
    return 0 if ok == len(paths) and ok_builtin else 1


def main(argv):
    if len(argv) > 1 and argv[1] == "--selftest":
        return _selftest(argv[2:])
    if len(argv) < 2:
        print(__doc__)
        print("usage: xmlb.py <file.xmlb>            # dump\n"
              "       xmlb.py --selftest <file>...   # round-trip check",
              file=sys.stderr)
        return 2
    root = parse(open(argv[1], "rb").read())

    def dump(n, d=0):
        pad = "  " * d
        print("%s<%s %s>" % (pad, n.name,
                             " ".join('%s="%s"' % kv for kv in n.attrs)))
        for c in n.children:
            dump(c, d + 1)
    dump(root)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
