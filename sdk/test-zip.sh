#!/usr/bin/env bash
# Unpacks the SDK zip into an empty folder and builds and checks every example from it alone:
# no file from the repository is used after packaging. CI runs this (.github/workflows/sdk.yml)
# on Linux and Windows (Git Bash + MSVC); it also runs on macOS.
#
#   sdk/test-zip.sh                         # packages the legacy zip first (sdk/package.py), then tests it
#   sdk/test-zip.sh <sco-sdk.zip>           # tests a zip built elsewhere, e.g. CI's artifact
#   sdk/test-zip.sh <core.zip>              # the core SDK alone: no game file may be needed
#   sdk/test-zip.sh <core.zip> <game-sc.zip> # the Star Citizen extension, unpacked over that core
#
# What it checks follows from what is unpacked: the game examples and the C# game tests run when
# their files are there; the core SDK alone must build and pass without them.
#
# Needs python3, cmake, a C and C++20 compiler and Lua 5.4 (lua5.4). SCO_SDK_NO_LUA=1 skips the Lua
# example's run (the Windows CI job, which has no Lua); the job that packages runs it.
# SCO_SDK_CSHARP=1 also runs the C# SDK's test and publishes cs_hello with NativeAOT (needs the
# .NET 8 SDK, and on Windows Visual Studio's linker), then checks it like the C examples.
#
# SCO_SDK_WIDGETS_ONLY=1 <core.zip> builds/checks only widgets + sco-plugin-check from that archive.
# Exit 0 = the selected archive check passed (all examples unless narrowed above).
set -euo pipefail
SDK=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
# Git Bash on Windows: hand native tools (cmake, the checker) C:/ paths, not /tmp ones.
if command -v cygpath >/dev/null; then WORK=$(cygpath -m "$WORK"); fi

PY=${PYTHON:-$(command -v python3 || command -v python)}
if [ $# -ge 1 ]; then
  ZIP=$1
else
  "$PY" "$SDK/package.py" --out "$WORK/dist"
  ZIP=$(ls "$WORK"/dist/sco-sdk-*.zip)
fi
if [ $# -ge 2 ]; then
  # Read descriptors and bind the overlay to the exact archive before extraction or copying.
  "$PY" - "$ZIP" "$2" <<'PY'
import hashlib
import json
import re
import sys
import zipfile


def descriptor(path, role):
    suffix = ".game-sc" if role == "game-sc" else ""
    with zipfile.ZipFile(path) as z:
        names = [n for n in z.namelist() if n.endswith("/COMPATIBILITY.json" + suffix)]
        if len(names) != 1:
            raise ValueError(f"{role}: expected exactly one compatibility descriptor")
        d = json.loads(z.read(names[0]))
    required = {"schema_version", "profile", "core_sdk_version", "plugin_abi_version",
                "game_overlay_version", "source_commits", "base_core_sha256"}
    if set(d) != required or d["schema_version"] != 1 or d["profile"] != role:
        raise ValueError(f"{role}: invalid compatibility descriptor or archive role")
    for field, pattern in (("core_sdk_version", r"[0-9]+\.[0-9]+\.[0-9]+(-[a-z0-9.]+)?"),
                           ("plugin_abi_version", r"[0-9]+\.[0-9]+")):
        if not isinstance(d[field], str) or not re.fullmatch(pattern, d[field]):
            raise ValueError(f"{role}: invalid {field}")
    commits = d["source_commits"]
    if not isinstance(commits, dict) or set(commits) != ({"core", "game-sc"} if suffix else {"core"}):
        raise ValueError(f"{role}: invalid source commits")
    if any(not isinstance(c, str) or not re.fullmatch(r"[0-9a-f]{40}", c) for c in commits.values()):
        raise ValueError(f"{role}: invalid source commit")
    if suffix:
        if not isinstance(d["base_core_sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", d["base_core_sha256"]):
            raise ValueError("game-sc: invalid base core SHA-256")
        if not isinstance(d["game_overlay_version"], str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", d["game_overlay_version"]):
            raise ValueError("game-sc: invalid game overlay version")
    elif d["base_core_sha256"] is not None or d["game_overlay_version"] is not None:
        raise ValueError("core: unexpected overlay fields")
    return d


try:
    core, overlay = descriptor(sys.argv[1], "core"), descriptor(sys.argv[2], "game-sc")
    with open(sys.argv[1], "rb") as f:
        digest = hashlib.sha256()
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
        actual = digest.hexdigest()
    if overlay["base_core_sha256"] != actual:
        raise ValueError(f"base core SHA-256 mismatch: overlay expects {overlay['base_core_sha256']}, supplied {actual}")
    for field in ("core_sdk_version", "plugin_abi_version"):
        if overlay[field] != core[field]:
            raise ValueError(f"{field} mismatch")
    if overlay["source_commits"]["core"] != core["source_commits"]["core"]:
        raise ValueError("base core source commit mismatch")
except (OSError, ValueError, TypeError, KeyError, zipfile.BadZipFile) as e:
    sys.exit(f"incompatible SDK overlay: {e}")
print("zip: exact core/overlay compatibility OK")
PY
fi
verify_zip() {   # verify_zip <zip>: unpack into its own folder; its SHA256SUMS must match and list every file
  local dir="$WORK/z$1" top listed actual sums
  mkdir "$dir"
  "$PY" -m zipfile -e "$2" "$dir"
  top=$(ls -d "$dir"/sco-sdk-*)
  # The overlay profile keeps its own copies (SHA256SUMS.game-sc), so each zip's sums file is its own.
  sums=$(cd "$top" && ls SHA256SUMS* 2>/dev/null || true)
  [ -n "$sums" ] && [ "$(printf '%s\n' "$sums" | wc -l)" -eq 1 ] \
    || { echo "$(basename "$2"): expected exactly one SHA256SUMS* file, found: ${sums:-none}"; exit 1; }
  ( cd "$top" && if sha256sum --version 2>/dev/null | grep -q GNU; then sha256sum -c --quiet "$sums"; else shasum -a 256 -c --quiet "$sums"; fi )
  listed=$(wc -l < "$top/$sums")
  actual=$(cd "$top" && find . -type f ! -name 'SHA256SUMS*' | wc -l)
  [ "$listed" -eq "$actual" ] || { echo "$sums lists $listed files, $(basename "$2") has $actual"; exit 1; }
  echo "zip: $(basename "$2"): $actual files, $sums OK"
  VERIFIED=$top
}
verify_zip 1 "$ZIP"
ROOT=$VERIFIED
# The source profile uses repository layout, not the plugin-example install layout.
if [ -d "$ROOT/src/host" ]; then
  [ $# -eq 1 ] || { echo "a source kit is not an overlay"; exit 1; }
  for path in src/game src/datacore include/sco/game plugins/lua/sco_lua_game.c third_party/tomlplusplus; do
    [ ! -e "$ROOT/$path" ] || { echo "source kit contains excluded implementation: $path"; exit 1; }
  done
  "$PY" "$SDK/package.py" --profile core-source --check-installed-headers "$ROOT"
  cmake -S "$ROOT" -B "$WORK/build" -DCMAKE_BUILD_TYPE=Debug \
    -DSCO_GAME_SC=OFF -DSCO_BUILD_TESTS=ON
  cmake --build "$WORK/build" --config Debug --parallel
  # Focused exercise of host/runtime and the no-git, frozen-header path.
  ctest --test-dir "$WORK/build" -C Debug --output-on-failure \
    -R '^(abi_freeze|test_host|test_runtime|test_plugins|host_sim_examples)$'
  echo "sdk source zip: kernel built and synthetic host tests passed without game implementation"
  exit 0
fi
if [ $# -ge 2 ]; then
  # The extension goes over the core: no file of one may overwrite a file of the other, and each
  # zip's own VERSIONS.txt / SHA256SUMS survives (the overlay profile's are named after it).
  # LICENSE is the one file both zips ship, from one source; it must be byte-identical.
  verify_zip 2 "$2"
  ( cd "$VERIFIED" && find . -type f ) | while read -r f; do
    if [ -e "$ROOT/$f" ]; then
      cmp -s "$VERIFIED/$f" "$ROOT/$f" || { echo "$(basename "$2") overwrites $f of the core SDK"; exit 1; }
    fi
  done
  ( cd "$VERIFIED" && find . -type f -exec sh -c 'mkdir -p "$0/$(dirname "$1")" && cp "$1" "$0/$1"' "$ROOT" {} \; )
  # Both archives' metadata is there, under each archive's own names.
  for m in VERSIONS.txt SHA256SUMS COMPATIBILITY.json ATTRIBUTIONS.json SBOM.spdx.json; do
    [ -f "$ROOT/$m" ] || { echo "the core SDK's $m is gone after the overlay"; exit 1; }
    [ -f "$ROOT/$m.game-sc" ] || { echo "$(basename "$2") did not keep its own $m.game-sc"; exit 1; }
  done
  echo "zip: $(basename "$2") unpacked over $(basename "$ZIP")"
fi
GAME=0
[ -e "$ROOT/include/sc_game_pack.h" ] && GAME=1
if [ $GAME = 0 ]; then
  # The core SDK alone: nothing in it may name a game header or wrapper it doesn't ship.
  if grep -rlE '#include *[<"](sc_(actors|vehicles|entities|world|creative|spawn|spatial|game_events|game_pack)\.h|sco_datacore\.h|scosdk/(datacore|game/)[^>"]*)[>"]' "$ROOT" --include='*.h' --include='*.hpp' --include='*.c' --include='*.cpp'; then
    echo "the core SDK includes a header of the Star Citizen extension"; exit 1
  fi
  if grep -rlE 'Sco\.Sdk\.Game|ScoDatacore|GameAbi' "$ROOT" --include='*.cs'; then
    echo "the core SDK's C# names a type of the Star Citizen extension"; exit 1
  fi
  echo "core: no game header, wrapper or C# type referenced"
fi

# Build everything from the unpacked SDK, as a modder would. Inspect CMake's
# resolved target model (also available with Visual Studio, unlike compile_commands).
mkdir -p "$WORK/build/.cmake/api/v1/query"
: > "$WORK/build/.cmake/api/v1/query/codemodel-v2"
cmake -S "$ROOT" -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release -DSCO_SDK_INCLUDE_DIR="$ROOT/include"
"$PY" - "$WORK/build" "$ROOT" "$SDK" <<'PY'
import json
import os
from pathlib import Path
import sys

build, root, checkout_sdk = map(os.path.realpath, sys.argv[1:])
reply = Path(build, ".cmake/api/v1/reply")
targets = list(reply.glob("target-*.json"))
if not targets:
    sys.exit("archive-only include violation: CMake target model unavailable")
for target in targets:
    data = json.loads(target.read_text())
    for group in data.get("compileGroups", []):
        for inc in group.get("includes", []):
            path = os.path.realpath(inc["path"])
            if os.path.commonpath([path, root]) != root:
                sys.exit(f"archive-only include violation: {data['name']} includes outside extracted SDK: {path}")
        for flag in group.get("compileCommandFragments", []):
            # CMake represents CFLAGS/CXXFLAGS as raw fragments, not include objects.
            fragment = flag["fragment"].replace("\\", "/")
            checkout = os.path.dirname(checkout_sdk).replace("\\", "/")
            if checkout in fragment:
                sys.exit(f"archive-only include violation: {data['name']} has a checkout compiler flag")
print("zip: native target includes resolve only inside extracted SDK")
PY
# Focused archive proof: build only the checker + widget target. Normal CI also
# exercises this example below after installing the complete SDK example set.
check_widgets() {
  "$1" "$2" | tee "$WORK/widgets.txt"
  "$PY" - "$WORK/widgets.txt" <<'PY'
import re
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
kinds = {int(n) for n in re.findall(r'widgets node widgets\.main .*?kind=(\d+)\(', text)}
events = re.findall(r'^  widgets event widgets\.main .*?kind=(\d+)\(', text, re.M)
assert kinds == set(range(1, 11)), f"widget kind coverage: {sorted(kinds)}"
assert sorted(map(int, events)) == [2, 3, 4, 5, 6, 7], f"event coverage: {events}"
assert len(re.findall(r'widgets: event ', text)) == 6, "plugin callback logs missing"
assert 'widgets callback widgets.main node=4 returned logs=2 invokes=1' in text, "callback-to-command route missing"
assert 'widgets remove widgets.main' in text, "remove_panel not exercised"
assert 'widgets unload removed=0 retained=0 callbacks=0' in text, "unload cleanup missing"
assert 'OK: 0 failure(s)' in text, "checker failed"
print("zip: widgets all 10 kinds, 6 callbacks, command reentrancy and unload OK")
PY
}
if [ "${SCO_SDK_WIDGETS_ONLY:-0}" = 1 ]; then
  [ $# -eq 1 ] || { echo "widget-only check requires one plugin SDK archive"; exit 1; }
  [ -f "$ROOT/include/sco_widgets.h" ] && [ -f "$ROOT/include/scosdk/widgets.hpp" ]
  [ ! -e "$ROOT/include/sco/widgets_imgui.h" ] && [ ! -e "$ROOT/src/widgets/imgui_adapter.cpp" ] \
    || { echo "plugin SDK contains host-only adapter"; exit 1; }
  cmake --build "$WORK/build" --config Release --target sco-plugin-check widgets
  mkdir -p "$WORK/plugin/widgets"
  cp "$ROOT/examples/widgets/plugin.ini" "$WORK/plugin/widgets/"
  BUILT="$WORK/build"
  [ -f "$BUILT/Release/widgets.dll" ] && BUILT="$BUILT/Release"
  cp "$BUILT/widgets.dll" "$WORK/plugin/widgets/"
  CHECK="$BUILT/sco-plugin-check"
  [ -f "$CHECK.exe" ] && CHECK="$CHECK.exe"
  check_widgets "$CHECK" "$WORK/plugin/widgets"
  exit 0
fi
# The external compile proof uses compiler dependency output, not the target
# include-directory model above. Overlay headers use the composed profile closure.
if [ $# -ge 2 ]; then
  PROFILE=game-sc
else
  PROFILE=$("$PY" -c 'import json,sys; print(json.load(open(sys.argv[1]))["profile"])' "$ROOT/COMPATIBILITY.json")
fi
"$PY" "$SDK/package.py" --profile "$PROFILE" --check-installed-headers "$ROOT"
# --config is for multi-config generators (Visual Studio); single-config ones ignore it.
cmake --build "$WORK/build" --config Release
cmake --install "$WORK/build" --config Release --prefix "$WORK/out"
CHECK="$WORK/out/bin/sco-plugin-check"
P="$WORK/out/data/plugins"

check_widgets "$CHECK" "$P/widgets"
"$CHECK" "$P/hello"
"$CHECK" "$P/hello" --cap teleport --invoke hello.wave "Pilot One" | tee "$WORK/hello.txt"
grep -q 'invoke  hello.wave -> ok "Hello, Pilot One"' "$WORK/hello.txt"
# The C++20 example (include/scosdk/): a command with an argument, one without, and its service.
"$CHECK" "$P/cpp_hello" --invoke cpp_hello.wave "Pilot One" | tee "$WORK/cpp_hello.txt"
grep -q 'invoke  cpp_hello.wave -> ok "Hello, Pilot One"' "$WORK/cpp_hello.txt"
grep -q 'invoke  cpp_hello.ticks -> ok "3 ticks"' "$WORK/cpp_hello.txt"
grep -q 'service cpp_hello.greeter 1.0' "$WORK/cpp_hello.txt"
"$CHECK" "$P/my_plugin" | tee "$WORK/template.txt"
grep -q 'invoke  my_plugin.ping -> ok "pong"' "$WORK/template.txt"
if [ $GAME = 1 ]; then
  "$CHECK" "$P/travel_pack"
  "$CHECK" "$P/quantum_pack" | tee "$WORK/quantum_pack.txt"
  grep -q 'content datacore/eos.toml' "$WORK/quantum_pack.txt"
else
  [ ! -e "$P/travel_pack" ] && [ ! -e "$P/quantum_pack" ] || { echo "the core SDK installed a game example"; exit 1; }
fi
"$CHECK" "$P/greeter"
"$CHECK" "$P/notebook"

# The C# layer (csharp/Sco.Sdk): its layout and fake-host test, then cs_hello published with
# NativeAOT into a native library named cs_hello.dll (also on Linux, as sco_add_plugin names it).
if [ "${SCO_SDK_CSHARP:-0}" = 1 ]; then
  # The NativeAOT build finds Visual Studio with vswhere, which Git Bash may not have on PATH.
  VSW="/c/Program Files (x86)/Microsoft Visual Studio/Installer"
  if [ -d "$VSW" ]; then PATH="$PATH:$VSW"; fi
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) RID=win-x64; LIB=cs_hello.dll ;;
    Darwin) RID=osx-$(uname -m | sed 's/x86_64/x64/'); LIB=cs_hello.dylib ;;
    *) RID=linux-x64; LIB=cs_hello.so ;;
  esac
  dotnet run -c Release --project "$ROOT/csharp/Sco.Sdk.Tests" | tee "$WORK/cs_tests.txt"
  grep -q '^Sco.Sdk.Tests: OK' "$WORK/cs_tests.txt"
  # The game half (GamePins.cs, GameFakeHost.cs) runs exactly when the extension is unpacked.
  if [ $GAME = 1 ]; then grep -qE '^layout: 3[0-9]{2} pins' "$WORK/cs_tests.txt"
  else grep -qE '^layout: 1[0-9]{2} pins' "$WORK/cs_tests.txt"; fi || { echo "C#: the pin count doesn't match the profile"; exit 1; }
  dotnet publish "$ROOT/examples/cs_hello/cs_hello.csproj" -c Release -r "$RID" -o "$WORK/cs_publish"
  mkdir -p "$WORK/cs/cs_hello"
  cp "$WORK/cs_publish/$LIB" "$WORK/cs/cs_hello/cs_hello.dll"
  cp "$ROOT/examples/cs_hello/plugin.ini" "$WORK/cs/cs_hello/"
  "$CHECK" "$WORK/cs/cs_hello" --invoke cs_hello.wave "Pilot One" | tee "$WORK/cs_hello.txt"
  grep -q 'invoke  cs_hello.wave -> ok "Hello, Pilot One"' "$WORK/cs_hello.txt"
  grep -q 'invoke  cs_hello.ticks -> ok "3 ticks"' "$WORK/cs_hello.txt"
  grep -q 'service cs_hello.greeter 1.0' "$WORK/cs_hello.txt"
  echo "C#: cs_hello ($RID, NativeAOT) checked"
else
  echo "C#: skipped (SCO_SDK_CSHARP=1 runs it)"
fi

# sco-plugin-check reads plugin.ini with the host's rules (src/plugins/manifest.cpp): it refuses
# what the game refuses and accepts what the game accepts.
CASES="$WORK/cases"
case_ini() {   # case_ini [key=value]...: a lua plugin.ini with these keys changed, in $CASES/<id>
  local id=bad name=Case version=1.0 author=Someone api=1.0 kind=lua entry=main.lua requires=teleport extra= kv
  for kv in "$@"; do local "$kv"; done
  mkdir -p "$CASES/$id"
  : > "$CASES/$id/main.lua"
  printf 'id = %s\nname = %s\nversion = %s\nauthor = %s\napi = %s\nkind = %s\nentry = %s\nrequires = %s\n%s\n' \
    "$id" "$name" "$version" "$author" "$api" "$kind" "$entry" "$requires" "$extra" > "$CASES/$id/plugin.ini"
  echo "$CASES/$id"
}
refused() {    # refused <what> [key=value]...: the checker must fail on that plugin.ini
  local what=$1 dir code
  shift
  dir=$(case_ini "$@")
  set +e; "$CHECK" "$dir" > "$WORK/case.txt"; code=$?; set -e
  { [ $code -eq 1 ] && grep -q '^FAIL plugin.ini' "$WORK/case.txt"; } || { cat "$WORK/case.txt"; echo "sco-plugin-check accepted $what"; exit 1; }
}
refused "requires = a..b" "requires=a..b"
refused "a capability listed twice" "requires=teleport, teleport"
refused "a plugin requiring itself" "requires=teleport, bad"
refused "a plugin requiring its own service" "requires=bad.svc"
refused "'*' in entry" "entry=main*.lua"
refused "a control character in entry" "entry=main$(printf '\001').lua"
refused "an empty author" "author="
refused "a control character in name" "name=Case$(printf '\177')"
refused "api = 1.0x" "api=1.0x"
refused "api = +1.0" "api=+1.0"
refused "api = -0.0" "api=-0.0"
refused "a plugin.ini over 16 KiB" "extra=;$(head -c 17000 /dev/zero | tr '\0' x)"
refused "a setting default above its max" "extra=[settings]
a = int default 11 max 10"
refused "an unknown setting type" "extra=[settings]
a = integer"
refused "a misspelled setting field" "extra=[settings]
a = bool defualt true"
good=$(case_ini id=good "extra=homepage = a
homepage = b
notes = $(head -c 3000 /dev/zero | tr '\0' x)")
"$CHECK" "$good" > "$WORK/case.txt" || { cat "$WORK/case.txt"; echo "sco-plugin-check refused unknown keys or a long line"; exit 1; }
# requires names capabilities, services (<plugin id>.<name>, sco.storage, game.vehicles) and plugin ids.
svc=$(case_ini id=svcuser "requires=otherplug.svc, otherplug, sco.storage ,  game.vehicles, teleport.spatial")
"$CHECK" "$svc" > "$WORK/case.txt" || { cat "$WORK/case.txt"; echo "sco-plugin-check refused requires naming services and plugin ids"; exit 1; }
grep -q 'needs   otherplug.svc' "$WORK/case.txt"
grep -q 'needs   sco.storage' "$WORK/case.txt"
settings=$(case_ini id=settings "extra=[settings]
speed = int default 5 min 1 max 10 label \"Speed ; fast\" ; comment
mode = enum(easy, hard) default hard")
"$CHECK" "$settings" > "$WORK/case.txt" || { cat "$WORK/case.txt"; echo "sco-plugin-check refused a good [settings] section"; exit 1; }
grep -q '^  setting speed (int)$' "$WORK/case.txt" && grep -q '^  setting mode (enum)$' "$WORK/case.txt" || { cat "$WORK/case.txt"; echo "sco-plugin-check didn't list the settings"; exit 1; }
echo "sco-plugin-check: 15 bad plugin.ini refused, requires naming services and plugin ids, unknown keys, a 3000-byte line and a [settings] section accepted"

if [ "${SCO_SDK_NO_LUA:-0}" = 1 ]; then
  echo "lua-check: skipped (SCO_SDK_NO_LUA=1)"
else
  LUA=$(command -v lua5.4 || command -v lua-5.4 || command -v lua || true)
  { [ -n "$LUA" ] && "$LUA" -v 2>&1 | grep -q 'Lua 5\.4'; } || { echo "need Lua 5.4 (lua5.4) for the Lua example"; exit 1; }
  "$LUA" "$ROOT/tools/lua-check.lua" "$P/greeter" --invoke greeter.greet "Pilot One" true | tee "$WORK/greeter.txt"
  grep -q 'invoke  greeter.greet -> ok "HI, PILOT ONE!"' "$WORK/greeter.txt"
  # lua-check's sandbox and sco table behave like the runtime's (plugins/lua/sco_lua.c).
  mkdir -p "$CASES/parity"
  printf 'id = parity\nname = Parity\nversion = 1.0\napi = 1.0\nkind = lua\nentry = main.lua\n' > "$CASES/parity/plugin.ini"
  cat > "$CASES/parity/main.lua" <<'EOF'
assert(string.dump == nil, "string.dump")
assert(not pcall(setmetatable, {}, { __gc = print }), "__gc")
assert(sco.subscribe("tick", print))
assert(select(2, sco.subscribe("tick", print)) == "bad_arg", "same function twice")
for i = 1, 63 do assert(sco.subscribe("tick", function() return i end)) end
assert(select(2, sco.subscribe("tick", function() end)) == "too_many", "65th function on one event")
sco.register_command{ name = "parity.add", title = "Add", args = {{ name = "a", type = "int" }},
                      fn = function(a) return tostring(a + 1) end }
assert(select(2, sco.invoke("parity.add", 2.0)) == "bad_arg", "int takes no float")
assert(select(2, sco.invoke("parity.add")) == "bad_arg", "argument count")
local ok, reply = sco.invoke("parity.add", 2)
assert(ok and reply == "3", "invoke")
print("parity", true, nil)
EOF
  "$LUA" "$ROOT/tools/lua-check.lua" "$CASES/parity" | tee "$WORK/parity.txt"
  grep -q "\[parity\] info: parity$(printf '\t')true$(printf '\t')nil" "$WORK/parity.txt"
  # The storage example (sco.store): add a note, then list and clear run without arguments.
  "$LUA" "$ROOT/tools/lua-check.lua" "$P/notebook" --invoke notebook.add "buy fuel" | tee "$WORK/notebook.txt"
  grep -q 'invoke  notebook.add -> ok "note 1 saved"' "$WORK/notebook.txt"
  grep -q 'invoke  notebook.list -> ok "1 note: buy fuel"' "$WORK/notebook.txt"
  grep -q 'invoke  notebook.clear -> ok "removed 1 note"' "$WORK/notebook.txt"
fi

# A copied template must build outside the SDK folder with -DSCO_SDK.
cp -R "$ROOT/template" "$WORK/mine"
cmake -S "$WORK/mine" -B "$WORK/mine/build" -DSCO_SDK="$ROOT" -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/mine/build" --config Release

# The Lua and data-pack examples configure and install on their own too: project(... NONE), so
# no compiler is enabled and the 64-bit check must not fire. greeter builds in its own folder,
# under a name other than build or out (those are always left out): that build folder must not be
# installed with the pack, however the platform spells the temp path.
EXAMPLES="greeter notebook"
[ $GAME = 1 ] && EXAMPLES="$EXAMPLES travel_pack quantum_pack"
for ex in $EXAMPLES; do
  if [ $ex = greeter ]; then B="$ROOT/examples/$ex/my-build"; else B="$WORK/$ex-build"; fi
  cmake -S "$ROOT/examples/$ex" -B "$B"
  cmake --install "$B" --config Release --prefix "$WORK/$ex-out"
  "$CHECK" "$WORK/$ex-out/data/plugins/$ex"
  for f in my-build CMakeLists.txt; do
    [ ! -e "$WORK/$ex-out/data/plugins/$ex/$f" ] || { echo "sco_add_pack($ex) installed $f"; exit 1; }
  done
done
echo "sdk zip: every example built and checked from $(basename "$ZIP")${2:+ + $(basename "$2")}"
