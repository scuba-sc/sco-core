# Reference snapshots bound to SDK inputs

`site/scripts/snapshot.py` freezes a catalogue and its explicitly referenced
examples against selected SDK ZIPs and a full immutable source commit. It is a
local, standard-library tool using Git and that revision's `sdk/package.py`.
It does **not** approve, sign, deploy or publish anything.

## Inputs and guarantees

- `--source-sha` must identify an existing lowercase 40-hex commit object.
  Branches, tags and short SHAs are not accepted. Sources come from that Git
  revision, not the current dirty working tree. Every exported file and mode is
  checked against that revision's Git blob inventory; export substitutions or
  omissions cannot silently change the bound sources.
- Catalogue source, SDK/game coordinates, every contract record (including
  signatures, language status and example paths), common/service prose, table
  inventory and literal version macros are checked against those sources.
  Repository `blob`/`tree` URLs must use the same immutable commit.
- Select `core`, `core` plus `game-sc`, `core-source`, or `legacy`. Every ZIP
  member and its executable flag must match the committed packaging tool's
  expected selection, transformed documentation and generated metadata. That
  includes versions, compatibility descriptors, attribution, SPDX SBOM and the
  exact checksum inventory; checking a ZIP's self-reported sums alone is not
  sufficient. ZIP directories, duplicate/unsafe members and symlinks are refused.
- A game overlay must bind the **supplied core ZIP hash**, not merely the same
  SDK version. The actual selected ZIP hashes and metadata hashes are frozen.
  Packaging expectations are computed in a temporary tree/in memory; supplied
  artifacts are never rewritten. Different compression is permitted for a
  standalone ZIP, but is a different pinned input and may invalidate its overlay.
- Only examples explicitly named by the canonical records are copied. Each must
  exist in the exact source tree and equal every selected archive's shipped copy
  where present. `shipped_in` records the profile, destination and byte hash;
  an empty list means **source-only**, not an archive compilation/availability
  claim. The catalogue remains the combined contract inventory; choosing core
  alone does not make game services part of that package.

The snapshot is a binding check, not a replacement for compiler-backed
`tools/sdk_catalogue.py build/check`. Signatures are compared to the source's
canonical contracts; re-proving that the contracts agree with compiled headers
and rendered method cells is the catalogue producer's responsibility. Literal
version macros are deliberately fail-closed; a future expression-valued macro
requires an explicit tool update, not an unchecked evaluation.

## Create a preview or an unapproved candidate

Build the catalogue with `tools/sdk_catalogue.py build` and package the selected
profiles with `sdk/package.py` from the intended source revision first. Supply
those actual files, not independently rebuilt replacements for signed inputs:

```sh
python3 site/scripts/snapshot.py create \
  --source-sha "$SOURCE_SHA" \
  --catalogue dist/sdk-catalogue.json \
  --archive core="$CORE_ZIP" \
  --archive game-sc="$GAME_ZIP" \
  --channel candidate --out reference-output
```

Default channel is `preview`. Result directories are separated:

```text
reference-output/previews/sdk-<version>/game-<version>/<source-sha>/
reference-output/candidates/sdk-<version>/game-<version>/<source-sha>/
```

Both channels always say `publication_status: not-approved`. There is no
approved/release channel or unchecked release-tag label. Existing snapshot
coordinates are never overwritten, including an identical regeneration.

Each directory contains:

- `catalogue.json`: the checked catalogue's original bytes;
- `examples/sdk/examples/...`: only its named source examples;
- `inputs.json`: sorted source-relative SHA-256 inventory of packaging inputs,
  selected components, contract/generator inputs and referenced source evidence;
  hashes are not permission to publish those source files;
- `inventory.json`: deterministic coordinates, selected archive/metadata hashes,
  example locations and the complete snapshot file inventory with sizes/hashes.
  It excludes itself to avoid a circular digest. Its SHA-256 is printed by create.

No absolute input paths, timestamps, Git author information or inferred approval
enter these files. For identical source, catalogue bytes and archive bytes,
output bytes are deterministic independent of ZIP argument order or output path.

## Verify frozen artifacts

Keep the inventory SHA-256 **outside** the snapshot, eventually in the approved
canonical candidate/release inventory. Do not calculate it from the received
snapshot and call that approval. Verification also requires the originally
selected ZIPs and access to the exact Git source object:

```sh
python3 site/scripts/snapshot.py verify \
  --snapshot "$SNAPSHOT_DIR" \
  --inventory-sha256 "$PINNED_INVENTORY_SHA256" \
  --archive core="$CORE_ZIP" \
  --archive game-sc="$GAME_ZIP"
```

The verifier checks the external pin, reconstructs expected binding records,
rechecks package inputs and compares every actual file and byte. Missing,
modified, extra or symlinked snapshot files fail. Changing the inventory as well
as the payload still fails the original external pin. Never regenerate a
snapshot after approval/signing and reuse the old pin or approval; new bytes are
new candidate inputs requiring review.

## Narrow local check and remaining acceptance

```sh
python3 site/scripts/test_snapshot.py
```

The focused test file packages core/game fixtures from the immutable source,
checks deterministic creation and real CLI verification, and exercises wrong
source/versions/signature/example/table members, archive tampering, wrong overlay
base, changed/unlisted snapshot files, inventory rehashing and overwrite refusal.
When CMake is available, it unpacks the selected **core ZIP**, configures the
archive's SDK CMake project and builds **only `hello`**. No repository headers are
used for that example build. If CMake is absent, that test explicitly skips;
compilation is then still owed. Snapshots always keep `compile_status:
not-checked`: the local test is not a durable attestation bundled into a candidate.

Still owed before claiming full candidate acceptance:

1. Compile/check **all** referenced examples/snippets from the exact intended
   candidate archives, including C++, C#, Lua and applicable game overlays on the
   supported toolchains; attach results bound to those ZIP hashes. One local C
   example build is not all-example coverage or runtime/in-game validation.
2. Produce/check the catalogue at the candidate SHA with the compiler-backed
   catalogue gate. Package/snapshot inputs must be stable before approval.
3. Integrate the permitted snapshot files and their frozen inventory hash into
   the existing canonical candidate/release inventory. This tool does not change
   `sdk/components.json`, SDK ZIP contents, release schemas or release gates.
   Adapt the existing inventory explicitly rather than claiming this sidecar is
   already signed or shipped. A rendered site needs its own complete output
   inventory; this snapshot is catalogue/examples, not the entire website.
4. Obtain the maintainer's approval for the **same** source, catalogue, archive
   and snapshot hashes, and separately authorize any public export/deployment.
   A passing local verifier is evidence for review, never publication approval.
