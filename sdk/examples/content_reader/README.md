# Content reader (native C)

A command-free, read-only `sco.content` 1.0 consumer using only public headers.
The host snapshots enabled lists/scripts before `sco_plugin_load`. This example
reads the first nonempty list as exact bytes and frees its own buffer; it executes no
scripts and imports no product base files. An empty index is normal.

Build a shared module against extracted core SDK headers (Windows x64: name it
`content_reader.dll`), check the folder with `sco-plugin-check`, then install it
under `data/plugins/content_reader/`. Requires a host containing the new service;
SDK 2.0.0's existing release does not contain it. Native plugins are trusted code.
The focused `test_content` compiles a development module with copied public
headers and loads it through the real neutral loader. No game proof is claimed.
C#/Lua are unsupported in this slice. See `sdk/docs/data-packs.md` in the source
repository for the full enumeration/read and restart contract.
