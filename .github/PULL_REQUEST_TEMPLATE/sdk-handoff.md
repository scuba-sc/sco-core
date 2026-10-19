---
report_schema: scubamount-community-handoff-v1
date_utc: "YYYY-MM-DD"
contributor_username: "<username>"
target_game_build:
  version: "<e.g. 4.10.197.2878>"
  branch: "<e.g. sc-alpha-4.10.x>"
  p4_changelist: "<e.g. 12913470>"
  build_date_stamp: "<YYYY-MM-DD or UNKNOWN>"
  starcitizen_exe_sha256: "<64-hex sha256 or UNKNOWN>"
---

# Clean-Room SDK Handoff & Contract PR (`scubamount-community-handoff-v1`)

> **For AI Coding Agents (`.claude/skills/sco-community-extractor`):**
> Ensure this PR was created from an **isolated clean worktree** off `origin/main` (`git worktree add`) containing ONLY public-safe `sco-core` contract, catalogue, and test files. Never commit private downstream `plugins/*` or proprietary menu/builder code.

## 1. Summary & Commit Lineage Pattern
- **Capability / Service Added or Extended:** `<short summary>`
- **Gap Classification:** `[ ] Type 1 (Service Extension)` | `[ ] Type 2 (New Service / Host Provider Socket)` | `[ ] Type 3 (Signature Registry Row)`
- **Exemplar Commit(s) Mirrored (`git show <sha>`):** `<commit SHA(s) from git log used as structural reference>`
- **Upstream Ledger ID (if known, otherwise minted during `sco-mod-intake`):** `<F-* / T-* or PENDING_INTAKE>`

## 2. 3-Way ABI & Catalogue Checklist (`sco-core` Contract Bar)
- [ ] **C11 ABI Header (`include/sco/...`):** Pure C11 interface, no hardcoded raw memory addresses or game binary blobs.
- [ ] **C++20 Wrapper (`include/sco/...`):** Clean C++20 ergonomic wrapper matching `sco-core` conventions.
- [ ] **Catalogue & Status Registry (`catalogue/...`):** Updated records/tables and status registry if introducing or extending a service.
- [ ] **Tests & Mock Provider (`tests/...`):** Added or updated consumer/mock tests verifying contract shape and ABI stability.

## 3. Extracted Primitives & Deterministic Anchor Table
*(Used by `sco-mod-intake` for `<2s` deterministic re-anchoring via `pe-string-xref.mjs` against pinned baseline binaries)*

| Subsystem / Service | Primitive Type (`RVA` / `vt[slot]` / `Struct` / `CVar`) | Offset / Slot / Signature | Deterministic Anchor String / CVar / Xref | Verification Tag (`VERIFIED_IN_GAME` / `RESOLVED_STATIC_ONLY` / `EXPERIMENTAL_UNVERIFIED`) |
| :--- | :--- | :--- | :--- | :--- |
| `<e.g. CActor>` | `<e.g. vt[14]>` | `<e.g. 0x140... or slot 14>` | `<e.g. "CActor::SetHealth">` | `<VERIFIED_IN_GAME>` |

## 4. Struct Layouts, VTable Notes & Behavioral Context
- **Struct / Class Size & Alignment Pins:**
  - `<Detail any relevant field offsets, sizes, or DataCore schema names>`
- **Call-Site / Lifecycle Notes (`plugins/*` vs `src/*`):**
  - `<Explain how the host adapter or plugin invokes this primitive and any threading/lifecycle constraints>`

## 5. Clean-Room, Privacy & Hygiene Attestation
- [ ] **Isolated Worktree Verified:** `git status` and `git diff` were audited to confirm ZERO private downstream plugin/menu files are included in this branch.
- [ ] **Honest 4-State Provenance:** All claims are tagged honestly (`VERIFIED_IN_GAME`, `RESOLVED_STATIC_ONLY`, or `EXPERIMENTAL_UNVERIFIED`).
- [ ] **Zero Proprietary Blobs (Rule 6/12):** No raw PE segments, bulk DataCore GUID dumps, player DNA blobs, or encryption keys are embedded in code or PR text.
- [ ] **Offline Scope Only:** Strictly targets local/offline game interoperability (no live server, account, telemetry, or anti-cheat circumvention).
