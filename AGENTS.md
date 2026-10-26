# AGENTS.md: sco-core

For **SDK consumers** building extensions, start with [the packaged workflow suite](sdk/skills/sco-plan-sdk-work/SKILL.md) and [discovery guide](docs/agent-workflows.md). The contributor/clean-room process below applies to core contributions and interoperability-derived facts; ordinary consumer debugging and packaging use public SDK inputs.

Welcome to `sco-core`. This file guides AI coding agents (Claude Code, Cursor, Codex, OmO)
working in this repository or in downstream forks.

## North Star & Architectural Context
- **`sco-core`** is the plain-C ABI (`include/sco_api.h`), C++20 SDK wrapper layer
  (`include/sco/`, `sdk/`), signature registry (`sco-sigcheck`), and host service provider
  (`sco.storage`, `sco.ui`, `sco.datacore`, `sco.ipc`, `sco.net`, and Star Citizen `game.*` services).
- **Offline only:** Strictly local/offline game interoperability (no live server, account,
  telemetry, or anti-cheat circumvention). Full policy in `IP_POLICY.md` and `CONTRIBUTING.md`.
- **The plugin ABI only grows:** `include/sco_api.h` is frozen and backwards-compatible.
- **Clean-room hygiene (Rules 6 & 12):** No raw game binaries, dumps, encryption keys,
  player DNA, or bulk GUID dumps in git or PR text.

---

## LLM & Coding Agent Contributor Guide: SDK Contracts & Clean-Room PRs

If you are an AI coding agent adding a new service, extending an existing contract, or reporting
an SDK gap in `sco-core`, follow this workflow so your contribution passes automated intake and ABI gates.

### 1. Study Commit History Before Writing Code
`sco-core` enforces a strict C11 / C++20 ABI contract structure, catalogue registration, and clean-room hygiene.
Before authoring or modifying headers, mine recent commits to see how completed services landed:
```bash
# Find recent contract/service additions and SDK sync commits:
git log -n 20 --oneline
git log -n 5 --stat -- include/ catalogue/
# Inspect a complete contract commit to mirror the 3-way ABI pattern (C11 header, C++20 wrapper, catalogue/tests):
git show <recent-contract-commit-sha>
```

### 2. What Belongs in `sco-core` vs. Provenance Metadata
- **In Committed Source Files (`include/`, `src/`, `catalogue/`, `tests/`):**
  - Pure, clean-room C11/C++20 interfaces, host provider sockets, mock implementations, and consumer tests.
  - Never hardcode raw game memory addresses, proprietary binary blobs, or unverified offsets inside public SDK headers.
- **In the Pull Request Body or `sdk-gap` Issue Body:**
  - Run `.claude/skills/sco-community-extractor/SKILL.md` to generate a `scubamount-community-handoff-v1` provenance block.
  - Include deterministic **anchor strings / CVars** (used by the maintainers' sub-2-second PE string cross-reference pipeline to independently re-anchor your finding), target build manifest metadata, and honest 4-state verification tags (`VERIFIED_IN_GAME`, `RESOLVED_STATIC_ONLY`, or `EXPERIMENTAL_UNVERIFIED`).

### 3. Submitting Without Leaking Downstream Mods
If you are working inside a modded downstream fork, **do not push your dirty working branch**.
Use Step 6 of `.claude/skills/sco-community-extractor/SKILL.md` to stage *only* the clean `sco-core`
contract files in an isolated worktree off `origin/main` (`git worktree add`) and open a `--draft` PR
via `gh pr create --body-file <report.md>`.
