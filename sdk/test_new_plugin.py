#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for sdk/tools/sco-new-plugin.py. Run: python3 sdk/test_new_plugin.py

1. render_plugin is pure and refuses bad ids, services and text; the CLI refuses unsafe outputs.
2. C and C++ plugins, with and without a menu, are generated into a temp folder, configured
   against this sdk/ folder the way an SDK user would (-DSCO_SDK=...), built, and checked by
   sco-plugin-check with and without --short-tables (the generated `check` target, plus a direct
   run whose output is inspected).
"""
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "tools", "sco-new-plugin.py")
spec = importlib.util.spec_from_file_location("sco_new_plugin", TOOL)
assert spec and spec.loader
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)

CMAKE = shutil.which("cmake") or ""


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


class Render(unittest.TestCase):
    def test_pure_and_deterministic(self):
        s = {"id": "my_radar", "lang": "cpp", "with_menu": True, "services": ["game.actors", "sco.widgets"]}
        a = gen.render_plugin(dict(s))
        self.assertEqual(a, gen.render_plugin(dict(s)))
        self.assertEqual(sorted(a), ["CMakeLists.txt", "my_radar.cpp", "plugin.ini"])
        self.assertEqual(sorted(gen.render_plugin({"id": "x", "lang": "c"})), ["CMakeLists.txt", "plugin.ini", "x.c"])
        for text in a.values():
            self.assertNotIn(HERE, text)
            self.assertNotIn("\r", text)

    def test_ini_and_sources(self):
        files = gen.render_plugin({"id": "my_radar", "lang": "c", "with_menu": True,
                                   "services": ["game.actors@1.1", "sco.widgets"]})
        ini = files["plugin.ini"]
        self.assertIn("id = my_radar\n", ini)
        self.assertIn("api = 1.0\n", ini)
        self.assertIn("entry = my_radar.dll\n", ini)
        self.assertIn("; requires = game.actors, sco.widgets\n", ini)
        src = files["my_radar.c"]
        self.assertIn('"game.actors"', src)
        self.assertIn("0x00010001u", src)
        self.assertNotIn('"sco.widgets"', src.split("service_names")[1].split(";")[0])
        self.assertIn("SCO_WIDGET_COLUMN", src)
        self.assertIn("api->size >= offset + size", src)
        cmake = files["CMakeLists.txt"]
        self.assertIn("sco_add_plugin(my_radar SOURCES my_radar.c INI plugin.ini)", cmake)
        self.assertIn("--short-tables", cmake)
        self.assertIn("add_custom_target(check", cmake)
        cpp = gen.render_plugin({"id": "my_radar", "lang": "cpp", "with_menu": True})["my_radar.cpp"]
        self.assertIn("scosdk::PanelBuilder", cpp)
        self.assertIn('SCO_PLUGIN(MyRadar, "my_radar", "0.1.0", "you");', cpp)
        self.assertEqual(gen.class_name("3d_map"), "Plugin3dMap")

    def test_refusals(self):
        bad = [
            {"id": "My", "lang": "c"}, {"id": "", "lang": "c"}, {"id": "a" * 32, "lang": "c"},
            {"id": "../x", "lang": "c"}, {"id": "a/b", "lang": "c"}, {"id": "sco", "lang": "c"},
            {"id": "game", "lang": "cpp"}, {"id": "ok", "lang": "rust"}, {"id": "ok"},
            {"id": "ok", "lang": "c", "services": ["Game.Actors"]},
            {"id": "ok", "lang": "c", "services": ["game..actors"]},
            {"id": "ok", "lang": "c", "services": ["game.actors", "game.actors@1.2"]},
            {"id": "ok", "lang": "c", "services": ["game.actors@1"]},
            {"id": "ok", "lang": "c", "services": ["game.actors@70000.0"]},
            {"id": "ok", "lang": "c", "services": ["s%d" % i for i in range(17)]},
            {"id": "ok", "lang": "c", "name": 'a"b'}, {"id": "ok", "lang": "c", "author": "x\ny"},
            {"id": "ok", "lang": "c", "with_menu": "yes"}, {"id": "ok", "lang": "c", "extra": 1},
        ]
        for s in bad:
            with self.assertRaises(gen.SpecError, msg=repr(s)):
                gen.render_plugin(s)
        for p in ("", "/abs", "../up", "a/../b", "a\\b", "c:x", "a//b"):
            with self.assertRaises(gen.SpecError, msg=p):
                gen.check_relative(p)


class Cli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def cli(self, *args):
        return run([sys.executable, TOOL, *args])

    def test_writes_into_missing_or_empty_folder(self):
        out = os.path.join(self.tmp, "new", "my_radar")
        r = self.cli("--id", "my_radar", "--lang", "c", "--out", out)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(sorted(os.listdir(out)), ["CMakeLists.txt", "my_radar.c", "plugin.ini"])
        empty = os.path.join(self.tmp, "empty")
        os.mkdir(empty)
        r = self.cli("--id", "other", "--lang", "cpp", "--with-menu", "--out", empty)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_refuses_unsafe_outputs(self):
        full = os.path.join(self.tmp, "full")
        os.mkdir(full)
        with open(os.path.join(full, "keep.txt"), "w") as f:
            f.write("mine")
        a_file = os.path.join(self.tmp, "file")
        with open(a_file, "w") as f:
            f.write("x")
        link = os.path.join(self.tmp, "link")
        os.symlink(os.path.join(self.tmp, "elsewhere"), link)
        for out in (full, a_file, link):
            r = self.cli("--id", "my_radar", "--lang", "c", "--out", out)
            self.assertEqual(r.returncode, 2, out)
            self.assertIn("sco-new-plugin:", r.stderr)
        with open(os.path.join(full, "keep.txt")) as f:
            self.assertEqual(f.read(), "mine")
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "elsewhere")))
        r = self.cli("--id", "bad-id", "--lang", "c", "--out", os.path.join(self.tmp, "nope"))
        self.assertEqual(r.returncode, 2)
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "nope")))


@unittest.skipUnless(CMAKE, "cmake not found: the build checks need CMake 3.20+ and a C/C++20 compiler")
class Build(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def build_and_check(self, pid, lang, with_menu, services=()):
        out = os.path.join(self.tmp, pid)
        args = [sys.executable, TOOL, "--id", pid, "--lang", lang, "--out", out]
        if with_menu:
            args.append("--with-menu")
        if services:
            args += ["--services", ",".join(services)]
        r = run(args)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        build = os.path.join(out, "build")
        steps = [
            [CMAKE, "-S", out, "-B", build, "-DSCO_SDK=" + HERE, "-DCMAKE_BUILD_TYPE=Release"],
            [CMAKE, "--build", build, "--config", "Release"],
            [CMAKE, "--build", build, "--config", "Release", "--target", "check"],
        ]
        for step in steps:
            r = run(step)
            self.assertEqual(r.returncode, 0, f"{' '.join(step)}\n{r.stdout}\n{r.stderr}")
        check = r.stdout
        self.assertIn("--short-tables", check)
        self.assertEqual(check.count("OK: 0 failure(s)"), 2, check)

        exe = [os.path.join(dp, f) for dp, _, fs in os.walk(build) for f in fs
               if f in ("sco-plugin-check", "sco-plugin-check.exe")]
        self.assertEqual(len(exe), 1, exe)
        stage = os.path.join(build, "check", pid)
        outputs = {}
        for extra in ([], ["--short-tables"]):
            r = run([exe[0], stage, *extra])
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("OK: 0 failure(s)", r.stdout)
            self.assertIn(f"invoke  {pid}.ping -> ok \"pong", r.stdout)
            outputs[bool(extra)] = r.stdout
        self.assertIn("1.0 table", outputs[True])
        if with_menu:
            self.assertIn(f"widgets panel {pid}.main", outputs[False])
            self.assertIn("widgets event", outputs[False])
            self.assertNotIn(f"widgets panel {pid}.main", outputs[True])
        for name in (s.partition("@")[0] for s in services):
            if not (with_menu and name == "sco.widgets"):
                self.assertIn(f"service {name}", outputs[False])
        return outputs

    def test_c(self):
        self.build_and_check("scaf_c", "c", False)

    def test_c_menu(self):
        self.build_and_check("scaf_c_menu", "c", True, ("game.actors", "sco.widgets"))

    def test_cpp(self):
        self.build_and_check("scaf_cpp", "cpp", False, ("game.actors@1.1",))

    def test_cpp_menu(self):
        self.build_and_check("scaf_cpp_menu", "cpp", True, ("sco.widgets",))


if __name__ == "__main__":
    unittest.main(verbosity=2)
