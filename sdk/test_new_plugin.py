#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for sdk/tools/sco-new-plugin.py. Run: python3 sdk/test_new_plugin.py

1. render_plugin is pure and refuses bad ids, services and text the same way for every language,
   and the combinations a language can't support (services in Lua); the CLI refuses unsafe outputs.
2. C and C++ plugins, with and without a menu, are generated into a temp folder, configured
   against this sdk/ folder the way an SDK user would (-DSCO_SDK=...), built, and checked by
   sco-plugin-check with and without --short-tables (the generated `check` target, plus a direct
   run whose output is inspected). Lua plugins run their generated `check` target, and
   tools/lua-check.lua when a Lua 5.4 interpreter is on PATH.
3. With SCO_SDK_CSHARP=1 and dotnet on PATH, C# plugins are published with NativeAOT against a copy
   of this sdk/csharp (-p:ScoSdk=...) and checked by sco-plugin-check with and without
   --short-tables. SCO_FAST_AOT=1 passes -p:SCO_FAST_AOT=1.
"""
import importlib.util
import os
import platform
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
DOTNET = shutil.which("dotnet") or ""
CSHARP = os.environ.get("SCO_SDK_CSHARP") == "1"
LANGS = ("c", "cpp", "csharp", "lua")


def find_lua():
    """A Lua 5.4 interpreter on PATH (lua5.4, lua-5.4 or lua), or ""."""
    for name in ("lua5.4", "lua-5.4", "lua"):
        exe = shutil.which(name)
        if exe:
            r = subprocess.run([exe, "-v"], capture_output=True, text=True)
            if "Lua 5.4" in r.stdout + r.stderr:
                return exe
    return ""


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


class Render(unittest.TestCase):
    def test_pure_and_deterministic(self):
        s = {"id": "my_radar", "lang": "cpp", "with_menu": True, "services": ["game.actors", "sco.widgets"]}
        a = gen.render_plugin(dict(s))
        self.assertEqual(a, gen.render_plugin(dict(s)))
        self.assertEqual(sorted(a), ["CMakeLists.txt", "my_radar.cpp", "plugin.ini"])
        self.assertEqual(sorted(gen.render_plugin({"id": "x", "lang": "c"})), ["CMakeLists.txt", "plugin.ini", "x.c"])
        self.assertEqual(sorted(gen.render_plugin({"id": "x", "lang": "csharp"})),
                         ["README.md", "XPlugin.cs", "plugin.ini", "x.csproj"])
        self.assertEqual(sorted(gen.render_plugin({"id": "x", "lang": "lua"})), ["CMakeLists.txt", "main.lua", "plugin.ini"])
        for lang in LANGS:
            s = {"id": "my_radar", "lang": lang, "with_menu": True, "services": ["sco.widgets"]}
            files = gen.render_plugin(dict(s))
            self.assertEqual(files, gen.render_plugin(dict(s)))
            for text in list(files.values()) + list(a.values()):
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

    def test_csharp(self):
        files = gen.render_plugin({"id": "my_radar", "lang": "csharp", "with_menu": True,
                                   "services": ["game.actors@1.1", "sco.widgets"]})
        ini = files["plugin.ini"]
        for line in ("id = my_radar\n", "api = 1.0\n", "kind = native\n", "entry = my_radar.dll\n",
                     "; requires = game.actors, sco.widgets\n"):
            self.assertIn(line, ini)
        proj = files["my_radar.csproj"]
        for text in ("<AssemblyName>my_radar</AssemblyName>", "<PublishAot>true</PublishAot>",
                     "<NativeLib>Shared</NativeLib>", "<TargetFramework>net8.0</TargetFramework>",
                     '<ProjectReference Include="$(ScoSdk)/csharp/Sco.Sdk/Sco.Sdk.csproj" />',
                     "<ScoSdk Condition=\"'$(ScoSdk)' == ''\">$(SCO_SDK)</ScoSdk>",
                     '<None Update="plugin.ini" CopyToPublishDirectory="PreserveNewest" />', "Set ScoSdk"):
            self.assertIn(text, proj)
        cs = files["MyRadarPlugin.cs"]
        for text in ("namespace ScoPlugins", "public sealed class MyRadarPlugin : Plugin",
                     '("game.actors", ServiceVersion(1, 1)),', 'new CommandBuilder(this, "my_radar.ping")',
                     "new PanelBuilder()", "_widgets.RemovePanel(PanelId)",
                     'PluginExports.Query("my_radar", "0.1.0", "you")',
                     'PluginExports.Load<MyRadarPlugin>(api, self, "my_radar")',
                     '[UnmanagedCallersOnly(EntryPoint = "sco_plugin_unload")]'):
            self.assertIn(text, cs)
        self.assertNotIn('("sco.widgets"', cs)  # the menu's service, opened by Widgets.Open
        self.assertIn("dotnet publish -c Release -r win-x64 -o out\\my_radar -p:ScoSdk=", files["README.md"])
        plain = gen.render_plugin({"id": "widgets", "lang": "csharp"})["WidgetsPlugin.cs"]
        self.assertIn('reply.Set("pong");', plain)
        for text in ("PanelBuilder", "Widgets _widgets", "AnyTable", "LookupServices"):
            self.assertNotIn(text, plain)

    def test_lua(self):
        files = gen.render_plugin({"id": "lua_radar", "lang": "lua", "with_menu": True, "services": ["sco.widgets"]})
        ini = files["plugin.ini"]
        for line in ("id = lua_radar\n", "api = 1.0\n", "kind = lua\n", "entry = main.lua\n"):
            self.assertIn(line, ini)
        script = files["main.lua"]
        self.assertIn('name = "lua_radar.ping"', script)
        self.assertIn("if sco.widgets and sco.widgets.available() then", script)
        self.assertIn('sco.widgets.set_panel("lua_radar.main", "Lua radar", {', script)
        self.assertNotIn("sco.widgets", gen.render_plugin({"id": "lua_radar", "lang": "lua"})["main.lua"])
        cmake = files["CMakeLists.txt"]
        self.assertIn('sco_add_pack(lua_radar DIR "${CMAKE_CURRENT_SOURCE_DIR}")', cmake)
        self.assertIn('configure_file(main.lua "${_sco_check_dir}/main.lua" COPYONLY)', cmake)
        self.assertIn("add_custom_target(check", cmake)
        self.assertNotIn("sco_add_plugin", cmake)
        self.assertNotIn("--short-tables", cmake)  # nothing native: the runtime is the host's

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
            for lang in LANGS:  # the same id, service, text and key rules in every language
                spec = dict(s, lang=lang) if s.get("lang") in ("c", "cpp") else s
                with self.assertRaises(gen.SpecError, msg=repr(spec)):
                    gen.render_plugin(spec)
        # What one language can't do: Lua has no service lookup (sco.widgets is the menu's own).
        for s in ({"id": "ok", "lang": "lua", "services": ["game.actors"]},
                  {"id": "ok", "lang": "lua", "services": ["sco.widgets"]},
                  {"id": "ok", "lang": "lua", "with_menu": True, "services": ["sco.widgets", "sco.storage@1.0"]}):
            with self.assertRaises(gen.SpecError, msg=repr(s)) as ctx:
                gen.render_plugin(s)
            self.assertIn("lang lua can't look up services", str(ctx.exception))
        self.assertIn("main.lua", gen.render_plugin({"id": "ok", "lang": "lua", "with_menu": True,
                                                     "services": ["sco.widgets"]}))
        with self.assertRaises(gen.SpecError) as ctx:
            gen.render_plugin({"id": "ok", "lang": "rust"})
        self.assertIn("use c, cpp, csharp or lua", str(ctx.exception))
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
        out = os.path.join(self.tmp, "cs")
        r = self.cli("--id", "my_radar", "--lang", "csharp", "--with-menu", "--services", "game.actors", "--out", out)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(sorted(os.listdir(out)), ["MyRadarPlugin.cs", "README.md", "my_radar.csproj", "plugin.ini"])
        self.assertIn("dotnet publish", r.stdout)
        out = os.path.join(self.tmp, "lua")
        r = self.cli("--id", "my_radar", "--lang", "lua", "--with-menu", "--out", out)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(sorted(os.listdir(out)), ["CMakeLists.txt", "main.lua", "plugin.ini"])
        self.assertIn("lua-check.lua", r.stdout)
        r = self.cli("--id", "nope", "--lang", "lua", "--services", "game.actors", "--out", os.path.join(self.tmp, "nope"))
        self.assertEqual(r.returncode, 2)
        self.assertIn("lang lua can't look up services", r.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "nope")))

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

    def lua_check(self, pid, with_menu):
        out = os.path.join(self.tmp, pid)
        r = run([sys.executable, TOOL, "--id", pid, "--lang", "lua", "--out", out] + (["--with-menu"] if with_menu else []))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        build = os.path.join(out, "build")
        for step in ([CMAKE, "-S", out, "-B", build, "-DSCO_SDK=" + HERE, "-DCMAKE_BUILD_TYPE=Release"],
                     [CMAKE, "--build", build, "--config", "Release", "--target", "check"]):
            r = run(step)
            self.assertEqual(r.returncode, 0, f"{' '.join(step)}\n{r.stdout}\n{r.stderr}")
        self.assertIn(f"id={pid} kind=lua api=1.0 entry=main.lua", r.stdout)
        self.assertEqual(r.stdout.count("OK: 0 failure(s)"), 1, r.stdout)
        lua = find_lua()
        if lua:  # the script itself, against lua-check's stand-in sco table (no sco.widgets there)
            r = run([lua, os.path.join(HERE, "tools", "lua-check.lua"), out])
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn(f'invoke  {pid}.ping -> ok "pong"', r.stdout)
            self.assertIn(f"status  {pid}: ", r.stdout)
            self.assertIn("OK: 0 failure(s)", r.stdout)

    def test_lua(self):
        self.lua_check("scaf_lua", False)

    def test_lua_menu(self):
        self.lua_check("scaf_lua_menu", True)


@unittest.skipUnless(CSHARP and DOTNET and CMAKE,
                     "SCO_SDK_CSHARP=1, dotnet and cmake run the C# build (NativeAOT: .NET 8 SDK, and MSVC or clang)")
class CSharpBuild(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        # A copy of the C# layer, so dotnet's bin/ and obj/ stay out of sdk/csharp.
        cls.sdk = os.path.join(cls.tmp, "sdk")
        shutil.copytree(os.path.join(HERE, "csharp", "Sco.Sdk"), os.path.join(cls.sdk, "csharp", "Sco.Sdk"),
                        ignore=shutil.ignore_patterns("bin", "obj"))
        # sco-plugin-check, built the way a generated Lua plugin's `check` target builds it.
        folder = os.path.join(cls.tmp, "checker")
        r = run([sys.executable, TOOL, "--id", "checker", "--lang", "lua", "--out", folder])
        assert r.returncode == 0, r.stderr
        build = os.path.join(folder, "build")
        for step in ([CMAKE, "-S", folder, "-B", build, "-DSCO_SDK=" + HERE, "-DCMAKE_BUILD_TYPE=Release"],
                     [CMAKE, "--build", build, "--config", "Release", "--target", "sco-plugin-check"]):
            r = run(step)
            assert r.returncode == 0, f"{' '.join(step)}\n{r.stdout}\n{r.stderr}"
        exe = [os.path.join(dp, f) for dp, _, fs in os.walk(build) for f in fs
               if f in ("sco-plugin-check", "sco-plugin-check.exe")]
        assert len(exe) == 1, exe
        cls.check = exe[0]

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def publish_and_check(self, pid, with_menu, services=()):
        out = os.path.join(self.tmp, pid)
        args = [sys.executable, TOOL, "--id", pid, "--lang", "csharp", "--out", out]
        if with_menu:
            args.append("--with-menu")
        if services:
            args += ["--services", ",".join(services)]
        r = run(args)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        system = platform.system()
        if system == "Windows":
            rid, ext = "win-x64", ".dll"
        elif system == "Darwin":
            rid, ext = "osx-" + ("arm64" if platform.machine() == "arm64" else "x64"), ".dylib"
        else:
            rid, ext = "linux-x64", ".so"
        publish = os.path.join(self.tmp, "publish", pid)
        cmd = [DOTNET, "publish", os.path.join(out, pid + ".csproj"), "-c", "Release", "-r", rid, "-o", publish,
               "-p:ScoSdk=" + self.sdk]
        if os.environ.get("SCO_FAST_AOT") == "1":
            cmd.append("-p:SCO_FAST_AOT=1")
        r = run(cmd)
        self.assertEqual(r.returncode, 0, f"{' '.join(cmd)}\n{r.stdout}\n{r.stderr}")
        self.assertTrue(os.path.isfile(os.path.join(publish, "plugin.ini")), "the publish copies plugin.ini")
        stage = os.path.join(self.tmp, "stage", pid)  # the folder named after the id, entry = <id>.dll
        os.makedirs(stage)
        shutil.copy(os.path.join(publish, pid + ext), os.path.join(stage, pid + ".dll"))
        shutil.copy(os.path.join(publish, "plugin.ini"), stage)
        outputs = {}
        for extra in ([], ["--short-tables"]):
            r = run([self.check, stage, *extra])
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
                self.assertIn(f"service {name} not available", outputs[False])

    def test_csharp(self):
        self.publish_and_check("scaf_cs", False)

    def test_csharp_menu(self):
        self.publish_and_check("scaf_cs_menu", True, ("game.actors@1.1", "sco.widgets"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
