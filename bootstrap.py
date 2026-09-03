#!/usr/bin/env python3
"""Provision every portable input required by the default native target.

`./run.sh` enters this file through the locked uv environment. A player supplies
only the matching PC install and native system dependencies. Ghidra is a
maintainer tool: committed exports retain boundaries and decoded instructions,
while this initializer restores instruction bytes from the player's PE images.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent


@dataclass(frozen=True)
class SharedRepo:
    name: str
    url: str
    revision: str
    marker: str


SHARED_REPOS = (
    SharedRepo("alchemy", "https://github.com/SomeoneIsWorking/alchemy.git",
               "f95e0931c6ea97f14f02188df641d8ddd9cea227", "src/igb.h"),
    SharedRepo("port-assets", "https://github.com/SomeoneIsWorking/port-assets.git",
               "42a1648ab0414418893b0ebd6eec69fe5b6a97d4", "sets"),
    SharedRepo("android-port", "https://github.com/SomeoneIsWorking/android-port.git",
               "2dc4bcb12483aeae183387e8b46ec5b76a381de2", "tools/android_port.py"),
    # The runtime execution engine (jit-common S040/S047). Pinned to an exact
    # revision, never a branch: a fresh clone that resolved to whatever `main`
    # happened to be would make two machines run different engines while
    # reporting the same port revision.
    #
    # jit-common is listed SEPARATELY rather than left to x86port to fetch,
    # because x86port CONSUMES it and refuses to configure without it. Both are
    # this port's inputs, so both are this port's pins.
    SharedRepo("jit-common", "https://github.com/SomeoneIsWorking/jit-common.git",
               "182643e2ac3f472cd0d1c2abf03acc27f2ce2be2",
               "src/jitcommon/block_cache.h"),
    SharedRepo("x86port", "https://github.com/SomeoneIsWorking/x86port.git",
               "fb2a9ef2887c4d65621a3cc4e1eb5721608c4b2d",
               "src/x86port/engine.h"),
)

GAME_MODULE_SHA256 = {
    "libIGDisplay": "1d64d8cc8022b0c4e44509e84f104ee9d5245375f7f29409ff9fba1acc384847",
    "libIGCore": "1dc4daa739ba2155583b86f62b5a61c821bbb0efab77364748ddb59f7010de0d",
    "libIGSg": "832e64ede714b47116d5cc0885507d24fe3793fbbcfae3d987d66dfd5820f72c",
    "libIGMath": "494bf2513c1f9933dd9d852676af4279301b0d83c77e29fc07eb84e248673350",
    "libIGAttrs": "a3c4768bf2ba08f5de558984ef78c0deada9d1d43e170f6ac9cc23f6cfff120b",
    "libIGGfx": "5546a2801854ddeddb64b21c5f4a8326f28b257f80d981fda3d312a7c06c5cc4",
    "libIGUtils": "afc8f8d15841ff19159ac58628a5b695851b8207528f95342b2b89f17c284442",
    "libIGGui": "a08680a39155885506c801161f133d72a6f69607a35b7896d773940d5fa782b7",
    "libIGLua": "8d5f34b0668721ad3ad26a65acd0969a832e5c3ba3fac79f165ce760ccd253a9",
    "libIGOpt": "3ab1c64a74e4fcbdde5970b374bb13ce242f6440b4203a33c4ff51fcc00a1a31",
    "libMovie": "3ef54b4d5862cedf9adaa542f017988b8e7bee0dae9c22ed4f9a19f1c002da4a",
    "libCriMovie": "9fe36fd67ca4c7b45a30098d84c562debdcccff69e06346a9f64b854b49fd302",
    "libIGAudio": "6a4fcb141e4d4fea7a294d635cabf88bc75c9e70f56c04a985de37971e454e63",
    "libIGCollision": "6b96ed9cb3a47e5052f7c163568243f7961af311d79acb39147dff2b477cea94",
    "libIGInsight": "821ef180ae31f60f8705a32cce744ac7c283da79590147c0a231831bcb256a89",
    "libIGViewer": "0ccca68cca7aba83fd88d41d2985ad21605c06da322ca3410a703c4e2e629391",
    "cg": "0f1d803cb3aecd9c9bfb3caa94a80453510fbb9f1fdd7074856c222b5bc9bcf4",
    "cgD3D8": "0322e926d7f1aa7abb50154bfd32dab2a9e88774c02817eb18976d32474c4b58",
    "msdia80": "643d29919f996ebc74850135a3937583908d49d8ac202bc5267a9c0f9cdf0fee",
    "XMen2": "146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b",
}


def refuse(message: str) -> None:
    raise SystemExit(f"bootstrap: {message}")


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def modules() -> list[str]:
    text = (ROOT / "CMakeLists.txt").read_text()
    match = re.search(r"set\(X2_MODULES(.*?)\)", text, re.DOTALL)
    if not match:
        refuse("CMakeLists.txt has no X2_MODULES list")
    result = match.group(1).split()
    if set(result) != set(GAME_MODULE_SHA256):
        refuse("X2_MODULES and GAME_MODULE_SHA256 disagree")
    return result


def dotenv_value(path: Path, key: str) -> str | None:
    if not path.is_file():
        return None
    pattern = re.compile(rf"\s*(?:export\s+)?{re.escape(key)}=(.*)\s*$")
    for line in path.read_text().splitlines():
        match = pattern.fullmatch(line)
        if not match:
            continue
        value = match.group(1).strip().strip('"').strip("'")
        if value and not value.startswith("/path/to"):
            return value
    return None


def find_child(directory: Path, name: str) -> Path | None:
    wanted = name.casefold()
    matches = [entry for entry in directory.iterdir()
               if entry.is_file() and entry.name.casefold() == wanted]
    if len(matches) > 1:
        refuse(f"{directory} has case-insensitive collisions for {name}: "
               + ", ".join(sorted(entry.name for entry in matches)))
    return matches[0] if matches else None


def module_image(game: Path, module: str) -> Path | None:
    return find_child(game, f"{module}.dll") or find_child(game, f"{module}.exe")


def find_repo_game() -> Path | None:
    """Find one install placed directly inside the repository.

    ``./game`` remains the conventional name.  For a copied retail install,
    also accept XMen2.exe at the repository root or in one immediate child
    directory; going deeper would make build and scratch trees accidental
    search inputs.  Multiple unnamed installs are ambiguous and must be named
    explicitly with GAME_PC_DIR.
    """
    conventional = ROOT / "game"
    if conventional.is_dir() and find_child(conventional, "XMen2.exe"):
        return conventional.resolve()

    candidates: list[Path] = []
    if find_child(ROOT, "XMen2.exe"):
        candidates.append(ROOT)
    for child in sorted(ROOT.iterdir(), key=lambda path: path.name.casefold()):
        if child == conventional or not child.is_dir():
            continue
        if find_child(child, "XMen2.exe"):
            candidates.append(child)
    if len(candidates) > 1:
        refuse("multiple repository-local PC installs contain XMen2.exe: "
               + ", ".join(str(path) for path in candidates)
               + "; set GAME_PC_DIR to select one")
    return candidates[0].resolve() if candidates else None


def find_game() -> Path:
    dot_env = ROOT / ".env"
    if not dot_env.exists() and (ROOT / ".env.example").is_file():
        shutil.copyfile(ROOT / ".env.example", dot_env)
        print("bootstrap: created .env from .env.example")
    value = (os.environ.get("GAME_PC_DIR") or dotenv_value(dot_env, "GAME_PC_DIR")
             or find_repo_game())
    if not value:
        refuse("no PC install found. Put XMen2.exe in the repository root or "
               "one directory below it, or set GAME_PC_DIR in .env")
    game = Path(value).expanduser().resolve()
    if not game.is_dir() or find_child(game, "XMen2.exe") is None:
        refuse(f"GAME_PC_DIR={game} is not an X-Men Legends II PC install")
    os.environ["GAME_PC_DIR"] = str(game)
    return game


def validate_game(game: Path) -> dict[str, Path]:
    images: dict[str, Path] = {}
    missing: list[str] = []
    wrong: list[str] = []
    for module in modules():
        image = module_image(game, module)
        if image is None:
            missing.append(module)
            continue
        images[module] = image
        actual = hash_file(image)
        if actual != GAME_MODULE_SHA256[module]:
            wrong.append(f"{module}={actual}")
    if missing:
        refuse(f"{game} is missing {len(missing)} required module(s): "
               + ", ".join(missing))
    if wrong:
        refuse("the install differs from the build used for the committed exports: "
               + ", ".join(wrong))
    print(f"bootstrap: game identity OK ({len(images)} PE image(s))")
    return images


def run_git(arguments: list[str], cwd: Path | None = None) -> str:
    try:
        result = subprocess.run(["git", *arguments], cwd=cwd, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except OSError as error:
        refuse(f"git is required but could not run: {error}")
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic"
        refuse(f"git {' '.join(arguments)} failed: {detail}")
    return result.stdout.strip()


def canonical_url(value: str) -> str:
    value = value.strip().removesuffix(".git").rstrip("/")
    for prefix in ("https://github.com/", "http://github.com/",
                   "ssh://git@github.com/", "git@github.com:"):
        if value.startswith(prefix):
            return "github.com/" + value[len(prefix):].casefold()
    return value


def shared_target(repo: SharedRepo) -> Path:
    exact = os.environ.get(f"{repo.name.replace('-', '_').upper()}_DIR")
    if exact:
        return Path(exact).expanduser().resolve()
    shared = os.environ.get("SHARED_DIR")
    if shared:
        return (Path(shared).expanduser() / repo.name).resolve()
    return (ROOT / "vendor/shared" / repo.name).resolve()


def validate_checkout(repo: SharedRepo, target: Path) -> None:
    if not (target / ".git").is_dir():
        refuse(f"{target} exists but is not a git checkout")
    origin = run_git(["remote", "get-url", "origin"], target)
    if canonical_url(origin) != canonical_url(repo.url):
        refuse(f"{target} has origin {origin}, expected {repo.url}; refusing to mutate it")
    dirty = run_git(["status", "--porcelain"], target)
    head = run_git(["rev-parse", "HEAD"], target)
    if dirty:
        refuse(f"{target} has local changes; preserve them before provisioning")
    if head != repo.revision:
        refuse(f"{target} is at {head}, but this port requires {repo.revision}; "
               "move it aside or configure a different checkout")
    if not (target / repo.marker).exists():
        refuse(f"{target} is pinned but missing required marker {repo.marker}")
    uninitialized = [line.split()[1]
                     for line in run_git(["submodule", "status", "--recursive"],
                                         target).splitlines()
                     if line.startswith("-")]
    if uninitialized:
        refuse(f"{target} is at the pin but its submodules are not checked out: "
               + ", ".join(uninitialized)
               + "; run `git submodule update --init --recursive` there")


def clone_repo(repo: SharedRepo, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{repo.name}-", dir=target.parent) as raw:
        staged = Path(raw) / repo.name
        print(f"bootstrap: cloning {repo.url} at {repo.revision}")
        run_git(["clone", "--no-checkout", repo.url, str(staged)])
        run_git(["checkout", "--detach", repo.revision], staged)
        # A pinned repository's submodules are part of that pin. Without this a
        # cold clone validates clean and then fails in CMake with a directory
        # that "does not contain a CMakeLists.txt file" -- x86port's Zydis.
        run_git(["submodule", "update", "--init", "--recursive"], staged)
        validate_checkout(repo, staged)
        if target.exists():
            refuse(f"{target} appeared during provisioning; refusing to overwrite it")
        staged.replace(target)


def ensure_shared() -> None:
    cloned = 0
    for repo in SHARED_REPOS:
        target = shared_target(repo)
        if target.exists():
            validate_checkout(repo, target)
        else:
            clone_repo(repo, target)
            cloned += 1
        os.environ[f"{repo.name.replace('-', '_').upper()}_DIR"] = str(target)
    print(f"bootstrap: shared repositories OK ({cloned} cloned, "
          f"{len(SHARED_REPOS) - cloned} already pinned)")


def run_tool(arguments: list[str], purpose: str, capture: bool = False) -> str:
    result = subprocess.run([sys.executable, *arguments], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE if capture else None)
    if result.returncode:
        refuse(f"{purpose} failed with exit {result.returncode}: {' '.join(arguments)}")
    return result.stdout if capture else ""


def publish_text(path: Path, content: str) -> bool:
    if path.is_file():
        try:
            if path.read_text() == content:
                return False
        except (OSError, UnicodeError):
            # A damaged text cache is a miss. Replacing it is the recovery path.
            pass
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
                "w", encoding="utf-8", dir=path.parent,
                prefix=f".{path.name}-", delete=False) as temporary:
            temporary_path = Path(temporary.name)
            temporary.write(content)
            temporary.flush()
            os.fsync(temporary.fileno())
        temporary_path.replace(path)
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()
    return True


def publish_font_tier_ratio(game: Path) -> None:
    """Measure this install's own PC->HD font step into a generated header.

    The port's AUTO text scale divides that step back out, so the number has
    to come from the fonts the player has rather than from a constant typed
    into the C -- each localisation ships its own.
    """
    run_tool([str(ROOT / "tools/font_tier_ratio.py"), str(game),
              str(ROOT / "src/gen/font_tier_ratio.h")],
             "measuring the font tier step")


def publish_prompt_glyph_atlas() -> None:
    """Rasterise the shared prompt SVGs into the port's own atlas header.

    The renderer-side prompt feature draws its glyphs from pixels this port
    generates at build time -- never from a patched game font. Regenerated
    every provision so a shared-set update cannot leave a stale atlas
    committed in the tree looking current.
    """
    run_tool([str(ROOT / "tools/render_prompt_glyphs.py"),
              str(ROOT / "src/gen/prompt_glyph_atlas.h")],
             "rasterising the prompt glyph atlas")


def provision(game: Path) -> None:
    """Publish the portable inputs the launcher's build needs.

    There is no code-generation step: the guest's own instruction bytes are
    read out of the player's images at run time and executed by the engine, so
    the only things published here are derived assets.
    """
    publish_font_tier_ratio(game)
    publish_prompt_glyph_atlas()
    print("bootstrap: native inputs ready")


def initialize() -> None:
    os.chdir(ROOT)
    game = find_game()
    validate_game(game)
    ensure_shared()
    provision(game)


def main() -> int:
    if len(sys.argv) != 1:
        refuse("takes no arguments; project tools are separate from run.sh")
    initialize()
    environment = dict(os.environ)
    environment["PATH"] = (str(Path(sys.executable).parent) + os.pathsep
                           + environment.get("PATH", ""))
    if sys.prefix != sys.base_prefix:
        environment["VIRTUAL_ENV"] = sys.prefix
    print("bootstrap: handing over to tools/run.py")
    sys.stdout.flush()
    os.execve(sys.executable,
              [sys.executable, str(ROOT / "tools/run.py")], environment)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
