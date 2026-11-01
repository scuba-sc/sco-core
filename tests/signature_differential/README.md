# Actual resolver differential fixture

The files under `baseline/` are byte-for-byte Git blobs from sco-core commit
`f32cdb7cc631a8d3047b6fe867425b71950ae4f1`:

- `src/sco_signatures.cpp`: blob `a6a10986104d6177f7a5b0e7811a52fc99b65eee`.
- `include/sco/signatures.h`: blob `1c274e44d5a1010db5e5366182e2f49a2383dda8`.
- Scanner and logging sources and headers are copied from the same commit; their
  blob IDs are checked by the runner.

They compile in a separate process with that commit's scanner and log sources. The same fixture compiles against the candidate registry and callback
context. The Python runner compares registration order, all result fields,
image-relative addresses, and resolver-call order over three resolutions, including
real SigPattern and RIP processing. Synthetic bytes only; no game file is read.
The baseline source is copied only for this regression; maintain its provenance.

Run from the repository root with GCC or Clang: `python3 tests/signature_differential/run.py --sanitize`.
On Windows, run from an MSVC developer command prompt: `python tests/signature_differential/run.py --cxx cl`.
MSVC runs are unsanitized; requesting `--sanitize` with `cl` fails clearly because
this runner's sanitizer mode requires both ASan and UBSan. Set `CXX` or pass
`--cxx` to select the compiler. The default covers two named cases and 400
deterministically generated graphs, each resolved three times, returning to the
initial image in the third pass. Compilation outputs use a temporary directory.

CMake may build the two fixtures with its configured compiler and pass their
absolute paths as `--baseline-exe PATH --candidate-exe PATH`; this mode does not
compile anything. Sanitizer instrumentation is controlled by that build, so the
runner rejects `--sanitize` when given prebuilt executables rather than claiming
it configured instrumentation. The baseline blob verification still runs.
