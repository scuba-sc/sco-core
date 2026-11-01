# Container contents reader

This C plugin reads `game.inventory.contents` 1.0 through public SDK headers.
It registers `container_reader.read <session-container-id>`, re-queries the service
for each call and prints counts, generation and flags only after `SCO_OK`. The
example is the complete runnable code; `container_reader.c` is also discoverable
from the current-source catalogue's `sc_container_contents_v1.snapshot` record.

Use a combined plugin kit, or compose the exact matching `game-sc` overlay over
its `core` kit. Check the archive checksums and `COMPATIBILITY.json` first. A
core-only kit does not include this header or example. For a checkout, use its
`sdk/` directory instead. Source changes do not update an already published kit:
RC2 contains this example but its catalogue does not list it in `get_example`.

From the kit root, with CMake 3.20+ and a 64-bit C/C++ toolchain, run in a POSIX
shell (on Windows use a developer shell and add `-A x64` to both configure
commands; the checker path may be `sdk-build/Release/sco-plugin-check.exe`):

```sh
cmake -S . -B sdk-build
cmake --build sdk-build --config Release --target sco-plugin-check
cmake -S examples/container_reader -B container-build
cmake --build container-build --config Release
cmake --install container-build --config Release --prefix container-out
sdk-build/sco-plugin-check container-out/data/plugins/container_reader
```

On Linux and macOS this produces a development module for the local checker; a
game-loadable plugin requires a Windows x64 build. For a checkout, run the same
commands from `sdk/`. The normal checker run loads and unloads the module but
does not invoke its argument-taking command. The checker supplies no container
provider. For example, adding `--invoke container_reader.read 7` returns
`not_found` and a nonzero exit: that is the stand-in's missing service, not an
empty container or a successful snapshot. `--cap game.inventory.contents.read`
changes capability answers only; it does not install a provider.

In current-source MCP discovery, after connecting the matching kit's server, call:

```json
{"name":"get_symbol","arguments":{"symbol_or_id":"sc_container_contents_v1.snapshot"}}
```

```json
{"name":"get_example","arguments":{"symbol_or_id":"sc_container_contents_v1.snapshot","language":"c"}}
```

A configured `--repo-root` supplies the listed example's contents; without it
MCP returns its path only. Use the matching catalogue and archive rather than
pairing current-source discovery with an older kit. See the kit's
`docs/sdk-mcp-tutorial.md` for initialization and profile setup.

The API is game-thread-only. IDs are opaque and session-local, not persistent
record IDs. The caller initializes `snapshot.size` and `snapshot.count` as
capacity (up to 64); `count` becomes the number copied. Only `SCO_OK` with
`total_seen == 0` establishes an empty snapshot. A zero count with a positive
`total_seen` may mean insufficient capacity; `TRUNCATED` marks omitted items.
Volume is meaningful only when `VOLUME_KNOWN` is set. Handle `SCO_NOT_FOUND`,
`SCO_UNAVAILABLE` and other failures separately and do not treat cleared failure
output as data. The complete contract and language limits are in
`docs/game/container-contents.md` in the kit.

The game-pack default has no production native container adapter and reports the
capability OFF; table discovery alone does not make a read ready. A host may supply
a provider, and scuba-engine has a portable synthetic provider and test fixtures;
fixture IDs and results are not game data. Passing the checker or a synthetic test
does not establish Windows host or in-game behavior.
