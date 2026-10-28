# SDK release checklist

The SDK has five profiles: `core`, `game-sc`, `legacy`, `core-source` and
`host-source-sc`. A release promotes the exact candidate bytes built on `main`;
it does not rebuild them.

## Machine checks

- The nine required jobs pass on the candidate source commit: CMake Linux, kernel-only and Windows;
  SDK package, Windows and C# host simulation; test; IP compliance; and docs-site.
- The documentation gate checks the version, profile scope and download coordinates. The candidate
  builds all five archives, verifies isolation and overlay compatibility, audits notices and SPDX
  metadata, exercises each archive, and writes checksums and build attestations.
- Promotion checks the selected candidate run, artifact, tag and source commit; verifies the exact
  payload, checksums, profiles and attestations; and checks every asset after a draft round trip.
- `needs-review` is informational metadata. Files with unresolved origin or rights and prohibited
  material remain blocked by the repository and package audits. Required licences and notices remain
  in the packages.

## Needs a person

- Review the release notes and changelog for accurate scope and the evidence actually obtained.
- Choose the candidate and initiate the release using the repository's GitHub permissions and the
  `release` environment. Keep the plugin-licence wording no stronger than
  [option A](release-decisions/2026-10-11-plugin-licence.md).
- After publication, check the deployed documentation and release assets. Final releases use the
  existing receipted announcement workflow; release candidates do not send that notification.

No separate per-file release decision or maintainer SSH signature is required. Build attestations
identify the workflow and source that produced bytes; they do not themselves authorize a release.

## Releasing

1. Prepare the version and release notes on `main`, then wait for all nine required jobs on that exact
   commit. Run `release-candidate.yml` with the prospective tag, such as `sdk-v2.4.0-rc.2`.
2. Check the successful candidate's source SHA, run ID, artifact ID, file inventory and hashes.
   Candidate generation does not publish a release or create a tag.
3. Create the intended tag at the exact candidate source commit and push it. Dispatch `release.yml`
   on that same `main` commit with `tag`, `run_id` and `artifact_id`. The selected candidate must be
   the first attempt of a successful main-branch `release-candidate.yml` dispatch; use a fresh
   dispatch if a candidate needs to be rerun.
4. The `release` environment's protection rules apply. The workflow downloads the specified candidate,
   verifies its metadata, checksums and attestations, and creates a draft. It downloads that draft's
   assets and verifies them again before publishing. A failed round trip leaves a draft for
   investigation; do not manually publish or replace its assets to bypass the checks.
5. Verify the published release and deployed documentation. The workflow publishes RC tags as
   prereleases. A final `sdk-v<version>` is a separate release.

If the source changes or the candidate expires, build a fresh candidate. For `sdk-v2.4.0-rc.2` only,
the owner deferred in-game validation: report **not run in game**, not a pass or native-behavior
claim. This deferral does not establish final 2.4.0 acceptance.
