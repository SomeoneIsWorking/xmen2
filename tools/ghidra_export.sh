#!/usr/bin/env bash
# ghidra_export.sh -- shared x86 translator toolchain, run from this port.
#
# The lifter lives in the `recomp-x86` repo (the x86-32 translator serves this
# PC port and an original-Xbox one alike). It resolves the port from the
# working directory, so this only has to find it and exec.
set -euo pipefail
RECOMP_X86="${RECOMP_X86_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../shared/recomp-x86" 2>/dev/null && pwd || true)}"
if [ -z "$RECOMP_X86" ] || [ ! -x "$RECOMP_X86/tools/ghidra_export.sh" ]; then
    echo "ghidra_export: no recomp-x86 checkout found (tried \$RECOMP_X86_DIR and ../../shared/recomp-x86)." >&2
    echo "  The x86 translator is a separate repo this port consumes; clone it into shared/." >&2
    exit 1
fi
exec "$RECOMP_X86/tools/ghidra_export.sh" "$@"
