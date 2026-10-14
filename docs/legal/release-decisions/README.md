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
| [SDK2.1 distribution and public-overlay scope](2026-10-12-sdk-v2.1.0-scope.md) | 2026-10-12 | Four profiles approved; implementation export held, host-source-sc publication excluded; signed bytes required |
| [SDK2.1 host-source candidate scope](2026-10-13-sdk-v2.1.0-host-source.md) | 2026-10-13 | Five-profile candidate selected; supersedes the SDK2.1 host-source exclusion only; fresh signed bytes required |
| [Engine-native index publication channel](2026-10-14-native-index.md) | 2026-10-14 | Decided: site and static MCP only, four fields per row, in no SDK zip; counsel review not recorded; index not yet landed |
| [SDK2.2 distribution scope](2026-10-14-sdk-v2.2.0-scope.md) | 2026-10-14 | DRAFT pending the maintainer; no candidate before approval |

No release is published from these records alone: `sdk-v2.0.0` also needs its signed `sdk-v2.0.0.json` approval.
