#!/usr/bin/env python3
"""After G1 integration, classify the six real adapter files in host-source-sc only.

No placeholders, broad include grants, policy changes or release operations.
Run from any directory: python3 sdk/add_host_adapter_components.py.
"""
import fnmatch
import json
from pathlib import Path
import sys
import tempfile

sys.dont_write_bytecode = True
import package as pkg

ADAPTER_FILES = ("include/sco/game/host.h", "src/game/host.cpp",
                 "src/game/host_testing.h", "tests/game_host_data.cpp",
                 "tests/game_host.cpp", "docs/game/host.md")


def main():
    root = Path(pkg.ROOT)
    missing = [p for p in ADAPTER_FILES if not (root / p).is_file() or (root / p).is_symlink()]
    if missing:
        raise pkg.ManifestError("missing real adapter files: " + ", ".join(missing))
    manifest = Path(pkg.MANIFEST)
    original = manifest.read_text(encoding="utf-8")
    m = json.loads(original)
    if "host-source-sc" not in m["profiles"]:
        raise pkg.ManifestError("integrate the G16 host-source-sc profile first")
    entries = json.loads((root / "docs/legal/provenance.json").read_text())["entries"]
    for p in ADAPTER_FILES:
        existing = [c for c in m["components"] if c["source"] == p]
        if existing:
            if len(existing) != 1 or existing[0]["profiles"] != ["host-source-sc"] or existing[0].get("public_header"):
                raise pkg.ManifestError(p + ": existing component is not private host-source-sc only")
            continue
        provenance = next((e for e in entries if
                           (p.startswith(e["path"][:-2]) if e["path"].endswith("/**") else
                            fnmatch.fnmatchcase(p, e["path"]) and ("/" in e["path"] or "/" not in p))), None)
        if provenance is None or provenance["status"] == "unresolved":
            raise pkg.ManifestError(p + ": missing or unresolved provenance; integrate G1 records first")
        m["components"].append({"id": "host-source-sc-" + p.replace("/", "-"),
                                "source": p, "destination": p, "profiles": ["host-source-sc"],
                                "origin": provenance["origin"], "license": provenance["license"],
                                "group": "host-source-sc", "public_header": False})
    # Test the entire candidate against real files and provenance before modifying the manifest.
    # TMPDIR controls the validation scratch location; no files are invented at adapter paths.
    with tempfile.TemporaryDirectory(prefix="host-adapter-manifest-") as tmp:
        candidate = Path(tmp, "components.json")
        candidate.write_text(json.dumps(m, indent=1) + "\n", encoding="utf-8")
        checked = pkg.load_manifest(str(candidate), pkg.ROOT)
        pkg.attributions(checked, "host-source-sc")
    updated = json.dumps(m, indent=1) + "\n"
    if updated != original:
        manifest.write_text(updated, encoding="utf-8")
        print("classified six real G1 files as private host-source-sc components")
    else:
        print("six G1 files already classified as private host-source-sc components")


if __name__ == "__main__":
    try:
        main()
    except (pkg.ManifestError, OSError, ValueError) as e:
        sys.exit(f"add_host_adapter_components.py: {e}")
