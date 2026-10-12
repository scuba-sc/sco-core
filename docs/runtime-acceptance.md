# Runtime MCP acceptance ledger

## Local implementation wave (2026-10-12)

Boundary: separate default-off local stdio launcher, fixed six read tools, real
host dispatcher/local mapping and packaged native peer. Synthetic fixture inputs
are project-authored; no game binary/assets/dumps or real user identifiers.

Owner selected **compile/configure only** for steps 3–5. New test files were
written but **NOT RUN**. No passing end-to-end/security/game result is claimed.

| Evidence | Status |
|---|---|
| CMake `SCO_GAME_SC=OFF` configuration | Completed on macOS/AppleClang |
| `test_runtime_diagnostics_policy` compilation/link | Completed on macOS |
| `test_runtime_diagnostics_audit_win` compilation/link | Completed POSIX branch only; Windows branch NOT VERIFIED |
| `sco_core_mcp_runtime_peer` shared adapter compilation/link | Completed on macOS |
| Python launcher/peer/package/new tests syntax compilation | Completed |
| `test_runtime_diagnostics_integration` compilation/link | Completed on macOS |
| Policy/stdout/discovery regression/integration execution | **NOT RUN**, owner speed order |
| Direct hostile-peer/replay/mutation/cap/timing acceptance | **NOT RUN** in this wave; do not promote existing source tests to acceptance |
| Runtime package invocation/extracted consumer verification | **NOT RUN** |
| Windows/MSVC build/execution | **NOT VERIFIED** |
| Actual non-admin token/mapping/collector ACL and clear/retention denial | **NOT VERIFIED** |
| Administrator collector provisioning, capacity/overflow and 7-day retention | **NOT VERIFIED** |
| Product consumer `runtime_inspection` wiring/revocation fact | Separate authorized downstream work; not implemented here |
| Manual offline host/game acceptance | **NOT VERIFIED; not run in game** |

## No-game checks available to the human/next authorized run

With `SCO_BUILD_RUNTIME_MCP=ON`, `test_runtime_diagnostics_integration` builds the
C++ fixture host and depends on the actual shared peer adapter. Its named CTest
runs `tools/sco-core-mcp-runtime/test_integration.py`, connecting actual initialize,
list and six tool reads over stdio/native mutual-auth mapping. Fixture control
commands are test-only stdin, not runtime tool/dispatcher operations.

Default collector is explicitly synthetic. It covers logical audit denial, not
OS privilege isolation. The harness can accept `--collector-log`,
`--collector-source`, `--collector-capacity` for an administrator-provisioned
Windows no-game channel. Do not silently treat synthetic receipts as protected OS
acceptance. Record host/source commit, adapter hash, observed session IDs and
actual effective identities separately without storing secrets or personal IDs.

Fixture metrics print measured `snapshot_us`, work-call count, mapping/record caps,
audit events, mutation callback count and service hand-outs. They are measurements
only after the fixture is executed; compile success does not prove the 1ms tick
budget or protected audit bounds. Snapshot/tick performance, shutdown join latency,
full memory limits and hostile-peer cases remain acceptance debt.

Dedicated Windows privilege fixture (only synthetic `SCOFixture*` channel names):

```text
test_runtime_diagnostics_audit_win --windows-fixture-driver SCOFixtureAudit SCOFixtureSource 8388608
```

Run as the intended non-administrator identity after human provisioning per
[runtime-audit.md](runtime-audit.md). It spawns its own same-identity child and
attempts protected append and denied clear/retention/config/storage/service rights.
Never run these destructive-rights probes against Application/System/Security or
production audit history. Privileged/elevated identity must be refused, not certified.
This fixture has not been executed here.

## Human offline acceptance (never scheduled automatically)

- [ ] Human confirms an authorized offline developer product on the same Windows PC.
- [ ] Provision and verify protected collector/non-admin publisher/client identity.
- [ ] Enable host startup setting plus positive trusted offline verdict and disclosure consent.
- [ ] Register the separate local client with pinned adapter and the six-name allowlist.
- [ ] Observe loaded and refused project-authored fixture plugins, permitted structured logs.
- [ ] Verify redactions, bounded responses, source/build/session/freshness attribution.
- [ ] Disconnect/reconnect, stop/restart, confirm new session and old response invalidation.
- [ ] Revoke product policy with a read pending; confirm no observation escapes.
- [ ] Verify collector denial blocks reads without changing plugin/game state.
- [ ] Measure snapshot/tick/stop work and record the exact host build/adapter hash.
- [ ] Restore default OFF and record only actually observed behavior.

No automatic game launch, injection, scanner, network, official server, account or
anti-cheat action is part of any fixture. Diagnostics acceptance never certifies
unobserved game services; `docs/game/status.md` is unchanged. Security reports use
[SECURITY.md](../SECURITY.md), not public exploit details or game dumps.
