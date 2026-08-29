#!/usr/bin/env python3
"""Fetch the maintained Xbox recompilation fork used by the optional Xbox path."""

from __future__ import annotations

import subprocess
from pathlib import Path


def run_git(repository: Path, *arguments: str) -> None:
    subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=True,
    )


def read_lock(lock_path: Path) -> tuple[str, str]:
    values = {}
    for line in lock_path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator or not value:
            raise ValueError(f"invalid xboxrecomp lock line: {line!r}")
        values[key] = value
    try:
        return values["repository"], values["commit"]
    except KeyError as error:
        raise ValueError(f"xboxrecomp lock is missing {error.args[0]}") from error


def main() -> int:
    repository_root = Path(__file__).resolve().parents[1]
    destination = repository_root / "vendor" / "xboxrecomp"
    if destination.exists():
        print(f"{destination} already exists; refusing to replace it")
        return 0

    repository_url, repository_ref = read_lock(repository_root / "xbox" / "xboxrecomp.lock")
    destination.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["git", "clone", repository_url, str(destination)],
        check=True,
    )
    run_git(destination, "checkout", "--detach", repository_ref)
    print(f"cloned {repository_url} at {repository_ref}")
    print("The Xbox toolkit changes are maintained in that fork; no project patch queue is needed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
