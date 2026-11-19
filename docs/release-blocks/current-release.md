<!-- Template for the release-block named current-release; edit here, then run tools/release_docs.py --write. -->
SDK **{{sdk_version}}**{{candidate_label}} uses Star Citizen game pack **{{game_pack_version}}** (checked against Star Citizen
{{game_patch}}, build {{game_build}}; see [game builds]({{root}}docs/game/builds.md)) and plugin ABI **{{plugin_abi}}**. Download it from the
[`{{download_tag}}` release page]({{release_page}}) and verify each archive against that release's
`SHA256SUMS`; the [release notes]({{notes_url}}) list what changed. The previous release is
[SDK {{previous_version}}]({{previous_notes_url}}).
