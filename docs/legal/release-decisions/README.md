# Release decisions

One file per decision that a release depends on and that a person, not CI, has to make: licensing
(for example the plugin licence, [options](../plugin-licensing-options.md)), the basis for shipping a
game-derived file, approval of a specific SDK release.

Two kinds of file live here:

- **A signed release approval** for a tag: `<tag>.json` (for example `sdk-v1.3.0.json`)
  and its detached `<tag>.json.sig`. Promotion requires **schema version 2** from
  [`release-approval.schema.json`](../release-approval.schema.json), verified by
  [`tools/release_verify.py`](../../../tools/release_verify.py). The manifest binds publication
  approval to the candidate's source and recursive dependency commits, builder identity/run,
  exact artifact IDs and complete payload filenames, byte sizes and SHA-256 values. Its exact
  bytes must carry a maintainer SSH signature in namespace `sco-release-v1`, checked against
  independently trusted allowed signers (the release environment's `SCO_RELEASE_ALLOWED_SIGNERS`,
  not a checkout-local trust file). The
  [release procedure](../release-checklist.md#releasing) and
  [signed approval requirements](../release-checklist.md#signed-approval-record) are authoritative;
  [`release.yml`](../../../.github/workflows/release.yml) refuses missing, invalid or stale approval
  inputs and promotes only the approved candidate bytes, never a rebuild.

  **Legacy format:** schema version 1 is the older unsigned decision record, retained by the
  schema for compatibility. Its `approver.key_fingerprint` and the older
  `tools/ip_audit.py release` check are not a verified signature or sufficient approval for promotion.
- **A maintainer decision record**: `YYYY-MM-DD-<topic>.md`, stating the decision, who made it and
  when, what it was based on (with links), the commit and artifact hashes it applies to where
  relevant, and anything it leaves open.

| Decision | Date | Status |
|---|---|---|
| [Example data: synthetic names](2026-10-11-example-data.md) (F-08, F-09) | 2026-10-11 | Done |
| [Plugin licence: option A](2026-10-11-plugin-licence.md) (F-13) | 2026-10-11 | Wording in place; counsel review pending |
| [Contribution rights](2026-10-12-contribution-rights.md) (P05-03) | 2026-10-12 | Approved |
| [Star Citizen overlay package](2026-10-12-game-overlay.md) (P05-06) | 2026-10-12 | Approved |
| [Game implementation](2026-10-12-game-implementation.md) (P05-07) | 2026-10-12 | Option A: export on hold, in no SDK profile |
| [Distribution scope for sdk-v2.0.0](2026-10-12-distribution-scope.md) (P05-10) | 2026-10-12 | Approved; signed manifest still required |

No release is published from these records alone: `sdk-v2.0.0` also needs its signed `sdk-v2.0.0.json` approval.
