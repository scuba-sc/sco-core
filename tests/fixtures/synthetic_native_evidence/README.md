# Private synthetic native evidence model

This repository-only test model accepts invented `fixture.*` row identifiers, synthetic datasets and observations, and `unverified` status only. All fixture values are invented; no game binary, game mapping, executable identity, or game evidence is supplied. Do not feed this tool real observations or use it for release, product decisions, or runtime activation. The fixture namespace restriction reduces accidental use; it is not a security boundary or proof that input data is synthetic.

Run from the repository root:

    python3 tools/test_synthetic_native_evidence.py

The validator rejects duplicate or unknown rows and evidence references, mismatched build/profile/source coordinates, extra fields, non-synthetic claims, and unreferenced observations. It makes a deterministic synthetic-only projection, distinguishing `not_recorded` from `unverified`. The canonical profile vector is checked byte-for-byte and by digest and matching subject fields in the tests. Repeated fields are equality checks, not authenticated identity: the model does not independently hash an executable or verify provenance, profile trust, runtime ownership, gameplay, or verification authority. No real native index is read. This model cannot relax existing startup refusals or make readiness claims.

The maintainer's native-index decision remains unchanged: no expanded evidence publication through the documentation site, MCP or SDK archives. Any production evidence system needs separately reviewed trust, provenance, privacy and publication decisions. The current public index's hash/source conformance remains unresolved.

The archive-membership regression assembles every manifest profile and inspects ZIP member names. It substitutes placeholder generated catalogue and guide contents, so it does not validate compiler-backed catalogue generation or constitute a release test. No evidence files are selected into any profile.
