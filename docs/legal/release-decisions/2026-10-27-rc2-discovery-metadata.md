# RC2 discovery metadata: archive scope

- Decision: include the current native index and status registry in the `game-sc`, `legacy` and `host-source-sc` SDK archives, subject to exact-file provenance and release review.
- Decided by: the maintainer; recorded by an agent from the maintainer's affirmative answer to the proposed RC2 inclusion.
- Date: 2026-10-27
- Applies to: SDK 2.4.0 RC2 preparation only; no change to `core` or `core-source`.
- Source snapshot for this scope choice: commit `410460c19cf960cb4272f94bee0615bb5c8a7a5c`.
- `sdk/native_index.json` SHA-256: `cd7e1113ae4272c9c858ef34e25d4bf9d3026f278f053bac47eebc5c20c9f17c`.
- `sdk/status_registry.json` SHA-256: `7b8ea1ac21cf0e854a908f6d653873f8b3438b52f4b8b6f047edba813bbc1d7f`.

The maintainer approved retaining both files in those three archives so their static discovery features can use them. The index approval includes its `hash` and `source` fields. `hash` is a symbolic identifier derived from the row name, not an address; `source` is the repository path naming the row's source file. The current index also supplies row names, subsystem, tier and associated capabilities. The index and registry are distinct files and each remains subject to its applicable exact-file review.

For these identified RC2 source bytes and archive profiles only, this decision supersedes the no-SDK-archive restriction in the [2026-10-14 native-index decision](2026-10-14-native-index.md). That historical decision remains intact for its other scope and channels. It is not a general permission for future versions or other content. The 2.2 and 2.3 registry scope records also remain historical; this decision states the intended RC2 archive scope separately.

This records a distribution-scope choice, not a provenance or rights attestation, human exact-file Gate B review, counsel opinion, candidate approval, signature, tag or publication. The maintainer must decide each shipped pending file and generated attribution against the actual candidate bytes under the [release checklist](../release-checklist.md). A changed index or registry requires renewed evaluation for that candidate. No in-game validation is asserted.
