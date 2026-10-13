# G5: filtered aim rays and reversible free-camera lease

## Contract and boundary

`game.world.control` 1.0 is an independent 40-byte size-prefixed table in
`sc_world_control.h`, not a reinterpretation/growth of `game.world` 1.0. The
existing read table and frozen `sco_api` 1.1 remain unchanged. This is game-pack
policy; neither implementation nor declarations enter the game-neutral kernel.

C: direct public table. C++: `scosdk/game/world_control.hpp`, full-member-extent
and null checks per call, failed reopen clears the borrowed table.
C# and sandboxed Lua: unsupported. No sandbox mutation expansion is implied.
The new public headers belong only to `game-sc`/combined legacy SDK packages.

## Bounded authority

- Filtered rays copy at most 32 unique nonzero skip IDs. Every ID must still be
  owned by the caller through the ordinary game entity/spawn ownership path;
  no foreign/kept IDs, player pointers or raw physics proxies are accepted.
  Filters are call-only, never shared/persisted. Aim calculation/input is plugin
  policy; this method accepts an explicit zone-local ray just like world 1.0.
- Directions normalized; finite distance clamped to 20,000m. Only position and
  distance are reported. Normal/entity fields and their flags remain unknown/zero.
- One exclusive camera lease, bound to the live plugin and a monotonic nonzero
  session token. Same owner reacquisition is idempotent. Foreign owner cannot
  steal, move or release. Stale/zero tokens cannot affect another camera lease.
- Camera step copies finite world-frame translation (norm <=10m) and unit
  quaternion world rotation delta (shortest angle <=0.25rad). No arbitrary pose,
  zone, FOV, hooks, mode flags or unrestricted input injection is exposed.
  Bounds are per synchronous call, not a sandbox for trusted native plugins.
- All operations require the game thread; providers see no off-thread calls.
  No async work, waiting, plugin callbacks or cross-boundary allocation.
  Recursive provider reentry is refused. Caller extra tails are never written.

## Restoration and failure

A host-only provider must capture **all** camera/input/mode state it will
change before any mutation, retain that snapshot, and make failed acquisition
atomic. Restoration must restore the exact snapshot, not hardcode stock/default
values. The portable policy admits acquisition only with begin/step/restore
callbacks and a registered ordinary owner-release hook.

Explicit release, ordinary owner unload, and service stop use the same restore
path before relinquishing the lease. Repeated unload is a no-op. Failed step
attempts restore immediately and latches restart-required even if restored.
Failed restore retains owner/token and provider backing snapshot; retries can
restore, but cannot reset the failure latch or enable a new lease. Stop/restart
cannot discard a live lease or failure latch. Retained native-provider faults
are subject to the host service-lifetime restart contract, not a promise of safe
unload after arbitrary memory corruption.

## Current production readiness: unavailable

The Windows world-service lifecycle publishes this table and an owner cleanup
hook, but installs **no engine backend**. Both new capabilities remain OFF and
valid new acquisition/filtered ray requests return `SCO_UNAVAILABLE`. Existing
read-only `game.world` behavior is unchanged. Signature matches do not prove
skip construction or reversible shared-camera mutation. No engine offsets,
hooks, engine writes, game sessions or network calls were invented or executed.

Before an adapter can be enabled: owner-run offline/anti-cheat admission proof,
verified skip-ID ownership/streaming conversion, failure-atomic snapshot and
restoration across unload/session shutdown, and exact-build Windows/MSVC and
game evidence are required. The internal seam is not a plugin-provided adapter.

## Synthetic evidence

`tests/test_world_control.cpp` drives the real portable controller through a
fake public table and C++ wrapper. Synthetic preview collision, ownership and
quotas, bad numbers, wrong thread, stale/foreign leases, exact non-default state
restoration, unload twice, stop, acquisition failure, post-mutation step failure,
restore failure/recovery, physically truncated tails and full-size null methods
are covered. A real neutral-runtime `AddReleaseHook`/`Release` path drives the
synthetic camera restore, including second-release refusal; this does not prove
Windows loader/SEH behavior. `tests/abi_world_control.c` independently pins layout/signatures.
These are policy/adapter fixtures, not a Windows engine/game or native fault
containment certification.

Decision pending owner: none required to ship the disabled contract. Engine
adapter activation and Windows/game acceptance remain deliberately blocked.
