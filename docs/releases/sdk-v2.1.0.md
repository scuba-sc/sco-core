# sco-core SDK 2.1.0 (release notes)

Tag `sdk-v2.1.0`, combined zip `sco-sdk-2.1.0.zip`. **Published on 2026-10-13 at 22:42:09 UTC:** [SDK 2.1.0 release and assets](https://github.com/scubamount/sco-core/releases/tag/sdk-v2.1.0). The tag peels to approval commit `8ef24243b4b8ee45fb49bde4192bdbdfa7473ace`, which adds only the signed approval record (`docs/legal/release-decisions/sdk-v2.1.0.json` and `.sig`); the published archives were built from source commit `77c328fc088f85336f1241c0c054bc461fe8b64c`. The two identities are distinct. The release publishes all five profiles (combined/legacy, core, core-source, Star Citizen game-sc overlay and the Star Citizen host-source kit), their SBOMs and `SHA256SUMS`. Published assets are immutable approved payloads; a local build with the same version is not proof of byte equality. The release workflow publishes the CHANGELOG section [`## sdk-v2.1.0`](../../CHANGELOG.md#sdk-v210-2026-10-13) as the release's notes; this page is the longer reference behind it. For the newest published SDK, use [latest release](https://github.com/scubamount/sco-core/releases/latest).

<!-- release-scope:start -->
```json
{"tag":"sdk-v2.1.0","sdk_version":"2.1.0","game_pack_version":"0.4.0","plugin_abi":"1.1","profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
```
<!-- release-scope:end -->

## Versions

| What | Version | Where |
|---|---|---|
| Kernel (the SDK) | 2.1.0 (was 2.0.0) | `sdk/VERSION`, tag `sdk-v2.1.0` |
| Plugin ABI | 1.1, unchanged | `SCO_API_MAJOR` / `SCO_API_MINOR` in `sco_api.h` (frozen; 96 pinned items; `tests/abi_v1.c` unchanged) |
| Star Citizen game pack | 0.4.0 (was 0.3.0) | `SCO_GAME_PACK_VERSION` in `sc_game_pack.h` |
| Verified game build | 4.10.196.36804, PE timestamp `0x6ac64e6f` | rows verified by sigcheck; in-game verification pending |

`VERSIONS.txt` in the zip states the same lines, generated from the headers by `sdk/package.py`.

## Coordinates and distribution

- Core SDK, combined/legacy SDK and the neutral core-source kit: 2.1.0.
- Optional game-sc public overlay: 0.4.0, following the additive-service minor
  rule in [game-pack policy](../game-pack.md). Its compatibility descriptor binds
  the exact core 2.1.0 archive SHA-256; changed bytes retain a new package identity.
- Star Citizen host-source kit (`host-source-sc`): 2.1.0, a standalone source archive
  (see [host-source guide](../host-source-sc.md)), published in this release.
- Frozen plugin ABI: 1.1, unchanged. Individual service versions remain independent.

The five-profile publication follows the
[SDK 2.1.0 host-source scope decision](../legal/release-decisions/2026-10-13-sdk-v2.1.0-host-source.md),
which superseded the earlier [four-profile scope review](../legal/release-decisions/2026-10-12-sdk-v2.1.0-scope.md)
to distribute the exact `host-source-sc` archive. These decisions record maintainer scope
and supply no licence grant or counsel opinion. The exact-byte signed approval binds the
published archive bytes; this page does not authorize a new release or asset replacement.

## Published assets

The release carries an 11-file payload: `SHA256SUMS`, five archives and their five SBOMs.

| Archive | Choose it when |
|---|---|
| `sco-sdk-core-2.1.0.zip` | You need the neutral ABI, host services, native template and generic examples. |
| `sco-sdk-2.1.0.zip` | You also need Star Citizen service headers, DataCore or game data-pack examples. Combined core + game extension. |
| `sco-sdk-game-sc-0.4.0.zip` | Optional extension over its **exact matching** core archive, not a standalone SDK. Check `COMPATIBILITY.json.game-sc` and the required base digest before composing; equal version strings are insufficient. See [archive composition](../../sdk/README.md#zips). |
| `sco-sdk-core-source-2.1.0.zip` | You are embedding/building the host kernel, not following the plugin template path. Build with `-DSCO_GAME_SC=OFF`; no game implementation. See [source-kit instructions](../../sdk/README.md#build-the-kernel-source-kit). |
| `sco-sdk-host-source-sc-2.1.0.zip` | You are building the Star Citizen host source standalone (`-DSCO_GAME_SC=ON`). See the [host-source guide](../host-source-sc.md). |

## Engineering scope and limits

The [CHANGELOG `## sdk-v2.1.0` section](../../CHANGELOG.md#sdk-v210-2026-10-13) preserves the
complete engineering entries and their limits: additive game contracts, renderer-neutral
widgets and language adapters, content access and default-off diagnostics, plus host-only
lifecycle/admission foundations. Each entry's historical verification limits are unchanged.

Native Windows and portable synthetic CI fixtures provide no-game engineering evidence,
not engine or gameplay acceptance. Real-game, anti-cheat/official-network,
private-LAN/firewall and GPU renderer acceptance of the new contracts remain unproven.
No offline consumer pin or legacy deletion is part of this release.

## Checking the zip

```sh
sha256sum -c SHA256SUMS
unzip -p sco-sdk-2.1.0.zip sco-sdk-2.1.0/VERSIONS.txt
```

Publication followed the [maintainer release procedure](../legal/release-checklist.md#releasing);
these notes do not authorize a new release or asset replacement.
