# Release decisions

One file per decision that a release depends on and that a person, not CI, has to make: licensing
(for example the plugin licence, [options](../plugin-licensing-options.md)), the basis for shipping a
game-derived file, approval of a specific SDK release.

Two kinds of file live here:

- **A release approval** for a tag: `<tag>.json` (for example `sdk-v1.3.0.json`), a JSON object
  validated against [`../release-approval.schema.json`](../release-approval.schema.json) (JSON Schema
  draft 2020-12) plus the semantic checks of `tools/ip_audit.py release --tag <tag>`: the decision is
  `approve` and the distribution is `approved-for-publication`, the record is unexpired and no
  exception has, `release_tag` equals the tag, `source.commit` is the tagged commit or an ancestor
  with only release decisions changed since, `profiles` covers every artifact role, artifact names
  are unique, and `--artifacts DIR` is exactly the listed set. The record is not yet signed:
  `approver.key_fingerprint` is recorded, no signature is verified. Only the maintainer writes one,
  and `release.yml` refuses a tag whose record is missing, invalid or stale.
- **A maintainer decision record**: `YYYY-MM-DD-<topic>.md`, stating the decision, who made it and
  when, what it was based on (with links), the commit and artifact hashes it applies to where
  relevant, and anything it leaves open.

| Decision | Date | Status |
|---|---|---|
| [Example data: synthetic names](2026-10-11-example-data.md) (F-08, F-09) | 2026-10-11 | Done |
| [Plugin licence: option A](2026-10-11-plugin-licence.md) (F-13) | 2026-10-11 | Wording in place; counsel review pending |

No release is approved here yet.
