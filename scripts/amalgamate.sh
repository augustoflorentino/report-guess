#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/dist/infer/infer.h}
mkdir -p "$(dirname "$out")"

{
    cat "$root/include/infer/infer.h"
    echo
    echo "#if defined(INFR_IMPLEMENTATION) && !defined(INFR_IMPLEMENTATION_DONE)"
    echo "#define INFR_IMPLEMENTATION_DONE"
    for src in "$root"/src/*.h "$root"/src/*.c; do
        [ -f "$src" ] && grep -v '^#include "' "$src"
    done
    echo "#endif"
} > "$out"
echo "$out"
