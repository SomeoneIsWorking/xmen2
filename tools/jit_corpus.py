#!/usr/bin/env python3
"""Emit x86port's function corpus for this title, from the player's own exe.

WHY THIS EXISTS. x86port's `tools/jit_coverage.c` answers the questions a
generated test suite cannot -- how much of a SHIPPED binary the translator
covers, what stops it, and how its conditions are lowered over real code -- and
it reads one line per function:

    <hex entry address>TAB<hex body bytes>

Nothing produced that. The corpus was being reconstructed by hand each time it
was wanted, which is how a measurement quietly ends up over a different set of
functions than the one it is compared against. This is the one producer: the
addresses and sizes come from the recovered function inventory, the bytes come
from the player's own executable, and neither is committed.

THE NEGATIVE IS DESIGNED. A run that read no inventory, one whose image holds
none of the inventory's addresses, and one that emitted a handful of functions
are three different failures and none of them is a corpus. Each refuses by name
rather than printing a short file that would be measured as though it were the
title.
"""

from __future__ import annotations

import argparse
import csv
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "vendor/shared/x86port/tools"))

import pe as pe_module  # noqa: E402  (after the path is set up)
from script_commands import load_game_dir  # noqa: E402

# Below this the file is not this title, whatever it is. The recovered
# inventory holds over sixteen thousand functions; a few hundred means the
# inventory or the image was wrong, not that the game is small.
MINIMUM_FUNCTIONS = 1000


class Image:
    """The player's executable, addressed the way the inventory addresses it."""

    def __init__(self, path: pathlib.Path):
        self._pe = pe_module.PE(str(path))
        self.base = self._pe.image_base
        self.path = path

    def body(self, virtual_address: int, size: int) -> bytes | None:
        """The bytes at a virtual address, truncated at the end of the section
        that holds them. None when no section does, or when the address is past
        the section's RAW data -- uninitialised space holds no code to read."""
        rva = virtual_address - self.base
        for section in self._pe.sections:
            span = max(section["vsize"], section["rsize"])
            if not section["vaddr"] <= rva < section["vaddr"] + span:
                continue
            offset = rva - section["vaddr"]
            available = min(size, max(0, section["rsize"] - offset))
            if available <= 0:
                return None
            start = section["raddr"] + offset
            return self._pe.data[start : start + available]
        return None


def functions(path: pathlib.Path):
    """Yield (virtual address, size) from the recovered inventory."""
    seen = 0
    with path.open() as handle:
        for row in csv.DictReader(handle):
            if "vaddr" not in row or "size" not in row:
                raise ValueError(
                    f"{path}: no vaddr/size columns -- not a function inventory?"
                )
            size = int(row["size"])
            if size <= 0:
                continue
            seen += 1
            yield int(row["vaddr"], 16), size
    if seen == 0:
        raise ValueError(f"{path}: holds no function with a size")


def write_corpus(image: Image, inventory: pathlib.Path, out: pathlib.Path) -> int:
    emitted = unreadable = 0
    with out.open("w") as handle:
        for virtual_address, size in functions(inventory):
            body = image.body(virtual_address, size)
            if not body:
                unreadable += 1
                continue
            handle.write("%08x\t%s\n" % (virtual_address, body.hex()))
            emitted += 1
    print(
        "jit_corpus: %d function(s) from %s, %d whose bytes the image does not hold"
        % (emitted, image.path.name, unreadable),
        file=sys.stderr,
    )
    return emitted


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--exe",
        help="the executable to read bytes from; default XMen2.exe in GAME_PC_DIR",
    )
    parser.add_argument(
        "--functions",
        default=str(ROOT / "build/decomp/functions.csv"),
        help="the recovered function inventory (vaddr,size,name)",
    )
    parser.add_argument("--out", required=True, help="where to write the corpus")
    args = parser.parse_args(argv)

    exe = args.exe
    if not exe:
        game_dir = load_game_dir(str(ROOT))
        if not game_dir:
            return fail(
                "no --exe and no GAME_PC_DIR in the environment or .env, so "
                "there is no executable to read"
            )
        exe = str(pathlib.Path(game_dir) / "XMen2.exe")

    exe_path = pathlib.Path(exe)
    if not exe_path.is_file():
        return fail(f"no executable at {exe_path}")
    inventory = pathlib.Path(args.functions)
    if not inventory.is_file():
        return fail(
            f"no function inventory at {inventory}; the decompilation export "
            "produces it"
        )

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    try:
        emitted = write_corpus(Image(exe_path), inventory, out)
    except ValueError as error:
        return fail(str(error))
    if emitted < MINIMUM_FUNCTIONS:
        return fail(
            "%d function(s) is not a corpus of this title; %s and %s do not "
            "describe the same image"
            % (emitted, exe_path.name, inventory.name)
        )
    print("jit_corpus: %s" % out)
    return 0


def fail(message: str) -> int:
    print("jit_corpus: REFUSED: %s" % message, file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
