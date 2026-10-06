#!/bin/sh
set -eu
cd "$(dirname "$0")"
command -v uv >/dev/null 2>&1 || {
    echo "run: uv is required. Install it from https://docs.astral.sh/uv/ and re-run." >&2
    exit 2
}
if [ "$#" -ne 0 ]; then
    echo "run: takes no arguments; project tools are separate from run.sh" >&2
    exit 2
fi
exec uv run --frozen python bootstrap.py
