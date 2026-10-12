# SDK release checklist

How an SDK release goes from a green `main` to a published GitHub release. Gates A–G and the
stop-ship conditions come from the release-hardening plan; this page says which are checked by a
machine and which need a person. Machine checks collect evidence; they are not a legal opinion
([IP_POLICY.md](../../IP_POLICY.md)).

## Machine-checked (CI)

| Gate | Check | Where |
|---|---|---|
| A, 3 | Kernel builds and tests with `SCO_GAME_SC=OFF`; no game-pack object or symbol in it | `cmake.yml` `kernel-only` |
| A | The core SDK zip builds and passes from its own files; it includes no extension header and names no game C# type | `sdk.yml` (`sdk/test-zip.sh <core.zip>`), `tools/ip_audit.py zip --profile core` |
| A | The Star Citizen extension unpacked over the core builds and passes; overwrites no core file, and keeps its own `VERSIONS.txt.game-sc` / `SHA256SUMS.game-sc` | `sdk.yml` (`sdk/test-zip.sh <core.zip> <game-sc.zip>`) |
| A | ABI v1.1 pins unchanged (`tests/abi_*.c`, C# `Pins.cs`/`GamePins.cs`) | `test.yml`, `sdk.yml` |
| B, 1 | Every tracked file matches a `docs/legal/provenance.json` entry; none `unresolved` | `ip-compliance.yml` (`tools/ip_audit.py repo`) |
| C, 2 | No binary or refused-extension file outside the exact-hash allowlist, in the repository or any zip | `ip-compliance.yml` |
| C, D, 4 | Vendored files match their pinned hashes; licence texts and notices present and shipped; `sdk/components.json` and `provenance.json` agree; REUSE 3.3 compliant | `ip-compliance.yml` (`ip_audit.py repo`, `reuse lint`) |
| E, 5 | SPDX 2.3 SBOM per zip, bound to commit and zip hash, validated | `ip-compliance.yml`, `release.yml` |
| E, 6 | Each profile packaged twice gives the same bytes; each profile's `SHA256SUMS` matches every entry | `ip-compliance.yml`, `sdk/test_package.py` |
| E, 7 | Each check refuses a synthetic defect (52 cases) | `ip-compliance.yml` (`tools/test_ip_audit.py`) |
| G | The release job refuses unless `docs/legal/release-decisions/<tag>.json` validates against `docs/legal/release-approval.schema.json`, names the tagged commit (or its parent, with only release decisions changed), is unexpired and approves publication | `release.yml` (`tools/ip_audit.py release --tag`) |

A failed or skipped mandatory check blocks the release. Nothing in CI creates a tag.

## Needs a person

- [ ] **F:** the CHANGELOG section and the README describe what ships (core vs. extension) and claim no more than the checks show.
- [ ] **B:** every `needs-review` entry the release ships has a decision in `docs/legal/release-decisions/`, or the release leaves that component out. Since the synthetic example data (2026-10-11), no SDK zip ships one; the game pack's entries ship in none.
- [ ] **D:** counsel has reviewed the plugin-licence wording ([option A, chosen 2026-10-11](release-decisions/2026-10-11-plugin-licence.md)), or the release states nothing stronger than it.
- [ ] **G:** the maintainer checks the commit and the zip hashes from the CI run, and qualified counsel reviews any open material legal question.
- [ ] **G:** the maintainer commits the approval record, then tags. Publishing is a separate act from CI going green.

## Signed release manifest (schema version 2)

`release.json` extends the existing
[`release-approval.schema.json`](release-approval.schema.json), not a separate approval schema.
The schema keeps version 1 compatibility through `oneOf`; signed publication verification accepts
only version 2. Unknown properties, duplicate keys at any depth, non-finite JSON numbers and
missing or malformed values are refused. No signing keys belong in the repository.

Required fields:

| Field | Meaning |
|---|---|
| `schema_version` | Integer `2` |
| `tag` | SDK tag, exactly the caller's `--tag` |
| `source.repo`, `source.sha` | `scubamount/sco-core` and the full lowercase 40-hex commit used to build the candidate; equal to the published `--source-sha` or its ancestor with only `docs/legal/release-decisions/` changed |
| `source.dependencies` | Array of `{path, sha}` for every recursive submodule, rooted at the source checkout; exact equality with pinned gitlinks, including nested dependencies |
| `components_manifest_sha256` | SHA-256 of the exact `sdk/components.json` blob at `source.sha` |
| `builder.workflow_path`, `builder.workflow_sha` | Regular `.github/workflows/*.yml` or `*.yaml` file at the pinned workflow commit; this commit may differ from the source checkout commit |
| `builder.run_id`, `builder.run_attempt` | Positive integers, with run id exactly `--run-id` when supplied |
| `artifacts` | Nonempty array of `{name, role, size, sha256, artifact_id}`; roles are `zip`, `source`, `sbom`, `notices`, `evidence`, `checksums`; positive build-artifact id per entry (several files may share an id) |
| `profiles` | Exactly `["core", "game-sc", "legacy"]`, in that order |
| `distribution.status` | Only `approved-for-publication` passes; `internal-only` and `hold` are explicit refusals |
| `approved_by`, `approved_at` | Nonblank maintainer identity and RFC 3339 UTC timestamp (`Z` or `+00:00`); no future approvals; maximum age seven days by default |

The manifest is an inventory, not a replacement for the repository, ZIP, licence and legal gates
above. Each archive keeps its own packager-generated `VERSIONS.txt` and `SHA256SUMS` metadata;
the release-level inventory binds archives, source, SBOMs, notices and evidence together.

The **exact file bytes** are the canonical signed message: do not parse, reformat or regenerate
`release.json` after signing. Keep the manifest and its detached signature outside the payload
directory to avoid a self-referential hash. Payloads are flat directories of safe regular
basenames, never symlinks or nested directories. Every payload file, including the generated
`SHA256SUMS`, must appear exactly once in the manifest, with its actual byte size and SHA-256.

```sh
python3 tools/release_verify.py inventory --payload-dir payload --out payload/SHA256SUMS
# Assemble and review release.json with hashes including payload/SHA256SUMS, then sign it.
ssh-keygen -Y sign -f "$SIGNING_KEY" -n sco-release-v1 release.json
python3 tools/release_verify.py verify \
  --manifest release.json --signature release.json.sig \
  --allowed-signers "$TRUSTED_ALLOWED_SIGNERS" --payload-dir payload \
  --tag "$TAG" --source-sha "$PUBLISHED_SHA" --run-id "$BUILD_RUN_ID" --max-age-days 7
```

Run from the publication checkout. `--source-sha` is the commit being published (the tagged
commit), not necessarily the candidate build commit in `source.sha`. The built commit must equal
the published commit or be its ancestor, with every changed path under
`docs/legal/release-decisions/`. This permits committing the signed approval after building
without accepting source changes. Missing Git objects, failed ancestry/diff commands, non-ancestor
source or any other changed path refuse publication. The pinned source object and initialized
recursive submodules must be available; workflow files and dependencies are checked at the
manifest's pinned commits, never replaced by the approval commit.
The allowed-signers file is supplied explicitly from an independent trust source outside the
checkout (for CI, a protected environment value). It must authorize identity `sco-maintainer`
for namespace `sco-release-v1`; the verifier never chooses a repository signing key or trust file.
`approved_by` is signed attribution, not the SSH principal: authorization always comes from that
external allowed-signers policy. Builder ids are signed provenance, not a live GitHub API query;
the caller supplies the trusted expected run id to bind the downloaded run's artifacts.

Exit `0` means every check passed. Every refusal exits `1`, emits one `refused: ` line per failed
check to stderr and does not echo untrusted values or subprocess output. `--now <RFC3339>` is for
reproducible tests only; publication must use the actual UTC clock. CI runs
`python3 tools/test_release_verify.py` with freshly generated throwaway Ed25519 keys; signature
tests skip with an explicit message only if `ssh-keygen` is absent.

## Legacy approval record (schema version 1)

`docs/legal/release-decisions/<tag>.json`, where `<tag>` is the tag being pushed (for example
`sdk-v1.3.0.json`). It is a JSON object validated against
[`release-approval.schema.json`](release-approval.schema.json) (JSON Schema draft 2020-12) plus
semantic checks `tools/ip_audit.py release --tag <tag>` applies: the decision is `approve` and
`distribution.status` is `approved-for-publication`; `approved_at` precedes `expires_at`, the record
has not expired and no exception has; `release_tag` equals the tag; `source.commit` is the tagged
commit or an ancestor with nothing but release decisions changed since; `profiles` covers every
artifact role (`sdk-core` → `core`, `sdk-game-sc` → `game-sc`, `sdk-legacy` → `legacy`); artifact
names are unique; and, with `--artifacts DIR`, the directory is exactly the listed set (bytes and
sha256 per file).

The version 1 record is **unsigned**: `approver.key_fingerprint` is recorded, but no signature is
verified by `ip_audit.py release`. It remains supported for existing records, not as a substitute
for signed-manifest verification. Version 2 requires the verifier below; the legacy command
refuses it instead of treating a schema-valid manifest as publication authority.

Synthetic example (every value fake: `a`*40 commits, `b`*64 hashes, identity `maintainer`):

```json
{
 "schema_version": 1,
 "decision": "approve",
 "release_tag": "sdk-v1.3.0",
 "approved_at": "2026-10-20T12:00:00Z",
 "expires_at": "2026-11-20T12:00:00Z",
 "approver": {"identity": "maintainer", "key_fingerprint": "SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"},
 "source": {"repository": "scubamount/sco-core", "commit": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "components_sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
            "dependencies": []},
 "build": {"workflow_commit": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "run_id": 1, "run_attempt": 1,
           "artifact_ids": [1],
           "evidence_sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
 "profiles": ["core", "game-sc", "legacy"],
 "artifacts": [
  {"name": "sco-sdk-1.3.0.zip", "role": "sdk-legacy", "bytes": 123456,
   "sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
  {"name": "sco-sdk-1.3.0.zip.sha256", "role": "checksums", "bytes": 83,
   "sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}
 ],
 "distribution": {"status": "approved-for-publication", "scope": ["public GitHub release"],
                  "rights_record_digest": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                  "source_plan_digest": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                  "external_counsel": "not_obtained"},
 "exceptions": []
}
```
