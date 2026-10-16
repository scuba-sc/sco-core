#!/usr/bin/env bash
# Build tools/sco-sigcheck and run it against a StarCitizen.exe on disk.
#   tools/sigcheck.sh <StarCitizen.exe> [-v] [--catalog <runtime_catalog.inc>]
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/tests/out
mkdir -p "$OUT"
if [ -z "${CXX:-}" ]; then
  CXX=$(command -v clang++ || command -v g++ || true)
  if [ -z "$CXX" ] && [ -n "${LOCALAPPDATA:-}" ]; then
    for c in "$LOCALAPPDATA"/Microsoft/WinGet/Packages/*LLVM-MinGW*/*/bin/clang++.exe \
             "$LOCALAPPDATA"/Microsoft/WinGet/Packages/*LLVM-MinGW*/bin/clang++.exe; do
      if [ -f "$c" ]; then CXX="$c"; break; fi
    done
  fi
fi
CXX=${CXX:-clang++}
if command -v cygpath >/dev/null 2>&1 && [ -f "$CXX" ]; then
  CXX=$(cygpath -u "$CXX")
fi
export PATH="$(dirname "$CXX"):$PATH"
# Only the signature registration/tables belong to this scanner CLI.
GAME=("$ROOT/src/game/signatures.cpp" "$ROOT"/src/game/*_sigs.cpp)
"$CXX" -static -std=c++20 -O2 -Wall -Wextra -Werror -I "$ROOT/include" "$ROOT/tools/sco-sigcheck.cpp" \
  "$ROOT"/src/sco_scan.cpp "$ROOT"/src/sco_signatures.cpp "$ROOT"/src/sco_log_status.cpp \
  "$ROOT"/src/sco_pe_file.cpp "${GAME[@]}" -o "$OUT/sco-sigcheck"
exec "$OUT/sco-sigcheck" "$@"