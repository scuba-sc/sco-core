# Third-party notices

sco-core includes the following third-party code. Each keeps its own licence, which applies to
that code only; sco-core's own licence (GPL-3.0-only, [LICENSE](LICENSE)) does not replace it.
Versions, source and file hashes are in [docs/legal/third-party-inventory.md](docs/legal/third-party-inventory.md).

## Lua 5.4.8

`plugins/lua/third_party/lua/` (compiled into hosts that run Lua plugins). **Modified:** the loops
in `lstrlib.c` and `ltablib.c` call `SCO_LUA_STEP` to charge the script's step budget, and
`luaconf.h` includes `sco_lua_user.h`. Every changed line is marked `/* sco-lua */`; see
[plugins/lua/README.md](plugins/lua/README.md#limits).

```
Copyright © 1994–2025 Lua.org, PUC-Rio.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## SQLite 3.53.4

`third_party/sqlite/` (the `sco.storage` service). Unmodified. SQLite is in the public domain;
see <https://sqlite.org/copyright.html>. No notice is required; it is listed here for completeness.

## toml++ 3.4.0

`third_party/tomlplusplus/` (DataCore data packs; game pack only). Unmodified.

```
MIT License

Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Dear ImGui 1.92.8 WIP (public headers only)

`sdk/third_party/imgui/` (`imgui.h`, `imconfig.h`; shipped in the SDK so a plugin compiles against
the ImGui the host links). Unmodified.

```
The MIT License (MIT)

Copyright (c) 2014-2026 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Astro Starlight 0.42.6 search component

`site/src/components/Search.astro` adapts the dialog and styles from
[Starlight](https://github.com/withastro/starlight), version 0.42.6.
Modified for task phrase matching, category filters, first-use loading and focus handling. The project changes are recorded under the repository GPL-3.0-only policy; the user confirmed GPL-3.0-only contribution rights with no employer or other-party claim for the local modifications introduced in #245. Exact-file provenance and release review remain pending. The [upstream MIT notice](site/public/starlight-LICENSE.txt) is also included in the built site and linked from search.

```
MIT License

Copyright (c) 2023 [Astro contributors](https://github.com/withastro/starlight/graphs/contributors)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Documentation landing page visual reference

The documentation landing page follows an owner-supplied visual composition, including its
bracket-and-core mark. The export credits "Liquid Background Hero (html-video, Apache-2.0)";
that attribution describes the supplied reference.

The particle model and browser runtime in `site/src/landing/` are local implementations. The
export's scripts, GSAP, HyperFrames integration and video files are not distributed with the site.

## Not affiliated

sco-core is an independent project. It is not affiliated with, endorsed by or sponsored by Cloud
Imperium Games, Roberts Space Industries or any of their affiliates. Star Citizen® and Squadron 42®
are trademarks of Cloud Imperium Rights LLC; they are used here only to say which game the optional
game pack works with.
