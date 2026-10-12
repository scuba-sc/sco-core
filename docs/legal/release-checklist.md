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
| A | The Star Citizen extension unpacked over the core builds and passes; overwrites no core file | `sdk.yml` (`sdk/test-zip.sh <core.zip> <game-sc.zip>`) |
| A | ABI v1.1 pins unchanged (`tests/abi_*.c`, C# `Pins.cs`/`GamePins.cs`) | `test.yml`, `sdk.yml` |
| B, 1 | Every tracked file matches a `docs/legal/provenance.json` entry; none `unresolved` | `ip-compliance.yml` (`tools/ip_audit.py repo`) |
| C, 2 | No binary or refused-extension file outside the exact-hash allowlist, in the repository or any zip | `ip-compliance.yml` |
| C, D, 4 | Vendored files match their pinned hashes; licence texts and notices present and shipped; `sdk/components.json` and `provenance.json` agree; REUSE 3.3 compliant | `ip-compliance.yml` (`ip_audit.py repo`, `reuse lint`) |
| E, 5 | SPDX 2.3 SBOM per zip, bound to commit and zip hash, validated | `ip-compliance.yml`, `release.yml` |
| E, 6 | Each profile packaged twice gives the same bytes; `SHA256SUMS` matches every entry | `ip-compliance.yml`, `sdk/test_package.py` |
| E, 7 | Each check refuses a synthetic defect (35 cases) | `ip-compliance.yml` (`tools/test_ip_audit.py`) |
| G | The release job refuses unless `docs/legal/release-decisions/<tag>.json` validates against `docs/legal/release-approval.schema.json`, names the tagged commit (or its parent, with only release decisions changed), is unexpired and approves publication | `release.yml` (`tools/ip_audit.py release --tag`) |

A failed or skipped mandatory check blocks the release. Nothing in CI creates a tag.

## Needs a person

- [ ] **F:** the CHANGELOG section and the README describe what ships (core vs. extension) and claim no more than the checks show.
- [ ] **B:** every `needs-review` entry the release ships has a decision in `docs/legal/release-decisions/`, or the release leaves that component out. Since the synthetic example data (2026-10-11), no SDK zip ships one; the game pack's entries ship in none.
- [ ] **D:** counsel has reviewed the plugin-licence wording ([option A, chosen 2026-10-11](release-decisions/2026-10-11-plugin-licence.md)), or the release states nothing stronger than it.
- [ ] **G:** the maintainer checks the commit and the zip hashes from the CI run, and qualified counsel reviews any open material legal question.
- [ ] **G:** the maintainer commits the approval record, then tags. Publishing is a separate act from CI going green.

## Approval record

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

The record is **not yet signed**: `approver.key_fingerprint` is recorded, but no signature is
verified. Until that lands, Gate G rests on the git history and the maintainer's own commit.

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
