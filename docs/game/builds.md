# Game builds: every patch the game pack was checked against

Each Star Citizen patch can move the code the game pack's signature rows find. This page
records every build the rows were checked against, oldest at the bottom, with the game pack
and SDK releases pinned to it and which rows changed from the build before. Read two
neighbouring sections to see what a patch changed for the SDK.

The table and sections below are generated from [`builds.json`](builds.json) by
`tools/release_docs.py --write`. Edit the JSON, not this page. CI runs `--check`, and the tool
refuses a game pack whose verified build (`SCO_GAME_PACK_VERIFIED_BUILD` in
[`sc_game_pack.h`](../../include/sc_game_pack.h)) is not the newest build in the register. So a
new patch cannot be pinned until it has a row here with its public patch name, its changelist
and what changed.

A build is identified two ways: the public patch name (for example "Alpha 4.10.3") and the
four-part file version that `StarCitizen.exe`, `build_manifest.id` and `game.build` report
(for example 4.10.200.58746). The two numbering schemes differ; the changelist ties them
together, since the launcher shows it in the build string (`4.10.3-LIVE.<changelist>`).

All checks here are static: a row resolves in a `sco-sigcheck` run on that build's executable.
They do not show in-game behaviour. Per-row evidence is in [status](status.md); the game pack's
version policy is in [the game pack guide](../game-pack.md).

<!-- release-block:game-builds -->
| Patch | Game build | Changelist | On LIVE | Game pack | SDK releases |
|---|---|---|---|---|---|
| Alpha 4.10.3\* | 4.10.200.58746 | 13165946 | 2026-11-12 | 0.8.1 | `sdk-v2.4.1` (this release) |
| Alpha 4.10.2 hotfix\* | 4.10.197.2878 | 12913470 | 2026-10-14 | 0.8.0 | `sdk-v2.4.0` |
| Alpha 4.10.2\* | 4.10.196.36804 | 12881860 | 2026-10-09 | 0.3.0, 0.4.0, 0.5.0, 0.6.0 | `sdk-v2.0.0`, `sdk-v2.1.0`, `sdk-v2.2.0`, `sdk-v2.3.0` |
| Alpha 4.10.1\* | 4.10.193.11644 | 12660092 | 2026-09-16 | none (before the verified-build pin) | none |

\* Patch name from community patch coverage of the launcher build string, not yet matched against the official notes.

### Alpha 4.10.3: 4.10.200.58746 (since 4.10.197.2878, Alpha 4.10.2 hotfix)

Rows that changed: `atc.clear_token`, `contracts.hauling_assign`, `offline.restricted_area_impound`, `quantum.drive_input`, `quantum.on_action`.

- Five rows changed layout and were re-anchored, each with a bounds test: ATC clear token, hauling assignment, restricted-area impound and the two quantum action rows. Their updated locators also resolve on 4.10.197.2878.
- Every row resolved in a static `sco-sigcheck` run (347/347). Every other row gives the same result as before on the synthetic parity fixtures (`tests/issue302_valid_parity.py`).
- Hauling semantics on this build are not verified.
- Name: Launcher build string 4.10.3-LIVE.13165946. The game reports branch sc-alpha-4.10.0 and file version 4.10.200.58746.

### Alpha 4.10.2 hotfix: 4.10.197.2878 (since 4.10.196.36804, Alpha 4.10.2)

- No signature row moved: every row resolved in a static `sco-sigcheck` run (317/317). The launcher and CrashHandler are byte-identical to 4.10.196.36804.
- Name: Hotfix of 4.10.2; the patch name stayed 4.10.2.

### Alpha 4.10.2: 4.10.196.36804 (since 4.10.193.11644, Alpha 4.10.1)

Rows that changed: `teleport.to_camera`.

- `teleport.to_camera`: the four camera fields its handler loads moved by 8 bytes. The row's checks now pin the instructions and wildcard the field offset (SDK 1.1.0); it resolves on 4.10.193 and 4.10.196.
- The first build recorded as the game pack's verified build.
- Name: Launcher build string 4.10.2-LIVE.12881860.

### Alpha 4.10.1: 4.10.193.11644

- The first build the signature rows were written against, before the game pack recorded a verified build.
- Name: A deployment log lists 4.10.1-live.12660092; one patch summary gives 12650677 instead.
<!-- /release-block:game-builds -->

## Adding a build

1. Capture the new build and run `tools/sigcheck.sh` on its `StarCitizen.exe`.
2. Re-anchor the rows that fail, each with a bounds test, and list them in `changed_rows`.
   A row whose layout changes on purpose also goes in `INTENTIONAL_LAYOUT_CHANGES` in
   `tests/issue302_valid_parity.py`.
3. Append the build to `builds.json`: patch name, changelist, LIVE date, and in `changes`
   what moved (or that nothing did).
4. Run `tools/release_docs.py --bump X.Y.Z --game-pack A.B.C --game-build <build>`. It refuses
   a build that is not the newest entry in the register.
