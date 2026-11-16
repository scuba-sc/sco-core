<!-- Template for the release-block named archives; edit here, then run tools/release_docs.py --write. -->
| Archive | Choose it when |
| --- | --- |
| `{{archive_core}}` | You need the neutral ABI, host services, native template and generic examples. |
| `{{archive_combined}}` | You also need Star Citizen service headers, DataCore or game data-pack examples: core plus the game extension in one archive, with the MCP discovery catalogue. |
| `{{archive_game_sc}}` | Optional extension over its **exact matching** core archive (`{{archive_core}}`), not a standalone SDK. Check `COMPATIBILITY.json.game-sc` and the required base digest before composing; equal version strings are insufficient. See [archive composition]({{root}}sdk/README.md#zips). |
| `{{archive_core_source}}` | You are embedding or building the host kernel, not following the plugin template path. See [source-kit instructions]({{root}}sdk/README.md#build-the-kernel-source-kit). Do not overlay it with a plugin SDK. |
| `{{archive_host_source}}` | You are building the Star Citizen host source standalone, not a plugin. See the [host-source guide]({{root}}docs/host-source-sc.md). Do not overlay it with a plugin SDK. |
