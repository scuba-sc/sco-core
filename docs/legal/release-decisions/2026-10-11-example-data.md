# Example data: synthetic names (F-08, F-09)

Decision: replace the game-derived data in the SDK's two example packs with made-up names that only demonstrate the format
Decided by: scubamount (maintainer)
Date: 2026-10-11
Applies to: `sdk/examples/quantum_pack/datacore/eos.toml`, `sdk/examples/travel_pack/lists/locations.txt` (both in the game-sc and legacy zips)

## What was there

- `eos.toml` (F-08): the record names `EntityClassDefinition.QDRV_RSI_S01_Eos_SCItem` and
  `...QDRV_WETK_S01_Beacon_SCItem`, field paths of Game2.dcb 4.10.193, and two of those records' GUIDs.
- `locations.txt` (F-09): two Star Citizen entity names.

## Options considered

1. **Replace them with synthetic names. Chosen.**
2. Leave both packs out of the release.
3. Record a basis for distributing the game's names (with counsel) and keep them.

Replacing them keeps the examples working and tested (the DataCore fixture in `tests/test_datacore_pack.cpp`
uses the same made-up schema), and it removes the question rather than answering it.

## Result

- Both files are now `reviewed`, `project-authored` in `docs/legal/provenance.json` and `sdk/components.json`.
- The game-sc and legacy zips ship no `needs-review` file.
- The docs and header examples that used the same names follow.

## Not covered

- The game pack itself (`src/game/`, `include/sco/game/`, `tests/fixtures/datacore/`), which names
  game structures and records as interoperability facts. It ships in no SDK zip and is assessed separately.
- Prose in `docs/` that reports measurements against the real game (`docs/datacore.md`'s `info`,
  `extract` and `Patch` sections). These are repository docs, not shipped in a zip.
