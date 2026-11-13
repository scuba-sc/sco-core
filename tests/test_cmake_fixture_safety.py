#!/usr/bin/env python3
"""Configure the real source twice and retain all evidence; never clean up."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile

SOURCE = Path(__file__).resolve().parents[1]
TIMEOUT = 180


def checked_directory(path):
    """Reject links/reparse points in the directory chain, including on Windows."""
    path = Path(os.path.abspath(path))
    for part in reversed((path, *path.parents)):
        info = part.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise RuntimeError(f"link or reparse point in test path: {part}")
        if not stat.S_ISDIR(info.st_mode):
            raise RuntimeError(f"not a directory: {part}")
    return path


def snapshot(root):
    result = {}
    for base, directories, files in os.walk(root, followlinks=False):
        for name in directories + files:
            path = Path(base) / name
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                raise RuntimeError(f"unexpected link or reparse point: {path}")
            if name in files:
                if not stat.S_ISREG(info.st_mode):
                    raise RuntimeError(f"unexpected non-regular file: {path}")
                result[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def run(command, env):
    result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors="replace", timeout=TIMEOUT)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {command!r}\n{result.stdout}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--generator", required=True)
    parser.add_argument("--platform", default="")
    parser.add_argument("--toolset", default="")
    args = parser.parse_args()
    parent = checked_directory(args.output_root)

    # The root configure has no project includes or subdirectories. Fail closed if
    # that changes, rather than assuming newly loaded CMake code is safe to run.
    cmake_source = (SOURCE / "CMakeLists.txt").read_text(encoding="utf-8")
    if re.search(r"\bfile\s*\(\s*REMOVE(?:_RECURSE)?\b", cmake_source, re.IGNORECASE):
        raise RuntimeError("source configure contains a file(REMOVE...) command")
    if re.search(r"(?m)^\s*(?:include|add_subdirectory)\s*\(", cmake_source, re.IGNORECASE):
        raise RuntimeError("source configure loads additional CMake code; audit required")

    # Local-only check: abi_snapshot.py otherwise attempts a network fetch of this tag.
    try:
        probe = subprocess.run(["git", "-C", str(SOURCE), "cat-file", "-e",
                                "sdk-v1.1.0:include/sco_api.h"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=20)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        print("SKIP: local git or the released SDK header tag is unavailable")
        return 77
    if probe.returncode:
        print("SKIP: sdk-v1.1.0 released header is unavailable locally; no fetch attempted")
        return 77

    # mkdtemp uses exclusive creation; a collision is never reused. No cleanup is performed.
    root = Path(tempfile.mkdtemp(prefix="cmake-fixture-safety-", dir=parent))
    checked_directory(root)
    build = root / "build"
    tmp = root / "tmp"
    build.mkdir()
    tmp.mkdir()
    checked_directory(build)
    checked_directory(tmp)
    env = {k: v for k, v in os.environ.items()
           if not k.upper().startswith("CMAKE_") and k.upper() not in
           {"CC", "CXX", "CFLAGS", "CXXFLAGS", "LDFLAGS"}}
    env.update(TEMP=str(tmp), TMP=str(tmp), TMPDIR=str(tmp))
    command = [args.cmake, "-S", str(SOURCE), "-B", str(build), "-G", args.generator,
               "-DSCO_GAME_SC=OFF", "-DSCO_BUILD_TESTS=ON"]
    if args.platform:
        command.extend(["-A", args.platform])
    if args.toolset:
        command.extend(["-T", args.toolset])

    def configure():
        output = run(command, env)
        match = re.search(r"^-- Test fixture namespace: (.+)$", output, re.MULTILINE)
        if not match:
            raise RuntimeError(f"fixture namespace missing from configure output:\n{output}")
        namespace = checked_directory(match.group(1).strip())
        if namespace.parent != build or not namespace.name.startswith("sco-fixtures-"):
            raise RuntimeError(f"fixture namespace escaped fresh build root: {namespace}")
        return namespace

    first = configure()
    before = snapshot(first)
    if not before or "plugins/m0/plugin.ini" not in before:
        raise RuntimeError("first configuration did not generate expected fixtures")
    second = configure()
    if first == second:
        raise RuntimeError("repeat configuration reused the fixture namespace")
    if snapshot(first) != before:
        raise RuntimeError("repeat configuration modified the first fixture namespace")
    ctest = (build / "CTestTestfile.cmake").read_text(encoding="utf-8")
    for name in ("test_plugins", "plugin_check_services", "host_sim_examples"):
        escaped_name = re.escape(name)
        pattern = re.compile(r'^\s*add_test\(\s*(?:"' + escaped_name +
                             r'"|\[=*\[' + escaped_name + r'\]=*\]|' +
                             escaped_name + r')(?=\s|\))')
        lines = [line.replace("\\\\", "/").replace("\\", "/") for line in ctest.splitlines()
                 if pattern.search(line) and "NOT_AVAILABLE" not in line]
        if not lines or any(second.as_posix() not in line or first.as_posix() in line for line in lines):
            raise RuntimeError(f"CTest paths do not name only the latest namespace for {name}: {lines}")
    print(f"PASS: configure preserved {len(before)} files and CTest uses {second}")
    print(f"Retained regression evidence: {root}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
