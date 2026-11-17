<!-- Used once by tools/release_docs.py --bump to start docs/releases/sdk-v<version>.md; not a block. -->
# sco-core SDK {{sdk_version}} release notes

Planned tag [`{{tag}}`]({{release_page}}), combined archive `{{archive_combined}}`.
The release workflow builds the five profile archives once from `main` and promotes them without
rebuilding; their digests are in the release's `SHA256SUMS`, not here.

<!-- release-scope:start -->
```json
{scope}
```
<!-- release-scope:end -->

## Compatibility

Plugin ABI {{plugin_abi}}; game pack {{game_pack_version}} for Star Citizen build {{game_build}}.

## What is new

The complete list is the [`## {{tag}}` section of the changelog](../../CHANGELOG.md).

## Previous release

[SDK {{previous_version}} release notes](sdk-v{{previous_version}}.md).
