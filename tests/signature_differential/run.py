#!/usr/bin/env python3
"""Compare actual f32cdb7 registry and candidate in independent processes."""
import argparse
import difflib
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
BASE = HERE / "baseline"
BLOBS = {
    BASE / "src/sco_signatures.cpp": "a6a10986104d6177f7a5b0e7811a52fc99b65eee",
    BASE / "include/sco/signatures.h": "1c274e44d5a1010db5e5366182e2f49a2383dda8",
    BASE / "src/sco_scan.cpp": "514c74051a8c20c8635d59e25bbe5fbad19e321d",
    BASE / "include/sco/scan.h": "599b2cdebd1efdbf00c0c1d514ed78f15b248400",
    BASE / "src/sco_log_status.cpp": "d4b0734640376708b8fc35c48839c26cb9ed6184",
    BASE / "include/sco/log.h": "7da600fa0201dba8e5eaf12b5236ed2a3b3929cb",
    BASE / "include/sco/status.h": "6ac2590d7e4e9c77a7d59eecb68811a0561e1110",
}

def blob_hash(path):
    data = path.read_bytes()
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

def run(cmd, cwd=None, timeout=None):
    return subprocess.run([str(x) for x in cmd], cwd=cwd, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default=os.environ.get("CXX", "cl" if os.name == "nt" else "c++"))
    parser.add_argument("--baseline-exe", type=Path, help="use a prebuilt baseline executable")
    parser.add_argument("--candidate-exe", type=Path, help="use a prebuilt candidate executable")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--seeds", type=int, default=400)
    args = parser.parse_args()
    if args.seeds < 0:
        parser.error("--seeds must be non-negative")
    prebuilt = args.baseline_exe is not None or args.candidate_exe is not None
    if prebuilt and (args.baseline_exe is None or args.candidate_exe is None):
        parser.error("--baseline-exe and --candidate-exe must be supplied together")
    if prebuilt and args.sanitize:
        parser.error("--sanitize cannot instrument prebuilt executables; configure sanitizers in the build instead")
    if prebuilt:
        args.baseline_exe = args.baseline_exe.resolve()
        args.candidate_exe = args.candidate_exe.resolve()
        if not args.baseline_exe.is_file() or not args.candidate_exe.is_file():
            parser.error("both prebuilt executable paths must name existing files")
    msvc = Path(args.cxx).name.lower() in ("cl", "cl.exe")
    if not prebuilt and msvc and args.sanitize:
        parser.error("--sanitize requires both address and undefined behavior sanitizers; this MSVC runner does not provide them")
    for path, expected in BLOBS.items():
        if blob_hash(path) != expected:
            raise SystemExit(f"baseline blob changed: {path}")
    baseline_sources = [HERE / "scenarios.cpp", BASE / "src/sco_scan.cpp", BASE / "src/sco_log_status.cpp"]
    candidate_sources = [HERE / "scenarios.cpp", ROOT / "src/sco_scan.cpp", ROOT / "src/sco_log_status.cpp"]
    with tempfile.TemporaryDirectory(prefix="signature-diff-") as tmp:
        if prebuilt:
            baseline, candidate = args.baseline_exe, args.candidate_exe
        else:
            suffix = ".exe" if os.name == "nt" else ""
            baseline, candidate = Path(tmp) / ("baseline" + suffix), Path(tmp) / ("candidate" + suffix)
            if msvc:
                flags = ["/nologo", "/std:c++20", "/EHsc"]
                baseline_cmd = [args.cxx, *flags, "/DSCO_DIFFERENTIAL_BASELINE", f"/I{BASE / 'include'}", f"/I{ROOT / 'include'}", *baseline_sources, BASE / "src/sco_signatures.cpp", f"/Fe{baseline}"]
                candidate_cmd = [args.cxx, *flags, f"/I{ROOT / 'include'}", *candidate_sources, ROOT / "src/sco_signatures.cpp", ROOT / "src/sco_signature_registry.cpp", f"/Fe{candidate}"]
            else:
                flags = ["-std=c++20", "-O1", "-g", "-pthread"]
                if args.sanitize:
                    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                baseline_cmd = [args.cxx, *flags, "-DSCO_DIFFERENTIAL_BASELINE", "-I", BASE / "include", "-I", ROOT / "include", *baseline_sources, BASE / "src/sco_signatures.cpp", "-o", baseline]
                candidate_cmd = [args.cxx, *flags, "-I", ROOT / "include", *candidate_sources, ROOT / "src/sco_signatures.cpp", ROOT / "src/sco_signature_registry.cpp", "-o", candidate]
            # cl.exe writes object files to its working directory during a combined build.
            run(baseline_cmd, cwd=tmp)
            run(candidate_cmd, cwd=tmp)
        for case in ["fixed", "callback_states", "chain", *(str(n) for n in range(args.seeds))]:
            before, after = run([baseline, case], timeout=10).stdout, run([candidate, case], timeout=10).stdout
            if case == "fixed":
                for expected in ("ROW test.forward 1 1 16 -", "ROW test.undeclared 4 0 -1 context lookup unavailable", "ROW test.cycle_a 5 0 -1 test.cycle_b", "ROW test.cycle_b 5 0 -1 test.cycle_a", "ROW test.self 5 0 -1 test.self", "ROW test.rip 1 1 100 -", "ROW test.forward 5 0 -1 test.unique"):
                    if expected not in before:
                        raise AssertionError(f"baseline sanity check missing: {expected}")
            if case == "callback_states":
                for expected in ("ROW state.not_run 0 7 120 explicit callback state",
                                 "ROW state.first_dependent 5 0 -1 state.not_run",
                                 "ROW state.forward_dependent 5 0 -1 state.forward_not_run",
                                 "CALLS 0 0 0 3 4 5 6 7 9 9 10"):
                    if expected not in before:
                        raise AssertionError(f"baseline callback-state sanity check missing: {expected}")
            lines = before.splitlines()
            starts = [i for i, line in enumerate(lines) if line.startswith("PASS ")]
            if len(starts) != 3 or lines[starts[0] + 1:starts[1]] != lines[starts[2] + 1:]:
                raise AssertionError(f"case {case}: baseline changed when the initial image was restored")
            if before != after:
                diff = list(difflib.unified_diff(before.splitlines(), after.splitlines(), fromfile="baseline", tofile="candidate", lineterm=""))
                raise AssertionError(f"case {case} differed:\n" + "\n".join(diff[:120]))
        mode = "prebuilt; sanitizer instrumentation controlled by build" if prebuilt else ("ASan+UBSan" if args.sanitize else "unsanitized")
        print(f"signature differential: {args.seeds + 3} scenarios, three resolutions each, 0 differences ({mode})")

if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        if e.stdout:
            print(e.stdout, end="", file=sys.stderr)
        if e.stderr:
            print(e.stderr, end="", file=sys.stderr)
        raise SystemExit(e.returncode or 1)
    except subprocess.TimeoutExpired as e:
        raise SystemExit(f"signature differential timed out after {e.timeout}s: {e.cmd}")
