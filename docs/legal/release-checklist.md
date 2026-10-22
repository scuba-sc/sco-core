# SDK release checklist

How an SDK release goes from a green `main` to a published GitHub release. Gates A–G and the
stop-ship conditions come from the release-hardening plan; Gates H (documentation readiness),
I (deployed Pages verification) and J (announcement receipt) cover what the public is told.
This page says which are checked by a machine and which need a person. An SDK release has five
profiles: `core`, `game-sc`, `legacy`, `core-source` and `host-source-sc`. Machine checks collect evidence; they are not a legal opinion
([IP_POLICY.md](../../IP_POLICY.md)).

## Machine-checked (CI)

| Gate | Check | Where |
|---|---|---|
| A, 3 | Kernel builds and tests with `SCO_GAME_SC=OFF`; no game-pack object or symbol in it | `cmake.yml` `kernel-only` |
| A | The core SDK zip builds and passes from its own files; it includes no extension header and names no game C# type | `sdk.yml`, `release-candidate.yml` (`sdk/test-zip.sh <core.zip>`), `tools/ip_audit.py zip --profile core` |
| A | The Star Citizen extension unpacked over the core builds and passes; overwrites no core file, and keeps its own `VERSIONS.txt.game-sc` / `SHA256SUMS.game-sc` | `sdk.yml`, `release-candidate.yml` (`sdk/test-zip.sh <core.zip> <game-sc.zip>`) |
| A | The core source archive builds a kernel and exercises the synthetic host from an empty directory, without the game implementation | `sdk.yml`, `release-candidate.yml` (`sdk/test-zip.sh <core-source.zip>`) |
| A | The Star Citizen host-source kit builds standalone from its extracted archive | `release-candidate.yml` (`sdk/test_host_source.py HostSourceArchive`) |
| A | ABI v1.1 pins unchanged (`tests/abi_*.c`, C# `Pins.cs`/`GamePins.cs`) | `test.yml`, `sdk.yml` |
| B, 1 | Every tracked file matches a `docs/legal/provenance.json` entry; none `unresolved` | `ip-compliance.yml` (`tools/ip_audit.py repo`) |
| C, 2 | No binary or refused-extension file outside the exact-hash allowlist, in the repository or any zip | `ip-compliance.yml`, `release-candidate.yml` |
| C, D, 4 | Vendored files match their pinned hashes; licence texts and notices present and shipped; `sdk/components.json` and `provenance.json` agree; REUSE 3.3 compliant | `ip-compliance.yml` (`ip_audit.py repo`, `reuse lint`) |
| E, 5 | SPDX 2.3 SBOM per zip, bound to commit and zip hash | `ip-compliance.yml`, `release-candidate.yml` (`ip_audit.py zip --sbom`); SPDX validation in `ip-compliance.yml` |
| E, 6 | Each profile packaged twice gives the same bytes; each profile's `SHA256SUMS` matches every entry | `ip-compliance.yml`, `sdk/test_package.py` |
| E, 7 | Each check refuses a synthetic defect (52 cases) | `ip-compliance.yml` (`tools/test_ip_audit.py`) |
| G | All nine required jobs succeed on the exact builder SHA, including `docs-site/docs-site` (the documentation site built and validated from that exact SHA); workflow ownership disambiguates the two `windows` checks | `release-candidate.yml` `prerequisites` (`tools/release_promote.py prerequisites`) |
| H | `docs/releases/<tag>.md` holds exactly one strict JSON release-scope block whose tag, SDK version, game-pack version, plugin ABI and profile list equal `sdk/VERSION`, `SCO_GAME_PACK_VERSION`, `SCO_API_MAJOR`/`SCO_API_MINOR`, the `sdk/components.json` profiles and the candidate workflow's profile loop; active download/extraction coordinates in `docs/getting-started.md` name those versions; active release-notes and host-source sections carry no pre-release scope denial ("not published", "publication is excluded") | `test.yml` `test` and `release-candidate.yml` `prerequisites` (`tools/doc_release_gate.py --tag <tag>`); refusal tests `tools/test_doc_release_gate.py` in `ip-compliance.yml` |
| G | Trusted SSH signature, approval and complete payload verified; downloads use approved run/artifact IDs, never a name or latest run; each file's attestation names this repository and candidate workflow | `release.yml` `verify` (`tools/release_verify.py verify`, `gh attestation verify`) |
| G | Only verified bytes are uploaded, with the committed approval and signature, to a draft; exact asset inventory is listed, downloaded and re-verified before publication | `release.yml` `publish` |

A failed, missing, skipped, cancelled or neutral mandatory check blocks the candidate. Nothing in
CI creates a tag. Candidate generation and public promotion are separate operations.

## Needs a person

- [ ] **F:** the CHANGELOG section and the README describe what ships (core vs. extension) and claim no more than the checks show.
- [ ] **B:** inspect the exact files selected for each candidate profile. Every shipped `needs-review` file requires an applicable maintainer decision in `docs/legal/release-decisions/`, or exclusion from that profile. Current source and generated catalogue metadata include pending review; historical decisions for earlier versions do not approve new candidate bytes. A passing repository or ZIP audit establishes structural coverage, not provenance clearance. Check the packaged attribution inventory, not an exclusion description alone.
- [ ] **D:** counsel has reviewed the plugin-licence wording ([option A, chosen 2026-10-11](release-decisions/2026-10-11-plugin-licence.md)), or the release states nothing stronger than it.
- [ ] **G:** the maintainer checks the source commit, candidate run, artifact IDs and every file hash, and qualified counsel reviews any open material legal question.
- [ ] **G:** the maintainer signs and commits the approval record, then tags. Publishing is a separate act from CI going green.
- [ ] **H:** before dispatching the candidate, the release notes describe the declared profiles in prose as well as in the scope block, and `docs/getting-started.md` walks through the new archives. The gate compares coordinates and refuses scope denials; it does not judge whether the prose is accurate.
- [ ] **I:** after publication, the documentation deployed to GitHub Pages is from the published commit, and the public release-notes and getting-started routes show the published versions and profiles. Gate H checks the source, not the deployed site.
- [ ] **J:** after Pages verification, the release announcement is sent once and its delivery receipt is recorded. An announcement attempt is not a delivery; an ambiguous timeout is investigated, never blindly resent.

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
| `source.repo`, `source.sha` | `scuba-sc/sco-core` and the full lowercase 40-hex commit used to build the candidate; equal to the published `--source-sha` or its ancestor with only `docs/legal/release-decisions/` changed |
| `source.dependencies` | Array of `{path, sha}` for every recursive submodule, rooted at the source checkout; exact equality with pinned gitlinks, including nested dependencies |
| `components_manifest_sha256` | SHA-256 of the exact `sdk/components.json` blob at `source.sha` |
| `builder.workflow_path`, `builder.workflow_sha` | Regular `.github/workflows/*.yml` or `*.yaml` file at the pinned workflow commit; this commit may differ from the source checkout commit |
| `builder.run_id`, `builder.run_attempt` | Positive integers, with run id exactly `--run-id` when supplied |
| `artifacts` | Nonempty array of `{name, role, size, sha256, artifact_id}`; roles are `zip`, `source`, `sbom`, `notices`, `evidence`, `checksums`; positive build-artifact id per entry (several files may share an id) |
| `profiles` | Exactly the historical `["core", "game-sc", "legacy"]` or the five-profile `["core", "game-sc", "legacy", "core-source", "host-source-sc"]`, in that order; SDK 2.1.0 preparation uses the latter |
| `distribution.status` | Only `approved-for-publication` passes; `internal-only` and `hold` are explicit refusals |
| `approved_by`, `approved_at` | Nonblank maintainer identity and RFC 3339 UTC timestamp (`Z` or `+00:00`); no future approvals; maximum age seven days by default |

`source.repo` is `scuba-sc/sco-core`, the repository's name since it moved from the `scubamount`
account to the `scuba-sc` organization. The signed `sdk-v2.0.0.json` was written before the move and
names `scubamount/sco-core`. It stays exactly as signed, and nothing in CI or the test suite
validates it against the current schema. To re-verify it with `tools/release_verify.py`, use a
checkout at its own tag, which carries the schema it was written for.

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
The verifier resolves the supplied trust path and the Git checkout root, and refuses any trust
file inside that root, including an outside symlink whose target is inside. Path location cannot
prove policy independence: a caller must still obtain the outside file from the protected trust
source, not copy repository-controlled bytes out of the checkout or use a hard-link alias.
`approved_by` is signed attribution, not the SSH principal: authorization always comes from that
external allowed-signers policy. Builder ids are signed provenance, not a live GitHub API query;
the caller supplies the trusted expected run id to bind the downloaded run's artifacts.

Exit `0` means every check passed. Every refusal exits `1`, emits one `refused: ` line per failed
check to stderr and does not echo untrusted values or subprocess output. `--now <RFC3339>` is for
reproducible tests only; publication must use the actual UTC clock. CI runs
`python3 tools/test_release_verify.py` with freshly generated throwaway Ed25519 keys; signature
tests skip with an explicit message only if `ssh-keygen` is absent.

## Release refusal matrix

`V` means `tools/test_release_verify.py` (`ReleaseVerifyTests`); `P` means
`tools/test_release_promote.py` (`PromotionTests`); `S` means the same promotion test file's
`SignedPromotionTests`; `D` means `tools/test_doc_release_gate.py` (`DocReleaseGateTests`), whose
passing controls are `test_current_release_passes`, `test_next_release_passes` and
`test_historical_prose_and_older_notes_allowed`. Mixed rows name tests from both files/classes. Existing verifier cases
are reused, not copied. Every negative case asserts a refusal; the signed integration cases
use the real verifier CLI, Git and throwaway OpenSSH keys with an injected, offline GitHub client.
`test_offline_signed_candidate_draft_publish_round_trip` is the passing round-trip control;
its create/edit responses are synthetic and perform no GitHub writes.

| Refusal | Test name(s) |
|---|---|
| Missing / malformed SHA | V: `test_missing_sha`, `test_malformed_sha` |
| Blank approval identity / date | V: `test_blank_approver`, `test_blank_date` |
| Duplicate JSON keys (including nested) | V: `test_duplicate_keys`, `test_nested_duplicate_keys` |
| Unknown JSON properties (including nested) | V: `test_unknown_property`, `test_nested_unknown_property` |
| Expired approval record | V: `test_expired_record`, `test_age_fractional_second_expired` |
| Nonexistent Git object | V: `test_missing_source_object`, `test_unknown_published_object_refused` |
| Failed Git command (object, ancestry, diff) | V: `test_missing_git_results`, `test_failed_ancestry_command`, `test_failed_diff_command` |
| Wrong signing key / namespace | V: `test_wrong_key`, `test_wrong_namespace` |
| Revoked key (removed from allowed-signers) | V: `test_revoked_key` |
| Checkout-local trust file, including symlink target | V: `test_checkout_trust_file_refused`, `test_checkout_subdirectory_trust_file_refused`, `test_symlink_to_checkout_trust_file_refused` |
| Extra ZIP / missing ZIP | P/V: `test_fetch_extra_member`, `test_missing_file` |
| Swapped SBOM | V: `test_swapped_sbom` |
| Wrong core source archive | V: `test_wrong_source_archive` |
| Changed payload size / hash | V/S: `test_changed_size`, `test_fetch_changed_candidate_bytes` |
| Duplicate payload filename / ZIP member | V/P: `test_duplicate_filename`, `test_fetch_duplicate_member` |
| Stale or different expected run ID | V: `test_different_run_id` |
| Expired Actions artifact | P: `test_fetch_expired_artifact` |
| Artifact from another builder run / wrong approved artifact ID | P: `test_fetch_different_builder_run`, `test_fetch_member_from_wrong_approved_artifact_is_refused` |
| Regenerated payload or approval after signing | V: `test_regenerated_zip_after_approval`, `test_signature_over_modified_bytes` |
| ZIP traversal member | P: `test_fetch_path_traversal_member`, `test_fetch_absolute_path_member`, `test_fetch_backslash_member` |
| ZIP symlink member | P: `test_fetch_symlink_member` |
| ZIP directory member | P: `test_fetch_directory_member` |
| Skipped required check | P: `test_prerequisites_skipped_job` |
| Cancelled required check | P: `test_prerequisites_cancelled_job` |
| Neutral required check | P: `test_prerequisites_neutral_job` |
| Missing / failed required check | P: `test_prerequisites_missing_job`, `test_prerequisites_missing_conclusion`, `test_prerequisites_failed_job` |
| Green check on a different SHA (check or owning run) | P: `test_prerequisites_wrong_check_sha`, `test_prerequisites_wrong_run_sha` |
| Builder not on main / not release-candidate workflow | P: `test_fetch_wrong_builder_branch`, `test_fetch_wrong_builder_workflow` |
| Partial profile success / failed owning workflow | V/P: `test_partial_profiles`, `test_prerequisites_partial_job_success_in_failed_workflow` |
| Draft asset set differs (extra, missing, duplicate) | P: `test_draft_extra_asset`, `test_draft_missing_asset`, `test_draft_duplicate_asset_name` |
| Draft asset bytes differ (payload, approval, signature) | S/P: `test_draft_payload_bytes_mismatch`, `test_draft_manifest_bytes_mismatch`, `test_draft_signature_bytes_mismatch` |
| Existing draft or published release for tag | P: `test_draft_existing_draft_refused_before_create`, `test_draft_existing_published_release_refused_before_create` |
| Verified draft identity / inventory changes before publication | P: `test_publish_replaced_draft_id`, `test_publish_asset_id_changed`, `test_publish_asset_size_changed`, `test_publish_asset_updated_at_changed` |
| Non-approval source change after building | V: `test_non_approval_change_refused` |
| Documentation site not built from the exact SHA (missing, other SHA, PR build) | P: `test_prerequisites_docs_site_build_missing`, `test_prerequisites_docs_site_built_from_other_sha`, `test_prerequisites_docs_site_pull_request_build_does_not_count` |
| Missing, wrong or reordered declared profile | D: `test_missing_profile_refused`, `test_wrong_or_reordered_profiles_refused`, `test_workflow_profiles_must_match_components` |
| Declared version, ABI or tag mismatch | D: `test_version_mismatches_refused`, `test_tag_mismatches_refused`, `test_missing_version_sources_refused` |
| Missing, duplicate or malformed release-scope block | D: `test_scope_block_structure_refused` |
| Pre-release scope denial in active sections | D: `test_forbidden_phrases_refused_in_active_notes`, `test_forbidden_phrase_refused_in_host_source_doc`, `test_section_after_historical_section_is_active_again` |
| Stale or absent getting-started coordinates | D: `test_stale_getting_started_coordinates_refused`, `test_getting_started_without_coordinates_refused` |
| Approval record absent, unsigned or with different profiles | D: `test_approval_refusals` |

The workflow's `fetch`, `prerequisites`, `draft`, `publish` and `summary` calls are implemented
in `tools/release_promote.py`. All GitHub commands use one injectable client boundary;
unit tests never reach the network. Workflow permission scopes, signature checks,
attestation verification and draft-before-publication ordering remain unchanged.
These tests prove the local refusal logic, not GitHub environment protection or live attestations.
Live candidate download and draft upload require a separately authorized maintainer run.

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
 "source": {"repository": "scuba-sc/sco-core", "commit": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
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

## Maintainer configuration (before the first release)

Create the GitHub environment **`release`** with the intended deployment reviewers and tag policy.
Set its environment variable **`SCO_RELEASE_ALLOWED_SIGNERS`** to the trusted OpenSSH
`allowed_signers` file content, not a private key. For example, an entry has the form
`sco-maintainer namespaces="sco-release-v1" ssh-ed25519 <public-key>`. The verifier always
authorizes the SSH principal `sco-maintainer`; `approved_by` is signed attribution, not the principal.
Both promotion jobs read this environment; only `publish` gets
`contents: write`. An empty variable fails with **`no trusted signer configured`**. Trust is never
loaded from an allowed-signers file committed alongside an approval.

The candidate workflow has read-only repository/check/run access. Only its `candidate` job gets
`id-token: write` and `attestations: write`, to attest the public repository's payload files.
No candidate job can create or modify a release. Configuration is a maintainer action, not a CI step.

## Releasing

1. Set `sdk/VERSION`, add a `## sdk-v<version> (<date>)` section to `CHANGELOG.md`, write
   `docs/releases/sdk-v<version>.md` with its release-scope block, update the download and
   extraction coordinates in `docs/getting-started.md`, and merge the intended source changes
   into `main`. Wait for **cmake/linux, cmake/kernel-only, cmake/windows, sdk/package,
   sdk/windows, sdk/csharp-host-sim, test/test, ip-compliance/audit and docs-site/docs-site**
   to succeed on that exact main commit.
2. Manually dispatch **`release-candidate.yml` on that main commit**, with input
   `tag: sdk-v<version>`. It runs the documentation gate, checks those prerequisites, builds
   `core`, `game-sc`, `legacy`, `core-source` and `host-source-sc`, audits each archive, writes its SBOM, and exercises each zip (the extension
   overlays core). It writes `SHA256SUMS`, attests every payload file and uploads one immutable
   artifact named **`release-candidate`**. Do not push a tag yet.
3. Download and review that candidate. Copy the summary's **`builder.run_id`**, artifact ID,
   **every filename, byte size and SHA-256** into the approval manifest, including the SBOMs and
   `SHA256SUMS` itself. Every `artifacts[]` entry names its `artifact_id`; all files in the single
   candidate bundle use the same ID. Save this `release.json` at
   `docs/legal/release-decisions/sdk-v<version>.json`. Complete the other required approval
   fields; a hash inventory alone is not publication approval. Never select a later run by name.
4. Review the complete approval and sign its exact bytes with the trusted maintainer key:
   ```sh
   ssh-keygen -Y sign -f <maintainer-key> -n sco-release-v1 docs/legal/release-decisions/sdk-v<version>.json
   ```
   The detached signature is `docs/legal/release-decisions/sdk-v<version>.json.sig`. Any JSON
   edit after signing requires signing again. Keep the private key outside the repository.
5. Commit **both JSON and signature** on top of the approved source, changing only release
   decisions since the candidate source commit. Verify the approval locally against the original
   downloaded payload (the allowed-signers file comes from a separately trusted source):
   ```sh
   python3 tools/release_verify.py verify \
     --manifest docs/legal/release-decisions/sdk-v<version>.json \
     --signature docs/legal/release-decisions/sdk-v<version>.json.sig \
     --allowed-signers <trusted-allowed-signers-file> --payload-dir <candidate-payload-dir> \
     --tag sdk-v<version> --source-sha <approval-commit-sha> --run-id <candidate-run-id>
   ```
   If the source changes, or the candidate expires or fails the verifier's age policy, build a
   fresh candidate and review/sign a new approval. Do not bypass a refusal.
6. The maintainer tags and pushes the commit containing the signed approval:
   `git tag sdk-v<version> && git push origin sdk-v<version>`.
   `release.yml` **never rebuilds**: it downloads the approved IDs, verifies the signed manifest
   and every payload file's provenance, and transfers the verified payload to `publish` by exact
   artifact ID. The release environment's protection rules apply before promotion.
7. `publish` creates a **draft** with exactly the manifest's payload files plus **`release.json`**
   and **`release.json.sig`**, using the tag's CHANGELOG section as notes. It lists and downloads
   every draft asset, compares the approval files with the tag, and verifies the downloaded payload
   again. Only then does it make the draft public. It never uses `--clobber`. A failed round trip
   leaves a draft for investigation; do not manually publish it or replace its assets to bypass CI.
8. Verify the deployed documentation (Gate I), then send the announcement once and record its
   delivery receipt (Gate J).

## Signed approval record

The tag's committed `docs/legal/release-decisions/<tag>.json` is published as `release.json`, with
its detached `.json.sig` published as `release.json.sig`. The signature namespace is
**`sco-release-v1`**. `tools/release_verify.py verify` is the promotion gate; the older unsigned
approval check by itself is insufficient.

The workflow reads `builder.run_id`, `artifacts[].name` and `artifacts[].artifact_id`. The manifest
must bind the complete payload inventory, source and approval metadata required by the verifier.
Candidate summaries include the outer `SHA256SUMS` file's own hash, even though an inventory
cannot include its own hash inside itself. Artifact IDs identify the immutable Actions bundle;
SHA-256 values bind the individual files. A successful candidate run is evidence, not a signature
or approval to publish.
