# Reference snapshots

A reference snapshot freezes an SDK catalogue and the examples it names, bound to the exact SDK ZIPs and the source commit they came from. You use it to prove later that a catalogue and its examples match a set of archives. It is a local tool for people who build or review SDK archives. It needs Python, Git and that commit's `sdk/package.py`. Plugin authors do not need it.

Snapshots are never marked approved.

## Create a snapshot

First build the catalogue with `tools/sdk_catalogue.py build` and package the profiles you want with `sdk/package.py`, both from the source commit. Pass those exact files, not rebuilt copies:

```sh
python3 site/scripts/snapshot.py create \
  --source-sha "$SOURCE_SHA" \
  --catalogue dist/sdk-catalogue.json \
  --archive core="$CORE_ZIP" \
  --archive game-sc="$GAME_ZIP" \
  --channel candidate --out reference-output
```

- `--source-sha` is the full lowercase 40-hex commit ID. Branches, tags and short IDs are refused. Sources come from that commit, not from your working tree.
- `--archive PROFILE=ZIP` takes `core`, `core` plus `game-sc`, `core-source` or `legacy`. A `game-sc` overlay binds the hash of the `core` ZIP you supply, not just the same SDK version.
- `--channel` is `preview` (default) or `candidate`. Both write `publication_status: not-approved`.
- `--out` receives one directory per coordinate. A coordinate that exists is never overwritten, even by an identical run:

```text
reference-output/previews/sdk-<version>/game-<version>/<source-sha>/
reference-output/candidates/sdk-<version>/game-<version>/<source-sha>/
```

Each directory contains:

- `catalogue.json`: the checked catalogue, byte for byte.
- `examples/sdk/examples/...`: only the examples the catalogue records name.
- `inputs.json`: a sorted SHA-256 inventory of the packaging inputs, components, generator inputs and referenced source files.
- `inventory.json`: the coordinates, the selected archive and metadata hashes, the example locations, and the sizes and hashes of every other snapshot file. It excludes itself. `create` prints its SHA-256.

The files hold no absolute paths, timestamps or Git author data. The same source, catalogue and archives give the same bytes whatever the ZIP argument order or output path.

## Verify a snapshot

Keep the inventory SHA-256 outside the snapshot, for example in your release notes. A hash computed from the snapshot you received proves nothing about it. Verification also needs the original ZIPs and the Git source commit:

```sh
python3 site/scripts/snapshot.py verify \
  --snapshot "$SNAPSHOT_DIR" \
  --inventory-sha256 "$PINNED_INVENTORY_SHA256" \
  --archive core="$CORE_ZIP" \
  --archive game-sc="$GAME_ZIP"
```

`verify` compares the pin, rebuilds the expected records from the commit, rechecks the packaging inputs, and compares every file and byte. A missing, changed, extra or symlinked file fails. Changing both the payload and the inventory still fails the original pin. If you change a snapshot, create a new one and pin its hash again.

## What is checked

- Every ZIP member and its executable flag must match what that commit's packaging tool would produce, including versions, compatibility descriptors, attribution, the SPDX SBOM and the checksum inventory. ZIP directories, duplicate or unsafe member names and symlinks are refused. A ZIP's own list of sums is not enough.
- The catalogue's source, SDK and game-pack coordinates, every contract record, the service prose, the table inventory and the literal version macros are checked against the commit. Repository `blob` and `tree` URLs must name the same commit.
- Each example must exist in the commit and equal the copy in every selected archive that ships it. An empty `shipped_in` list means the example is source-only.
- Signatures are compared with the source's canonical contracts. That the contracts match the compiled headers is the job of `tools/sdk_catalogue.py build` and `check`. A version macro that is an expression, not a literal, is refused until the tool is updated.

A snapshot does not compile its examples: `compile_status` is always `not-checked`.

## Test the tool

```sh
python3 site/scripts/test_snapshot.py
```

The test packages core and game fixtures from a commit and checks creation, verification and refusals: wrong source, versions, signature, example or table members, tampered archives, a wrong overlay base, changed or unlisted snapshot files, a rehashed inventory and an overwrite. With CMake available it also unpacks the core ZIP, configures the archive's SDK project and builds only `hello`. Without CMake that step is skipped.
