#!/usr/bin/env python3
"""One targeted G9 gate; synthetic providers and compile evidence, never gameplay."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "sdk"))
import sdk_catalogue
import package


def run(*args):
    subprocess.run([str(x) for x in args], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    clang = shutil.which("clang")
    if not clang:
        raise RuntimeError("Clang required for G9 C/C++ and Windows-x64 layout proof")
    # Compile only the added ABI pin in the four relevant language/layout modes.
    source = ROOT / "tests/abi_game_owned.c"
    for flags in [("-std=c11", "-x", "c"), ("-std=c++20", "-x", "c++"),
                  ("-std=c11", "-fshort-enums", "-x", "c"),
                  ("-target", "x86_64-pc-windows-msvc", "-ffreestanding", "-std=c11", "-x", "c")]:
        run(clang, *flags, "-Wall", "-Wextra", "-Werror", "-I", ROOT / "include", "-fsyntax-only", source)
    print("G9 ABI: C11, C++20, short-enums, Windows x64 cross-compile passed", flush=True)
    # Compiler-backed catalogue closure validates new methods and unchanged frozen root ABI.
    records, inventory = sdk_catalogue.validate(ROOT, clang)
    own = [r for r in records if r["table"] == "sc_owned_v1"]
    assert len(own) == 6 and inventory["tables"]["sc_owned_v1"]["size"] == 56
    for r in own:
        assert r["language_mappings"]["c"]["status"] == "direct"
        assert r["language_mappings"]["cpp"]["status"] == "direct"
        assert r["language_mappings"]["csharp"]["status"] == "unsupported"
        assert r["language_mappings"]["lua"]["status"] == "unsupported"
    manifest = package.load_manifest(root=str(ROOT))
    for profile in ("core", "core-source", "game-sc", "legacy"):
        chosen = {c["source"] for c in package.select(manifest, profile)}
        expected = profile in {"game-sc", "legacy"}
        assert ("include/sc_owned.h" in chosen) == expected, profile
        assert ("tests/abi_game_owned.c" in chosen) == expected, profile
        assert "src/game/services/owned.cpp" not in chosen, profile
        assert "tests/test_owned_removal.cpp" not in chosen, profile
    # Windows-specific source cannot run here; pin the actual wiring separately and say so.
    entities = (ROOT / "src/game/services/entities.cpp").read_text()
    keep = entities.split("sco_result SvcKeep(", 1)[1].split("sco_result SvcWatch(", 1)[0]
    assert "if (BuildRetainedBy(self, id))" in keep
    assert "Result::Failed" in keep
    prune = entities.split("void PruneOwned()", 1)[1].split("sco_result SvcSpawn(", 1)[0]
    assert "!BuildRetainedBy(g_owned[i].owner, g_owned[i].id)" in prune
    assert "const Result owned = StartOwned();" in entities
    assert "StopOwned();" in entities and "DropBuildRetention(self, id);" in entities
    assert "bool EntitiesOwnedBy" in entities
    owned = (ROOT / "src/game/services/owned.cpp").read_text()
    assert "alive(" not in owned and "RegisterPlayerVehicle" not in owned
    print("G9 catalogue, explicit language routes, package isolation and real-source wiring passed", flush=True)
    run("cmake", "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug")
    run("cmake", "--build", build, "--target", "test_owned_removal", "-j6")
    run("ctest", "--test-dir", build, "-R", "^test_owned_removal$", "--output-on-failure", "-V")
    print("G9 targeted contract gate passed; Windows/MSVC runtime and real engine NOT VERIFIED")


if __name__ == "__main__":
    main()
