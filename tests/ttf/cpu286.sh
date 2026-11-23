#!/bin/sh
# cpu286.sh - check that OpenWatcom objects hold 80286 code only: no
# 32-bit registers or 386 instructions, no x87 instructions, no
# floating-point helpers (TTVIEW.EXE, ARCHITECTURE.md 11.11). Disassembles
# each object (wdis) and fails on any match.
#   usage: tests/ttf/cpu286.sh OBJ...
set -eu
bad=0
for o in "$@"; do
    # the mnemonic is the field after the bytes (tab-separated): 386
    # instructions, any x87 one (they all start with f); then 32-bit
    # registers in operands, and OpenWatcom's floating-point helpers
    out=$(wdis "$o" 2>/dev/null | grep -E "$(printf '\t')(movzx|movsx|shld|shrd|bsf|bsr|bt|bts|btr|btc|set[a-z]+|cwde|cdq|jecxz|pushad|popad|pushfd|popfd|iretd|lfs|lgs|lss|f[a-z0-9]+)($(printf '\t')|\$)|\b(e[abcd]x|e[sd]i|e[bs]p|[fg]s:)\b|__(FD|FS|I4F|U4F|I8F|CHP|ED|ES)[A-Z0-9]*|fltused" \
        || true)
    if [ -n "$out" ]; then
        echo "cpu286: $o has code past the 80286 or floating point:" >&2
        printf '%s\n' "$out" | head -5 >&2
        bad=1
    fi
done
exit $bad
