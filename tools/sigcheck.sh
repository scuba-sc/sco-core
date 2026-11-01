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
    # Windows only (LOCALAPPDATA is set there): the WinGet LLVM-MinGW install, when it is not on PATH.
    # Its libc++/libunwind DLLs sit beside the compiler, so put that directory on PATH for the run.
    for c in "$LOCALAPPDATA"/Microsoft/WinGet/Packages/*LLVM-MinGW*/*/bin/clang++.exe; do
      if [ -f "$c" ]; then CXX=$c; PATH="$(dirname "$c"):$PATH"; break; fi
    done
  fi
  : "${CXX:?no clang++ or g++ on PATH; set CXX}"
fi
# Only the signature registration/tables belong to this scanner CLI.
GAME=("$ROOT/src/game/signatures.cpp" "$ROOT"/src/game/*_sigs.cpp)
"$CXX" -std=c++20 -O2 -Wall -Wextra -Werror -I "$ROOT/include" "$ROOT/tools/sco-sigcheck.cpp" \
  "$ROOT"/src/sco_scan.cpp "$ROOT"/src/sco_signatures.cpp "$ROOT"/src/sco_signature_registry.cpp "$ROOT"/src/sco_log_status.cpp \
  "$ROOT"/src/sco_pe_file.cpp "${GAME[@]}" -o "$OUT/sco-sigcheck"
exec "$OUT/sco-sigcheck" "$@"
