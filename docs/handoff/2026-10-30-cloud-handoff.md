# Cloud handoff — 2026-10-30 (canonical, all repos)

Status snapshot for the agent taking over. Everything below is **draft / local
integration**. Nothing here is merged, tagged, released, or run in game.

## Remote state at handoff

| Repo | `main` | Notes |
|---|---|---|
| scuba-sc/sco-core | `5b010dc0697535f10e9264235494b667ea850ea6` | Latest release `sdk-v2.4.0-rc.2`. Final `sdk-v2.4.0` **not published** (owner-only). |
| scuba-sc/sc-offline | `0f569b963be1b6a540fd93c80abffadd4ef67a13` | Draft #117 (ASOP ship selector) parked. |
| scuba-sc/sc-mp | `57ac2fd3e6fd3e23a974d58c33fbd4935b9d12f0` | Open #14 (chat), #15 (relay installer) — superseded by the handoff branch below. |
| scuba-sc/scuba-engine | `8373c52012d2f58fc773f796e199de81c16eb571` | — |

External PR sco-core #303 (third-party ASOP) is open and untouched. Do not
review or merge it without owner instruction.

## Handoff branches (all draft PRs)

| Repo | Branch | Base | Content |
|---|---|---|---|
| sco-core | `handoff/core-integration-20261030` | `5b010dc` | Resolver bounds (#302), MCP workflow contract, ASOP staging, SPDX fix, ASOP callback containment, MSVC slot-count fix, **WIP** initial-fleet policy |
| sco-core | `hold/registry-owner-drain-20261030` | `81942c3` (registry integration) | **DO NOT MERGE.** Owned-shutdown prototype, review verdict HOLD |
| sc-offline | `handoff/offline-integration-20261030` | `0f569b9` | Fleet stable identities, VPN admission (#183 work) |
| sc-mp | `handoff/mp-integration-20261030` | `57ac2fd` | Chat (#14) + installer (#15) fixes, opt-in synthetic authority spike with receipts |
| scuba-engine | `handoff/native-visual-acceptance-20261030` | `8373c52` | Native designer / net sandbox / plugin-surface parity and visual acceptance |

Each branch has its own `docs/handoff/2026-10-30-*.md` runbook with exact
verification and gaps.

## Verified vs. not verified (core branch)

| Commit | What | Evidence |
|---|---|---|
| `799a8c3`, `c4252aa`, `c0b5d92` | #302 resolver bounds + sentinel fix + MSVC fixture warnings | macOS 186/186, sanitizer 29/29; Windows 206/206 on the standalone branch |
| `8a1cff3`, `75c579a` | SDK workflow guidance bound to MCP schema; byte-preserving docs, symlink refusal | portable suite |
| `dce93dc` | ASOP staging, `Reconfigure` **refused** | portable 189/189 |
| `6991918` | ASOP row-callback exceptions → busy; one deferred game-thread initial attempt | ASOP ASan/UBSan 8/8 |
| `b5db321`, `879341e` | Injective SPDX IDs + namespace version | 5 profiles ×2 byte-identical; 10+10+10 SPDX validations; package 59/59; IP 78/78 |
| `8223e2d` | Bound ASOP slot-count conversions (fixes MSVC C4267 in `catalog_win.cpp`) | **Windows /WX full suite NOT rerun** (interrupted). Prior Windows run on `dce93dc` was 208/209 with `asop_host` not built — that is a failure, not a pass. |
| `978c858` **WIP** | Initial fleet publication skips unresolved entries and reports them (owner decision); runtime replacement stays all-or-nothing | Portable fleet test passes (clang, `-Werror`). No MSVC run, no independent review. |

## Owner decisions in force

- Initial ASOP fleet: **publish valid entries, report skipped ones** (preserves old main). Runtime replacement stays strict.
- Native `Reconfigure` stays refused.
- Registry: require proven reader/worker shutdown; keep unsafe native activation refused; **no fail-stop termination**; success-only callbacks are not evidence.
- Single-authority networking design is a **recommendation, not approved** production direction. The sc-mp spike is experimental.
- In-game smoke / V7 is **deferred, not waived**. No game execution.
- Owner approves draft PRs per batch; approval never implies merge or release.
- No force-push to `main`. No tag/promotion: those are owner-only.

## Next work, in order

1. **Core Windows gate.** On `handoff/core-integration-20261030`: MSVC default `/WX`, game ON, tests ON, full CTest. Must be all-pass including `asop_host`. Then independent review of `8223e2d` + `978c858`; drop the `WIP` label only after both.
2. **ASOP open items** (do not invent): deferred initial attempt is consumed even if rebuilding fails; a successful deferred build cannot repair the original entitlement response; production busy-callback composition absent; native lock/reentry/SEH unverified.
3. **Registry startup safety** (blocks the four owned-registry branches): sc-offline refuses native activation without a real reader barrier — keep it that way until a preparation/activation split is designed and verified on Windows with synthetic targets. The `hold/` branch is reference only.
4. **sc-mp**: see its runbook; next increments are real process topologies, durable incarnation identity, then entity ownership. Do not start distributed workers.
5. **Engine**: SDK pin adoption waits for a published 2.4.0 archive/hash + fresh Linux eight-peer run (engine #37).
6. **Release**: owner-only. Candidate run `41122658024` / artifact `13008569602` was verified for `5b010dc`; if `main` moves, a fresh candidate is required.

## Environment notes

- Windows validation uses VS 2019/2022 BuildTools, generator `"Visual Studio 16 2019" -A x64`; pip-installed CMake must be on PATH for the whole session (ctest spawns cmake). Use a non-system drive for build trees.
- No usable Linux host was available locally; Linux results in this branch came from earlier CI or are marked unverified.
