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
| E, 7 | Each check refuses a synthetic defect (24 cases) | `ip-compliance.yml` (`tools/test_ip_audit.py`) |
| G | The release job refuses unless `docs/legal/release-decisions/sdk-v<version>.md` approves the tagged commit (or its parent, with only release decisions changed) | `release.yml` (`tools/ip_audit.py release`) |

A failed or skipped mandatory check blocks the release. Nothing in CI creates a tag.

## Needs a person

- [ ] **F:** the CHANGELOG section and the README describe what ships (core vs. extension) and claim no more than the checks show.
- [ ] **B:** every `needs-review` entry the release ships has a decision in `docs/legal/release-decisions/` (today: `quantum_pack/datacore/eos.toml` F-08, `travel_pack/lists/locations.txt` F-09, and the game pack). Or the release leaves that component out.
- [ ] **D:** the plugin-licence decision is made and recorded ([options](plugin-licensing-options.md)) before any release text states a plugin licence.
- [ ] **G:** the maintainer checks the commit and the zip hashes from the CI run, and qualified counsel reviews any open material legal question.
- [ ] **G:** the maintainer commits the approval record, then tags. Publishing is a separate act from CI going green.

## Approval record

`docs/legal/release-decisions/sdk-v<version>.md`:

```
Decision: approve
Approved by: <name or handle>
Date: <YYYY-MM-DD>
Commit: <40-hex sha of the commit approved>
Artifacts: <zip names and sha256, from the ip-compliance or sdk run>
Scope: <what this approval covers, and what it doesn't (e.g. the game pack)>
Open items: <none, or each with its disposition>
```
