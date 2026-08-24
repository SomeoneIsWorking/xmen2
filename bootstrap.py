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
SPLIT = 1500


@dataclass(frozen=True)
class SharedRepo:
    name: str
    url: str
    revision: str
    marker: str


SHARED_REPOS = (
    SharedRepo("alchemy", "https://github.com/SomeoneIsWorking/alchemy.git",
               "1a171443512085fa7401371839a92c406ed42f07", "src/igb.h"),
    SharedRepo("port-assets", "https://github.com/SomeoneIsWorking/port-assets.git",
               "8282d4c7d19ef3a625866524092c1d45ec080110", "sets"),
    SharedRepo("recomp-x86", "https://github.com/SomeoneIsWorking/recomp-x86.git",
               "1efa0e60a084af0b2b57a274070bdc67179a0ef1", "tools/recomp.py"),
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


def hash_files(paths: tuple[Path, ...]) -> str:
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.name.encode())
        digest.update(bytes.fromhex(hash_file(path)))
    return digest.hexdigest()


def has_instruction_encodings(path: Path) -> bool:
    """Check canonical captured JSON without loading a multi-megabyte export."""
    needle = b'"b":'
    overlap = b""
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            haystack = overlap + block
            if needle in haystack:
                return True
            overlap = haystack[-(len(needle) - 1):]
    return False


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


def find_game() -> Path:
    dot_env = ROOT / ".env"
    if not dot_env.exists() and (ROOT / ".env.example").is_file():
        shutil.copyfile(ROOT / ".env.example", dot_env)
        print("bootstrap: created .env from .env.example")
    beside = ROOT / "game"
    value = (os.environ.get("GAME_PC_DIR") or dotenv_value(dot_env, "GAME_PC_DIR")
             or (str(beside.resolve()) if beside.is_dir() else None))
    if not value:
        refuse("no PC install found. Drop it at ./game/ or set GAME_PC_DIR in .env")
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


def clone_repo(repo: SharedRepo, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{repo.name}-", dir=target.parent) as raw:
        staged = Path(raw) / repo.name
        print(f"bootstrap: cloning {repo.url} at {repo.revision}")
        run_git(["clone", "--no-checkout", repo.url, str(staged)])
        run_git(["checkout", "--detach", repo.revision], staged)
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
    if path.is_file() and path.read_text() == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent,
                                     prefix=f".{path.name}-", delete=False) as temporary:
        temporary_path = Path(temporary.name)
        temporary.write(content)
        temporary.flush()
        os.fsync(temporary.fileno())
    temporary_path.replace(path)
    return True


def read_stamp(path: Path) -> dict[str, object] | None:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def write_stamp(path: Path, value: dict[str, object]) -> None:
    publish_text(path, json.dumps(value, sort_keys=True) + "\n")


def restore_exports(images: dict[str, Path]) -> dict[str, dict[str, object]]:
    scratch = ROOT / "scratch/recomp"
    scratch.mkdir(parents=True, exist_ok=True)
    states: dict[str, dict[str, object]] = {}
    recomp_repo = next(repo for repo in SHARED_REPOS if repo.name == "recomp-x86")
    recomp_root = shared_target(recomp_repo)
    reinject_tool = recomp_root / "tools/reinject_bytes.py"
    pe_tool = recomp_root / "tools/pe.py"
    tool_hash = hash_files((reinject_tool, pe_tool))
    for module in modules():
        frozen = ROOT / "re/ghidra" / f"{module}.json"
        live = scratch / f"{module}.json"
        if not frozen.is_file():
            refuse(f"missing committed export {frozen}; Ghidra is not a player prerequisite")
        if has_instruction_encodings(frozen):
            refuse(f"{frozen} contains instruction encodings; committed exports must be metadata only")
        source_hash = hash_file(frozen)
        image_hash = GAME_MODULE_SHA256[module]
        stamp_path = scratch / f".{module}.reinject.json"
        expected = {"schema": 1, "source": source_hash, "image": image_hash,
                    "tool": tool_hash}
        prior = read_stamp(stamp_path)
        live_hash = hash_file(live) if live.is_file() else None
        if not (live_hash and prior == {**expected, "output": live_hash}):
            shutil.copyfile(frozen, live)
            run_tool([str(ROOT / "tools/reinject_bytes.py"), str(live),
                      str(images[module])], f"restoring {module} instruction bytes")
            live_hash = hash_file(live)
            write_stamp(stamp_path, {**expected, "output": live_hash})

        iat = scratch / f"{module}.iat"
        iat_stamp = scratch / f".{module}.iat.json"
        iat_expected = {"schema": 1, "image": image_hash,
                        "tool": hash_file(pe_tool)}
        iat_prior = read_stamp(iat_stamp)
        iat_hash = hash_file(iat) if iat.is_file() else None
        if not (iat_hash and iat_prior == {**iat_expected, "output": iat_hash}):
            iat_text = run_tool([str(ROOT / "tools/pe.py"), "iat", str(images[module])],
                                f"deriving {module} import table", capture=True)
            if not iat_text.strip():
                refuse(f"PE parser produced an empty import table for {module}")
            publish_text(iat, iat_text)
            iat_hash = hash_file(iat)
            write_stamp(iat_stamp, {**iat_expected, "output": iat_hash})
        states[module] = {"json": live_hash, "iat": iat_hash}
    return states


def generated_bodies(module: str) -> list[Path]:
    single = ROOT / "src/recomp" / f"{module}.c"
    chunks = sorted((ROOT / "src/recomp").glob(f"{module}_[0-9][0-9][0-9].c"))
    return [single] if single.is_file() else chunks


def emit_modules(states: dict[str, dict[str, object]]) -> None:
    recomp_revision = next(repo.revision for repo in SHARED_REPOS
                           if repo.name == "recomp-x86")
    for module in modules():
        isolate = ROOT / "scratch/recomp" / f"{module}.isolate"
        provenance = {"schema": 1, "split": SPLIT, "recomp": recomp_revision,
                      **states[module],
                      "isolate": hash_file(isolate) if isolate.is_file() else None}
        stamp = ROOT / "scratch/recomp" / f".{module}.emit.json"
        native = ROOT / "src/recomp" / f"{module}_native.c"
        if read_stamp(stamp) == provenance and native.is_file() and generated_bodies(module):
            continue
        for old in (ROOT / "src/recomp").glob(f"{module}_[0-9][0-9][0-9].c"):
            old.unlink()
        (ROOT / "src/recomp" / f"{module}.c").unlink(missing_ok=True)
        live = ROOT / "scratch/recomp" / f"{module}.json"
        print(f"bootstrap: emitting {module}")
        run_tool([str(ROOT / "tools/recomp.py"), "emit", str(live),
                  str(ROOT / "src/recomp" / f"{module}.c"), "--split", str(SPLIT)],
                 f"emitting {module} bodies")
        run_tool([str(ROOT / "tools/recomp.py"), "native", str(live), str(native)],
                 f"emitting {module} dispatch")
        if not native.is_file() or not generated_bodies(module):
            refuse(f"translator reported success but emitted no complete {module} output")
        write_stamp(stamp, provenance)


def provision(images: dict[str, Path]) -> None:
    states = restore_exports(images)
    run_tool([str(ROOT / "tools/gen_probes.py")], "generating oracle probe wiring")
    emit_modules(states)
    run_tool([str(ROOT / "tools/check_emitted.py"), "--root", str(ROOT)],
             "verifying emitted code provenance")
    print(f"bootstrap: native inputs ready ({len(states)} module(s), Ghidra not used)")


def initialize() -> None:
    os.chdir(ROOT)
    game = find_game()
    images = validate_game(game)
    ensure_shared()
    provision(images)


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
