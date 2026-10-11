# SDK independence and IP assessment (draft)

**Status: DRAFT for the maintainer and counsel. It approves nothing, and it is not a legal opinion.**
It was written by an engineering agent from machine-checked evidence. Statements about ownership,
rights or law are flagged for a person to decide; none is settled here.

| | |
|---|---|
| Reviewed revision | sco-core `ca2e640a` (`main`, after PRs #76–#81) |
| Consumer revision | sc-offline `f433c42` (pins sco-core `c133484`, PR #104) |
| SDK version in the tree | 1.2.0 (`sdk/VERSION`); ABI `sco_api.h` 1.1; game pack 0.2.0 |
| Latest published release | `sdk-v1.1.0` (2026-10-09). **Nothing newer is tagged or published**, and this draft doesn't change that |
| Evidence | [evidence/ca2e640/](evidence/ca2e640/): the ip-compliance run's reports, SBOMs and zip hashes |
| Date | 2026-10-11 |
| Since then | The maintainer decided F-08/F-09 (synthetic example data) and F-13 (option A). Both are carried out in sco-core #83; the gate table below shows the status **after #83 merges**. Re-run the evidence on #83's merge commit before relying on it |

## Scope

**Assessed:** the game-neutral core SDK (`sco-sdk-core-1.2.0.zip`), the optional Star Citizen
extension zip (`sco-sdk-game-sc-0.2.0.zip`) as a *package*, the combined legacy zip, and the
repository records behind them.

**Not assessed:** the Star Citizen game pack's implementation (`src/game/`, `include/sco/game/`,
DataCore integration, signature rows), sc-offline's launcher and bootstrap, and any use of the SDK.
A clean core SDK says nothing about those. The plan assesses them separately.

## Artifacts

Built by CI (`ip-compliance` run 38113298363) from `ca2e640`, then rebuilt locally on macOS with
byte-identical results:

| Zip | sha256 | Files | Ships needs-review files |
|---|---|---|---|
| `sco-sdk-core-1.2.0.zip` | `e227362e12b3ba81774e9111e39dbe7c5f6a245e9088be41c4813352c89cb051` | 79 | **none** |
| `sco-sdk-game-sc-0.2.0.zip` | `35d581e4cd6917c96a89f21129093eefe21f50127c921297168e52cda3884d1b` | 40 | `quantum_pack/datacore/eos.toml` (F-08), `travel_pack/lists/locations.txt` (F-09) |
| `sco-sdk-1.2.0.zip` (legacy) | `bf87c4853314d97212753de3fe1646c45e542a8b02b17bc0785bb10a4d782d73` | 116 | the same two |

## Gate status

PASS here means the machine checks pass and the evidence is attached; per the plan, a gate is PASS
only once its reviewer accepts that evidence. Every gate below therefore still needs that sign-off.

| Gate | Core SDK | Game-sc extension | Evidence |
|---|---|---|---|
| **A** Architectural independence | **PASS (machine)**: kernel builds and tests with `SCO_GAME_SC=OFF` without game-pack objects or symbols; the core zip builds and passes alone with no extension header or game C# type (153 C# pins); ABI 1.1 pins unchanged | **PASS (machine)**: unpacked over the core it builds and passes (346 pins) and overwrites no core file; sc-offline builds against it on Windows | `cmake` 38113298326 (kernel-only), `sdk` 38113298231, `test` 38113298350, `core-audit.json`; sc-offline run 38112659589 |
| **B** Source provenance | **PASS (machine), review pending**: every file shipped is `reviewed`; nothing is unresolved | **PASS (machine) after #83, review pending**: the two `needs-review` files (F-08, F-09) are replaced by synthetic data ([decision](release-decisions/2026-10-11-example-data.md)); at `ca2e640` they were BLOCKED | `provenance.json`, `*-audit.json` notes |
| **C** Distribution rights | **PASS (machine), review pending**: the only third-party code shipped is Dear ImGui's two MIT headers, with their licence and notices; no binary or game asset | **PASS (machine) after #83, review pending**: no game-derived data left in the zip | `THIRD_PARTY_NOTICES.md`, `third-party-inventory.md`, artifacts check |
| **D** Licensing accuracy | **PASS (machine) after #83, counsel review pending**: option A chosen ([decision](release-decisions/2026-10-11-plugin-licence.md)); the five statements now describe the project's expectation, not a legal result. Everything else passes: GPL-3.0-only stated explicitly, REUSE 3.3 compliant (482/482 at `ca2e640`), SPDX on every file | Same as core | `plugin-licensing-options.md`, `reuse lint` |
| **E** Automated compliance | **PASS (machine)**: manifest, artifacts, isolation, licences, SBOM, SHA256SUMS, reproducibility, 24 negative tests; `release.yml` enforces the audit | Same | `ip-compliance.yml` run 38113298363 |
| **F** Documentation accuracy | **Needs review**: README, SDK README and CHANGELOG describe core vs. extension; a person must confirm that no claim goes beyond the evidence | Same | this draft, `CHANGELOG.md` |
| **G** Human authorization | **NOT STARTED**: no approval record exists; `release.yml` refuses to publish without one | Same | `release-decisions/` (empty) |

## Findings for a person to decide

1. **F-13, the plugin licence** (Gate D, both zips). **Decided 2026-10-11: option A**, carried
   out in #83. Open: counsel's review of the wording.
2. **F-08 and F-09, the game-derived example data** (Gates B, C, game-sc and legacy only).
   **Decided 2026-10-11: replaced with synthetic names**, carried out in #83. Nothing open.
3. **Copyright holder** (Gate B). The maintainer has stated that the commit identity `yerts` is the
   maintainer. This is recorded in `provenance.json`, but no external evidence has been reviewed.
   The plan requires contributor-rights evidence for project-authored code; with a single
   contributor, that is the maintainer's own affirmation.
4. **Release shape.** `release.yml` still publishes the legacy zip only. Publishing the core zip
   (and the extension) as release assets is a separate decision. The audit and SBOM steps support
   all three profiles.

## What is not verified

- **In game:** sc-offline with sco-core `c133484`, specifically a Lua script reading
  `sco.game.actors` and receiving `game.player.died` through the new adapter registration. CI
  covers this only with stand-in tables.
- The SBOMs' per-file licences come from the provenance record. No scanner checked file contents
  against licence texts beyond the vendored-file hash pins.
- **The game pack** (`src/game/`, 89 needs-review files). It is outside this assessment, as above.

## Not a claim

This release would be an independently authored SDK with appropriately licensed dependencies, as far
as the records and checks above show. It is not a claim that every use of the SDK, or the game pack,
or sc-offline is authorized, and it is not immunity from any claim.
