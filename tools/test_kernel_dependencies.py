#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused integration tests for the kernel-only CMake dependency gate."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


CHECKER = Path(__file__).with_name("check_kernel_dependencies.py")


class KernelDependenciesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR"))
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.build = self.root / "build"
        (self.root / "core.c").write_text("int core(void) { return 0; }\n")
        (self.root / "lua.c").write_text("int lua(void) { return 0; }\n")
        (self.root / "main.c").write_text("int core(void); int main(void) { return core(); }\n")

    def configure(self, extra=""):
        (self.root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\nproject(kernel_probe C)\n"
            "option(SCO_GAME_SC \"Game pack\" OFF)\n"
            "add_library(sco_core STATIC core.c)\n"
            "add_library(sco_lua STATIC lua.c)\n"
            "add_executable(sco_app main.c)\n"
            "target_link_libraries(sco_app PRIVATE sco_core sco_lua)\n" + extra)
        self.build.mkdir(exist_ok=True)
        (self.build / "CMakeGraphVizOptions.cmake").write_text("set(GRAPHVIZ_OBJECT_LIBS TRUE)\n")
        subprocess.run(["cmake", "-S", str(self.root), "-B", str(self.build),
                        "-G", "Unix Makefiles", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                        f"--graphviz={self.build / 'kernel-targets.dot'}"],
                       check=True, capture_output=True, text=True)
        subprocess.run(["cmake", "--build", str(self.build)],
                       check=True, capture_output=True, text=True)

    def check(self):
        return subprocess.run(["python3", str(CHECKER), str(self.build)],
                              capture_output=True, text=True)

    def test_clean_build_allows_public_game_abi_header(self):
        header = self.root / "include" / "sco" / "game" / "types.h"
        header.parent.mkdir(parents=True)
        header.write_text("#define GAME_ABI_SIZE 128\n")
        (self.root / "core.c").write_text('#include "include/sco/game/types.h"\nint core(void) { return 0; }\n')
        self.configure("add_library(sco_abi_game_c OBJECT core.c)\n")
        graph = (self.build / "kernel-targets.dot").read_text()
        self.assertRegex(graph, r'label = "sco_abi_game_c", shape = hexagon')
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("4 compiled objects", result.stdout)

    def test_shared_library_soname_is_metadata_not_input(self):
        # Native plugins have a SONAME such as m0.so, not an input in the build root.
        self.configure("add_library(native_plugin SHARED lua.c)\n")
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_header_only_game_implementation_is_rejected(self):
        header = self.root / "src" / "game" / "inline.h"
        header.parent.mkdir(parents=True)
        header.write_text("static inline int hidden(void) { return 0; }\n")
        (self.root / "core.c").write_text('#include "src/game/inline.h"\nint core(void) { return hidden(); }\n')
        self.configure()
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("game implementation dependency", result.stderr)

    def test_renamed_game_symbol_is_rejected(self):
        (self.root / "core.c").write_text("int RegisterGameSignatures(void) { return 0; }\nint core(void) { return 0; }\n")
        self.configure()
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("game pack symbols", result.stderr)

    def test_missing_depfile_fails_closed(self):
        self.configure()
        next(self.build.glob("CMakeFiles/**/*.o.d")).unlink()
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("missing compiler depfile", result.stderr)

    def test_external_game_object_is_rejected_as_link_input(self):
        obj = self.root / "src" / "game" / "hidden.o"
        obj.parent.mkdir(parents=True)
        subprocess.run(["cc", "-c", str(self.root / "core.c"), "-o", str(obj)], check=True)
        self.configure(f'target_link_options(sco_app PRIVATE "{obj}")\n')
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("game link input", result.stderr)

    def test_game_archive_in_driver_response_file_is_rejected(self):
        archive = self.root / "libsco_game_sc.a"
        obj = self.root / "imported.o"
        subprocess.run(["cc", "-c", str(self.root / "core.c"), "-o", str(obj)], check=True)
        subprocess.run(["ar", "rcs", str(archive), str(obj)], check=True)
        response = self.root / "libraries.rsp"
        response.write_text(f'"{archive}"\n')
        self.configure(f'target_link_options(sco_app PRIVATE "@{response}")\n')
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("game link input", result.stderr)

    def test_missing_linked_archive_fails_closed(self):
        self.configure()
        (self.build / "libsco_core.a").unlink()
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("missing link input", result.stderr)

    def test_missing_one_link_command_fails_closed(self):
        self.configure()
        (self.build / "CMakeFiles" / "sco_app.dir" / "link.txt").unlink()
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("missing target link command", result.stderr)

    def test_linker_only_game_library_is_rejected(self):
        # Linker options are not target_link_libraries: Graphviz has no game node.
        archive = self.root / "libsco_game_sc.a"
        obj = self.root / "imported.o"
        subprocess.run(["cc", "-c", str(self.root / "core.c"), "-o", str(obj)], check=True)
        subprocess.run(["ar", "rcs", str(archive), str(obj)], check=True)
        self.configure(f'target_link_options(sco_app PRIVATE "-L{self.root}" "-lsco_game_sc")\n')
        self.assertNotIn("sco_game_sc", (self.build / "kernel-targets.dot").read_text())
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("game link input", result.stderr)

    def test_interface_game_dependency_is_rejected_from_graph(self):
        # There are no game objects or symbols: only the actual CMake target edge.
        self.configure("add_library(sco_game_sc INTERFACE)\n"
                       "target_link_libraries(sco_core INTERFACE sco_game_sc)\n")
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("game target dependency", result.stderr)
        self.assertIn("sco_core -> sco_game_sc", result.stderr)


if __name__ == "__main__":
    unittest.main()
