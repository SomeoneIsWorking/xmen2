#!/usr/bin/env python3
"""Verify shipping config/logger ownership and retired-tool absence."""

from __future__ import annotations

import argparse
from pathlib import Path
import tempfile

from x2_source_policy import (
    CONFIG_OWNER,
    first_party_text,
    retired_path_violations,
    shipping_sources,
    source_violations,
    text_violations,
)


def selftest() -> int:
    cmake = """set(X2_NATIVE_SOURCES
    src/native/main.c
)
add_library(x2_runtime_services STATIC
    src/config/environment.c
    src/native/x2_log.c
)
add_library(x2_rmlui_ui STATIC
    src/ui/settings.cpp
)
"""
    parsed = shipping_sources(cmake)
    expected = {
        "src/native/main.c",
        "src/config/environment.c",
        "src/native/x2_log.c",
        "src/ui/settings.cpp",
    }
    if set(parsed) != expected:
        print(f"verify_source_policy selftest: product source closure was {parsed}")
        return 1
    clean = {
        CONFIG_OWNER: (
            'const char *read_env(void) { return getenv("X"); }\n'
            'int write_env(void) { return setenv("X", "1", 1); }\n'
        ),
        "src/native/main.c": (
            '#include <stdio.h>\n#include <lucent/log_c.h>\n'
            'void write_record(FILE *file) { fprintf(file, "record"); }\n'
            'void run(void) { lucent_log_error("x2", "failed"); }\n'
        ),
        "tools/build_stocklog.py": "PROXY_SOURCES = ('proxy.c',)\n",
        "tools/build_shim.py": "MODES = {'proxy', 'trace'}\n",
    }
    if text_violations(clean, clean):
        print("verify_source_policy selftest: clean fixture was rejected")
        return 1
    dirty = dict(clean)
    dirty["src/native/main.c"] = (
        'printf("bad");\nconst char *p = getenv("X");\n'
        'const char *q = x2_environment_get("X");\n'
    )
    dirty["tools/build_stocklog.py"] = 'run("tools/gen_probes.py")\n'
    violations = text_violations(dirty, dirty)
    reasons = "\n".join(item.reason for item in violations)
    if not all(
        word in reasons
        for word in ("environment", "configuration", "diagnostic", "static")
    ):
        print("verify_source_policy selftest: negative fixture escaped")
        return 1
    scratch = Path(__file__).resolve().parents[1] / "scratch/run"
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as directory:
        root = Path(directory)
        retired_path = root / "src/x86watch.c"
        retired_path.parent.mkdir(parents=True)
        retired_path.touch()
        retired = retired_path_violations(root)
        install = root / "game/Docs/License.txt"
        install.parent.mkdir(parents=True)
        install.write_bytes(b"retail license: \xff")
        if set(first_party_text(root)) != {"src/x86watch.c"}:
            print("verify_source_policy selftest: asset tree entered source policy")
            return 1
    if not any(item.location == "src/x86watch.c" for item in retired):
        print("verify_source_policy selftest: retired x86watch path escaped")
        return 1
    print("verify_source_policy selftest: five negative classes detected")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    checked, violations = source_violations(args.root)
    if violations:
        print(f"verify_source_policy: FAILED ({checked} inputs)")
        for violation in violations:
            print(f"  {violation.location}: {violation.reason}")
        return 1
    print(f"verify_source_policy: passed ({checked} inputs, 0 violations)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
