# Third-party inventory

Vendored code in sco-core, with enough to check it against its upstream. Notices:
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md). Policy: [IP_POLICY.md](../../IP_POLICY.md).

| Component | Path | Version | Licence (SPDX) | Upstream | Integrity | Local changes |
|---|---|---|---|---|---|---|
| Lua | `plugins/lua/third_party/lua/` | 5.4.8 (2025-05-21) | `MIT` | <https://www.lua.org/ftp/lua-5.4.8.tar.gz> | Tarball sha256 `4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae` (matches lua.org's list, checked 2026-10-08; `VERSION`) | Yes: `SCO_LUA_STEP` in `lstrlib.c` and `ltablib.c` loops, `sco_lua_user.h` included by `luaconf.h`; every line marked `/* sco-lua */`. `lua.c`, `luac.c` and the Makefile left out |
| SQLite | `third_party/sqlite/` | 3.53.4 | `blessing` (public domain) | <https://sqlite.org/2026/sqlite-amalgamation-3530400.zip> | `sqlite3.c` sha256 `b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189`, `sqlite3.h` sha256 `919e7f2e8ed1d8f56ac17b412b8971c76aa5d1a879752cc6058f75e7d5910e1d` (`README.md`) | None |
| toml++ | `third_party/tomlplusplus/` | 3.4.0 | `MIT` | <https://github.com/marzer/tomlplusplus> | `toml.hpp` sha256 `6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e`, `LICENSE` sha256 `529bc3900a9571e49db285b0df432397e70b881cc3bf48de6667ae74ff4b06d8`; checked by `tools/test.sh` | None |
| Dear ImGui (public headers) | `sdk/third_party/imgui/` | 1.92.8 WIP (`IMGUI_VERSION_NUM` 19277) | `MIT` | <https://github.com/ocornut/imgui>; copied from sc-offline `src/third_party/imgui/` at `1ee0727cc63866a95e709ae61dd5dd355b511253` | sc-offline CI compares the two copies | None |
| Starlight search adaptation | `site/src/components/Search.astro` | 0.42.6 | upstream `MIT`; project #245 modifications: user-confirmed `GPL-3.0-only` contribution rights, no employer or other-party claim | [component at upstream commit `afe57bbb`](https://github.com/withastro/starlight/blob/afe57bbbc254b957b969d581d0f861925035bdcf/packages/starlight/src/components/Search.astro); [upstream MIT license](https://github.com/withastro/starlight/blob/afe57bbbc254b957b969d581d0f861925035bdcf/LICENSE) | upstream component sha256 `30d6549c54409a9e92ec9734901946d409a408635e8876b18940285f21c62062`; adapted file sha256 `7ab3f852449da5eacd41b8c8cf6cc24a1173248f1125c82cb3447a3850d9ebb6`; MIT license sha256 `a0ee39c239b80f632b663fc02a44893be6c8489815a776e231a2fff17c12ef42` | Modified: task phrase matching, category filters, loading and focus handling, attribution and a license link. Local changes are bracketed with SCO-LOCAL-CHANGE markers in code; compare against the pinned upstream source. The scoped rights declaration covers the #245 local modifications; exact-file provenance and release review remain pending. |

SQLite's SPDX identifier `blessing` is the SQLite blessing in place of a licence; SQLite is
dedicated to the public domain.

## Not vendored

sco-core links nothing else into its targets. Build tools (CMake, compilers, the .NET SDK for the
C# layer) are not distributed.
