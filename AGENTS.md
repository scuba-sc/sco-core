# AGENTS.md — sco-core engineering charter

This file is binding guidance for development agents and reviewers. Read it with CONTRIBUTING.md, IP_POLICY.md, docs/legal/release-checklist.md, the current source and current open PRs. It is not a substitute for human legal approval.

## Product architecture
- sco-core owns the general-purpose, engine-neutral kernel, plugin SDK/host and optional Star Citizen game pack. sc-offline is the primary product/consumer; sco-titanlink-northstar is a separate MIT bridge endpoint.
- Preserve the plain-C sco_api.h v1.1 layout (128 bytes). Extend behavior through additive, versioned, size-prefixed service tables with C/C++/C#/Lua layout and compatibility checks. Never move raw game pointers across the public SDK boundary: use opaque IDs, lifetime rules and cleanup.
- SCO_GAME_SC=OFF must compile and test without Star Citizen game code. Signatures, volatile offsets, detours using game structures and patch-specific evidence belong in the optional game pack, not the kernel or generic SDK. Game-specific Lua adapters must remain optional and read-only.
- Keep plugin owners, unload/reload safety, thread affinity and event frequency explicit. Use low-volume events and filtered high-volume entity watches. Do not call untrusted plugins directly from a game detour.
- Put shared SDK functionality here rather than growing private copies in sc-offline; coordinate submodule and consumer migrations with tests.

## Current highest-priority incident
P0: sc-offline v0.7.5 crashes during megamap loading (Game.log: 4.10.196.36804, access violation). Two later mod.log sessions report event.player_death already_hooked. Treat the collision as a separate reproducible issue, not a proven cause of the earlier crash. The crash RIP is outside StarCitizen.exe's reported image range; identify its module and stack using authorized diagnostics, matched build data and timestamp-aligned logs. Do not claim a cause or successful fix without evidence.

## Release and IP
- sdk/components.json is authoritative for the core, game-sc and legacy SDK ZIP contents. Verify each independently, C++/C#/Lua examples, ABI pins, notices, SPDX SBOM, provenance and deterministic hashes.
- Keep existing licenses: project-authored GPL-3.0-only; exactly four special MIT wire/bridge headers; third-party license grants remain upstream grants. Maintainer selected option A for plugin wording and signs the decision themselves; do not imply independent counsel reviewed it.
- First hardened SDK release publishes three ZIPs (core, game-sc, legacy), plus requisite checksum/SBOM/supporting release files. Do not silently publish only legacy. Any newly found release-material issue must be tracked and resolved or excluded before declaration.
- A release readiness sign-off means the bounded, documented work required for a usable, verified SDK is completed—not that every speculative feature is implemented or copyright risk is certified away. Human release approval remains separate from CI.
- Never commit proprietary game code or assets, dumps, secrets, bypass mechanisms, official-online features or unreviewed data. Document factual interoperability methods and test against actual supported builds.

## Required change protocol
1. Inspect main and open PRs; state invariants and acceptance criteria before implementation.
2. Prefer minimal reviewable PRs and backward-compatible migration. Avoid duplicate agents' edits.
3. Run appropriate tools/test.sh, CMake/CTest, kernel-only, ABI pins, SDK profile tests, IP checks and sc-offline integration checks; report actual results rather than assuming CI passed.
4. Where game behavior changes, mark it unverified until actual in-game testing; report precise repro and tested build.
5. Update docs, manifest, provenance, tests and changelog with the same change. Feed sc-offline product lessons back into sco-core.

These principles also apply to automation prompts (Hermes skill, cron bot, OmO); those external configurations should reference this version-controlled document and should not invent a separate architectural policy.
