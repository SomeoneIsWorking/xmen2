#!/usr/bin/env python3
"""List or extract the length-prefixed members of an Alchemy .fb bundle."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import struct


NAME_BYTES = 128
POOL_BYTES = 64
HEADER_BYTES = NAME_BYTES + POOL_BYTES + 4


@dataclass(frozen=True)
class Member:
    name: str
    pool: str
    offset: int
    size: int


def _text(field: bytes, label: str, offset: int) -> str:
    raw = field.split(b"\0", 1)[0]
    try:
        return raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ValueError(f"record at {offset:#x} has non-ASCII {label}") from exc


def parse(data: bytes) -> list[Member]:
    members: list[Member] = []
    pos = 0
    while pos < len(data):
        remaining = len(data) - pos
        if remaining < HEADER_BYTES:
            raise ValueError(
                f"record {len(members)} at {pos:#x} has only {remaining} "
                f"header byte(s), needs {HEADER_BYTES}"
            )
        name = _text(data[pos : pos + NAME_BYTES], "name", pos)
        pool = _text(data[pos + NAME_BYTES : pos + NAME_BYTES + POOL_BYTES], "pool", pos)
        if not name:
            raise ValueError(f"record {len(members)} at {pos:#x} has an empty name")
        size = struct.unpack_from("<I", data, pos + NAME_BYTES + POOL_BYTES)[0]
        payload = pos + HEADER_BYTES
        end = payload + size
        if end > len(data):
            raise ValueError(
                f"record {len(members)} {name!r} declares {size} payload "
                f"byte(s), but only {len(data) - payload} remain"
            )
        members.append(Member(name, pool, payload, size))
        pos = end
    return members


def safe_member_path(name: str) -> PurePosixPath:
    path = PurePosixPath(name.replace("\\", "/"))
    if path.is_absolute() or not path.parts or any(part in ("", ".", "..") for part in path.parts):
        raise ValueError(f"unsafe bundle member path {name!r}")
    return path


def _record(name: str, pool: str, payload: bytes) -> bytes:
    def field(value: str, size: int) -> bytes:
        encoded = value.encode("ascii")
        if len(encoded) >= size:
            raise ValueError("self-test field is too long")
        return encoded + bytes(size - len(encoded))

    return field(name, NAME_BYTES) + field(pool, POOL_BYTES) + struct.pack("<I", len(payload)) + payload


def selftest() -> int:
    data = _record("ui/one.igb", "model", b"abc") + _record("data/two.engb", "data", b"12345")
    members = parse(data)
    checks = [
        len(members) == 2,
        members[0] == Member("ui/one.igb", "model", HEADER_BYTES, 3),
        data[members[1].offset : members[1].offset + members[1].size] == b"12345",
        str(safe_member_path("ui/one.igb")) == "ui/one.igb",
    ]
    try:
        parse(data[:-1])
        checks.append(False)
    except ValueError:
        checks.append(True)
    try:
        safe_member_path("../escape")
        checks.append(False)
    except ValueError:
        checks.append(True)
    passed = sum(checks)
    print(f"extract_fb selftest: {passed}/{len(checks)} checks passed over 2 synthetic members")
    return 0 if passed == len(checks) else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    sub = parser.add_subparsers(dest="command")
    listing = sub.add_parser("list")
    listing.add_argument("bundle", type=Path)
    extracting = sub.add_parser("extract")
    extracting.add_argument("bundle", type=Path)
    extracting.add_argument("outdir", type=Path)
    extracting.add_argument("--contains", action="append", default=[])
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.command:
        parser.error("choose list or extract; inspected 0 bundles")

    data = args.bundle.read_bytes()
    members = parse(data)
    if args.command == "list":
        for member in members:
            print(f"{member.offset:08x} {member.size:8d} {member.pool:8s} {member.name}")
        print(f"listed {len(members)} member(s) over {len(data)} bundle bytes")
        return 0

    needles = [value.lower() for value in args.contains]
    selected = [m for m in members if not needles or any(n in m.name.lower() for n in needles)]
    if not selected:
        parser.error(
            f"scanned {len(members)} members, matched 0 for {args.contains!r}; wrote NOTHING"
        )
    for member in selected:
        target = args.outdir.joinpath(*safe_member_path(member.name).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data[member.offset : member.offset + member.size])
        print(f"{member.size:8d} {member.name}")
    print(f"extracted {len(selected)} of {len(members)} member(s) to {args.outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
