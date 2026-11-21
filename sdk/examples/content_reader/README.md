# Content reader (native C)

A command-free, read-only `sco.content` 1.0 consumer using only public headers.
The host snapshots enabled lists/scripts before `sco_plugin_load`. This example
reads the first nonempty list as exact bytes and frees its own buffer; it executes no
scripts and imports no product base files. An empty index is normal.

Build `sco-plugin-check` as in the SDK README, [Build the examples](../../README.md#build-the-examples).
Then, from this folder:

```sh
cmake -S . -B build                                    # Windows: add -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix out
sco-plugin-check out/data/plugins/content_reader --no-load
```

The checker's stand-in host has no `sco.content`, so a full run would fail to load the plugin.
`--no-load` checks the folder only. To use the plugin, copy `out/data/plugins/content_reader` to
`data/plugins/content_reader/` of a host that provides `sco.content`; the host kit from SDK 2.1.0 on has
it. Native plugins are trusted code.

The focused `test_content` compiles a development module with copied public headers and loads it
through the real neutral loader. C# and Lua have no wrapper for it. See `sdk/docs/data-packs.md` in
the source repository for the full enumeration, read and restart contract. Not run in game. See
[game capability status](../../../docs/game/status.md).
