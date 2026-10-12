#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The kernel-only dependency gate, consuming CMake's generated build evidence.

Run after a complete Unix Makefiles build configured with SCO_GAME_SC=OFF,
CMAKE_EXPORT_COMPILE_COMMANDS=ON and --graphviz=<build>/kernel-targets.dot.
Public game ABI header pins are allowed; game implementation and link targets are not.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys


class DependencyError(Exception):
    pass


GAME_TARGET = re.compile(r"^(?:sco_game(?:_.*)?|sco_datacore(?:_.*)?|sco_pak|sco_lua_game|sco-sigcheck|sco-dcb)$")
NODE = re.compile(r'^\s*"([^"\\]+)"\s*\[\s*label\s*=\s*("(?:[^"\\]|\\.)*")\s*,\s*shape\s*=\s*(\w+)', re.MULTILINE)
EDGE = re.compile(r'^\s*"([^"\\]+)"\s*->\s*"([^"\\]+)"', re.MULTILINE)


def check_graph(path):
    text = path.read_text()
    declarations = NODE.findall(text)
    nodes = {node: json.loads(label).split("\n", 1)[0] for node, label, _ in declarations}
    object_targets = {nodes[node] for node, _, shape in declarations if shape == "hexagon"}
    edges = EDGE.findall(text)
    if not {"sco_core", "sco_app", "sco_lua"}.issubset(nodes.values()):
        raise DependencyError("target graph is missing required kernel targets")
    for source, target in edges:
        if source not in nodes or target not in nodes:
            raise DependencyError("target graph has an unresolved edge")
        if GAME_TARGET.fullmatch(nodes[target]):
            raise DependencyError(f"game target dependency: {nodes[source]} -> {nodes[target]}")
    for label in nodes.values():
        if GAME_TARGET.fullmatch(label):
            raise DependencyError(f"game target dependency: {label}")
    return set(nodes.values()), len(edges), object_targets


def game_input(token):
    name = Path(token.replace("\\", "/")).name
    if name.startswith("-l:"):
        name = name[3:]
    elif name.startswith("-l"):
        name = name[2:]
    if name.startswith("lib"):
        name = name[3:]
    name = re.sub(r"\.(?:a|lib|so(?:\..*)?|dylib|dll)$", "", name)
    return bool(GAME_TARGET.fullmatch(name))


def link_tokens(tokens, build, stack=()):
    for token in tokens:
        if token.startswith("-Wl,"):
            yield from link_tokens(token[4:].split(","), build, stack)
        elif token.startswith(("@rpath/", "@loader_path/", "@executable_path/")):
            # Mach-O runtime paths are metadata, not linker response files.
            yield token
        elif token.startswith("@"):
            response = (build / token[1:]).resolve()
            if response in stack:
                raise DependencyError("recursive linker response file")
            yield from link_tokens(shlex.split(response.read_text()), build, (*stack, response))
        else:
            yield token


def link_inputs(command, build):
    """Expand inputs, excluding outputs and SONAME/install-name metadata."""
    tokens = list(link_tokens(shlex.split(command), build))
    if not tokens:
        return
    tool = Path(tokens[0]).name
    if tool.endswith("ranlib"):
        return
    if tool.endswith("ar"):
        # CMake emits ar <operation> <output archive> <object inputs>, then ranlib.
        yield from tokens[3:]
        return
    it = iter(tokens[1:])
    for token in it:
        if token in ("-o", "-soname", "--soname", "-install_name", "-rpath", "--rpath"):
            if next(it, None) is None:
                raise DependencyError(f"link option has no value: {token}")
            continue
        if token.startswith(("--soname=", "--rpath=")):
            continue
        yield token


def check_links(build):
    links = sorted(build.glob("CMakeFiles/**/*.dir/link.txt"))
    if not links:
        raise DependencyError("missing CMake link commands (Unix Makefiles required)")
    objects = set()
    libraries = set()
    for link in links:
        for token in (token for command in link.read_text().splitlines()
                      for token in link_inputs(command, build)):
            if game_input(token) or game_source(token):
                raise DependencyError(f"game link input in {link.relative_to(build)}: {token}")
            if not token.startswith("-") and re.search(r"\.(?:o|a|lib|so(?:\..*)?|dylib)$", token):
                artifact = (build / token).resolve()
                if not artifact.is_file():
                    raise DependencyError(f"missing link input: {token}")
                (objects if token.endswith(".o") else libraries).add(artifact)
    if not objects:
        raise DependencyError("no actual object inputs in CMake link commands")
    return len(links), objects, libraries


def game_source(token):
    path = token.replace("\\", "/")
    return bool(re.search(r"(?:^|/)src/(?:game|datacore)(?:/|$)", path)
                or re.search(r"(?:^|/)(?:plugins/lua/sco_lua_game\.c|tools/sco-(?:sigcheck|dcb)\.cpp)$", path))


def check_objects(build, targets, object_targets):
    commands = json.loads((build / "compile_commands.json").read_text())
    if not isinstance(commands, list) or not commands:
        raise DependencyError("missing compile commands")
    objects = set()
    for entry in commands:
        directory = Path(entry["directory"])
        source = (directory / entry["file"]).resolve()
        if game_source(str(source)):
            raise DependencyError(f"game implementation dependency: {source}")
        tokens = entry.get("arguments") or shlex.split(entry["command"])
        try:
            obj = (directory / tokens[tokens.index("-o") + 1]).resolve()
            # Unix Makefiles emits <object>.d; compile_commands intentionally omits -MF.
            dep = Path(str(obj) + ".d")
        except (ValueError, IndexError) as exc:
            raise DependencyError("compile command lacks object/depfile output") from exc
        if not obj.is_file():
            raise DependencyError(f"missing compiled object: {obj}")
        target_dirs = [parent for parent in obj.parents
                       if parent.name.endswith(".dir") and parent.parent.name == "CMakeFiles"]
        if not target_dirs:
            raise DependencyError(f"object has no CMake target directory: {obj}")
        target_dir = target_dirs[0]
        target = target_dir.name[:-4]
        if target not in targets:
            raise DependencyError(f"compiled target missing from CMake graph: {target}")
        if target not in object_targets and not (target_dir / "link.txt").is_file():
            raise DependencyError(f"missing target link command: {target}")
        if not dep.is_file():
            raise DependencyError(f"missing compiler depfile: {dep}")
        dependencies = shlex.split(dep.read_text().replace("\\\n", ""))
        for token in [str(obj), *dependencies]:
            if game_source(token):
                raise DependencyError(f"game implementation dependency in {dep}: {token}")
        objects.add(obj)
    # Include unexpected/stale objects too; a clean CI build should have none.
    for obj in build.rglob("*.o"):
        if game_source(str(obj)):
            raise DependencyError(f"game pack object: {obj}")
    for dep in build.rglob("*.o.d"):
        for token in shlex.split(dep.read_text().replace("\\\n", "")):
            if game_source(token):
                raise DependencyError(f"game implementation dependency in {dep}: {token}")
    return objects


def check_symbols(build, objects):
    # Symbol names supplement the structured graph, compile and link inputs.
    # Unlike the old pipeline, a missing/broken nm cannot silently pass.
    inputs = sorted(set(build.rglob("*.a")) | objects)
    result = subprocess.run(["nm", "-C", "--defined-only", *(str(path) for path in inputs)],
                            check=True, capture_output=True, text=True)
    if re.search(r"RegisterGameSignatures|datacore::", result.stdout):
        raise DependencyError("game pack symbols in kernel objects/libraries")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    try:
        build = args.build.resolve()
        targets, edges, object_targets = check_graph(build / "kernel-targets.dot")
        links, linked_objects, libraries = check_links(build)
        objects = check_objects(build, targets, object_targets)
        check_symbols(build, objects | linked_objects | libraries)
    except (DependencyError, OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        print(f"kernel dependencies: FAIL: {exc}", file=sys.stderr)
        return 1
    print(f"kernel dependencies: OK ({len(targets)} targets, {edges} edges, "
          f"{links} link commands, {len(linked_objects)} linked objects, "
          f"{len(libraries)} libraries, {len(objects)} compiled objects)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
