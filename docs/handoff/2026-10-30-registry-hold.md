# HOLD — owned-shutdown drain prototype (do not merge)

Branch `hold/registry-owner-drain-20261030`, based on the local registry
integration `81942c3` (21 commits over sco-core main `5b010dc`; includes the
owned signature registry and #302 bounds). This branch exists only so the next
agent can read the attempt. Canonical cross-repo runbook:
`docs/handoff/2026-10-30-cloud-handoff.md` on `handoff/core-integration-20261030`.

Read `REGISTRY-OWNER-DRAIN.md` (design + independent review verdict).

## Why it is held
- Drain deadline not bounded (5 ms requested, 275 ms observed); callable re-entry deadlock risk.
- Cleanup failure can leave ownership marked ready.
- 14/246 baseline-clean tests regress, including an app segfault.
- `void -> bool` ABI/source break for typed function-pointer consumers.
- `sdk/components.json` omits new header/tests (source archives would miss them).
- Broader data/cvar host fixtures not migrated; no packaging/provenance rows.

## Upstream blocker this was attempting
sc-offline `offline_host::Prepare` refuses game services without a native-reader
barrier (`src/offline_host.cpp:60-69`; product sets `gameServices=true` with no
barrier, `src/dllmain.cpp:426-428`). Core contract `include/sco/app.h:112-120`
requires excluding in-flight **and new** native readers throughout service
startup. Recommended direction: audit a preparation/activation split. Do not
replace the guard with an always-true callback. Prove on Windows/MSVC with
synthetic targets.

## Separate MSVC defect in the registry line
`test_signature_registry` exits 3 on Windows Debug (54/55): budget-backed STL
allocation can throw `BudgetExceeded` inside an STL `noexcept` move. Not fixed.
