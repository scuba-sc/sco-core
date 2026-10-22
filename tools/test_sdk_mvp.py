#!/usr/bin/env python3
"""Run the packaged MCP scaffold proof using the repository's archive tests."""
import subprocess
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    return subprocess.run([sys.executable, "-m", "unittest",
        "sdk.test_package.RealManifest.test_archive_agent_discovery_and_scaffold"],
        cwd=root, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
