#!/usr/bin/env bash
# Exact-scope cleanup owned by tools/smoke_loop.sh.
set -euo pipefail

cd "$(dirname "$0")/.."
[ -d .git ] && [ -f tools/smoke_loop.sh ] || {
    echo "cleanup_smoke: not at the xmen2 repository root; refusing" >&2
    exit 2
}

case "${1:-}" in
    selftest)
        target=scratch/smoke-selftest
        if [ -e "$target" ]; then
            [ -d "$target" ] && [ ! -L "$target" ] || {
                echo "cleanup_smoke: $target is not the expected directory; refusing" >&2
                exit 2
            }
            find "$target" -depth -delete
        fi
        ;;
    screenshot)
        target=scratch/screenshots/smoke_loop.ppm
        if [ -e "$target" ] || [ -L "$target" ]; then
            [ -f "$target" ] && [ ! -L "$target" ] || {
                echo "cleanup_smoke: $target is not the expected regular file; refusing" >&2
                exit 2
            }
            find "$target" -maxdepth 0 -type f -delete
        fi
        ;;
    *)
        echo "usage: tools/cleanup_smoke.sh selftest|screenshot" >&2
        exit 2
        ;;
esac
