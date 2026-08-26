#!/usr/bin/env bash
# ghidra_export.sh -- shared x86 translator toolchain, run from this port.
#
# The lifter lives in the `recomp-x86` repo (the x86-32 translator serves this
# PC port and an original-Xbox one alike). It resolves the port from the
# working directory, so this only has to find it and exec.
set -euo pipefail
# ONE checkout, the provisioned one. This used to fall back to a sibling
# clone at ../../../shared, and the port then ran the emitter from the vendored
# copy while `tools/ghidra_scripts` symlinked into the sibling -- two checkouts
# of the same repo, silently disagreeing.
RECOMP_X86="${RECOMP_X86_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../vendor/shared/recomp-x86" 2>/dev/null && pwd || true)}"
if [ -z "$RECOMP_X86" ] || [ ! -x "$RECOMP_X86/tools/ghidra_export.sh" ]; then
    echo "ghidra_export: no recomp-x86 checkout found (tried \$RECOMP_X86_DIR and vendor/shared/recomp-x86)." >&2
    echo "  The x86 translator is a separate repo this port consumes; run ./run.sh to provision it." >&2
    exit 1
fi
exec "$RECOMP_X86/tools/ghidra_export.sh" "$@"
