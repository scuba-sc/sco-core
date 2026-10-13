# sdk-v2.1.0 unsigned approval draft

Status: UNSIGNED DRAFT — maintainer signature required.

`sdk-v2.1.0.json` binds verified candidate source and payload bytes for maintainer
review. Its schema-required publication-intent fields do not establish signed
publication approval. No signature, tag or release is supplied by this draft.

Candidate: https://github.com/scubamount/sco-core/actions/runs/38349453896
Source: `82462b053b222e9e93f552b98b695ee1e7066047`
Artifact ID: `11790057098`, run attempt 1; nine files: four archives, four SBOMs
and the outer SHA256SUMS. Profile distribution is core, game-sc, legacy and
core-source; schema-v2 represents core-source with the source artifact role.

Scope ruling: [SDK2.1 distribution and public-overlay scope](2026-10-12-sdk-v2.1.0-scope.md).
Game implementation/product export stays held; host-source-sc publication is
excluded. Synthetic and native Windows fixtures are engineering evidence,
not real-game, anti-cheat/official-network, private-LAN/firewall or renderer acceptance.

Hold main after candidate: only release-decision approval changes may land until
signature review, approval merge/verification and the owner's tag sequence completes.
Owner must review and sign the exact JSON bytes; any JSON change requires a new signature.
