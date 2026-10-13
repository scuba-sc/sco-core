# Offline quantum control 1.0

`game.quantum` / `sc_quantum_v1` (`sc_quantum.h`) is a bounded game-pack service,
not a hook/patch API. The frozen `sco_api` 1.1 table is unchanged. Plugin input,
commands and UI remain in the consumer. The game pack owns control state, lease
expiry, hook transaction and host-supplied drive/charging/audio/effect integration.
No player/account/ship identifiers, game pointers, raw block strings or memory
cross the public boundary; snapshots are copied into caller storage.

## Availability and evidence

**G8 mutation is owner-approved for R1.** The former unconditional capability
OFF is removed: successful publication with a complete configured host backend
and an installed owner-cleanup dispatcher sets `game.quantum.control` ON. This
means the bounded provider is installed, not that the current drive or session
is admitted. Every enable/hold still checks fresh explicit offline/EAC-absent
observations, drive availability, owner and game thread.

**The default production engine adapter is still absent.** No safe adapter can
be certified from the retained product source alone. With no configured backend,
the table remains discoverable but capability is OFF and calls return
`SCO_UNAVAILABLE` / `SC_QUANTUM_BLOCK_UNAVAILABLE`. This is an integration blocker,
not a pending policy approval. No inferred offset write, product fallback or
signature-only readiness is introduced. External plugin input/commands/UI can
use the unchanged public table, but real boost parity is not supplied by this
provider fixture.

### Explicit host-source integration contract

`src/game/services/quantum.h` is private game-pack/host-source code, **not a
plugin SDK header**. Serialized host setup may call `ConfigureProductionQuantum`
before services start. A complete observe/apply pair is mandatory; an empty pair
clears configuration. Replacement while started, inside a callback or after a
restart-required fault is refused. Configuration performs no engine call.
`spatial.cpp` calls `StartProductionQuantum(SpawnReleaseHooked())`, so actual
shared dispatcher installation gates mutation and hook installation; merely
having a release-dispatch function compiled is insufficient. Standalone fixture
hosts must install the forwarder before asserting cleanup readiness.

A supplied adapter must establish all of these obligations:

- Positively inspected offline/EAC-absent admission, never derived from matching
  rows, launcher flags, an absent DLL alone or transport installation. Unknown
  stays false. Observe supplies copied phase/block/progress within public bounds.
- Exact-build call/layout validation and live **local-player-owned** drive
  association; identity/lifetime validation before every write and restoration.
- Save first-prior state before each mutation; write only owned state, not shared
  drive parameter storage. Validate every value separately: finite charge in
  0..1, input mode/action on an exact allowlist, input value only 0/1, and bounded
  effect operations. The provider itself passes only boolean controls and a
  1..1000 ms lease; it does not authorize arbitrary field/action writes.
- `apply(false,false)` restores only this adapter's changed input/charge state
  and owned sound/effect activity, including after drive/admission loss. It must
  not overwrite another actor's state or assume disabling is blanket restoration.
- All callbacks/context and hook original slots remain valid through clean stop,
  or until process exit after uncertain mutation/restoration. A false/throwing
  callback permanently latches restart; no retry or further callback is allowed.

The legacy product has historical layout facts (`usable - 0x1C0`, drive charge
at `+0xCA8`, parameters through `+0x18`, input 582), but does **not** establish
local-owner identity, reversible shared-parameter ownership, first-prior charge
restoration or complete audio/effect teardown. Its `disableCharge = 1e9` write
is not a safe owned bridge. The effect handler null-skip and tag warm-up do not
prove restoration. Existing `quantum.*` rows locate functions, not those missing
contracts. Consequently no legacy memory write/hook is activated here. A grounded
engine adapter and supported admission collector remain required for gameplay;
Windows/MSVC and exact-game execution are not verified.

## Calls and ownership

All four methods require a live, non-quarantined plugin handle and the game
thread. Worker calls return `SCO_WRONG_THREAD` before accessing state. Released
or foreign handles return `SCO_BAD_ARG`. Terminal host shutdown refuses public
calls. Recursive calls during a bridge operation return `SCO_FAILED`.

- `enable(self, 1)` claims the single controller only after available-drive and
  explicit offline/anti-cheat-absent admission. Repeat enable by the same owner
  is idempotent. Another controller gets `SCO_FAILED`, never steals ownership.
  `enable(self, 0)` cancels holding and releases ownership; another owner's
  disable is refused. With no owner disable succeeds, including an empty bridge.
- `hold(self, milliseconds)` requires current ownership, available drive and no
  block. Duration is 1..1000 ms inclusive; zero and larger values are invalid.
  Each call refreshes the bounded lease from the latest game tick. A request
  before the first tick anchors to that first tick, never zero/absolute uptime.
  Missing ticks cannot provide wall-clock teardown: expiry occurs on a later tick.
  Unsigned tick subtraction handles ordinary 32-bit clock wrap.
- `release(self)` stops holding but keeps enable/ownership. Repeated release by
  that owner is idempotent; non-owner/unclaimed release returns `SCO_NOT_FOUND`.
  Cleanup is allowed even after drive/offline admission disappears; it only
  returns control to normal, never enables an unavailable drive.
- `state(self, out)` requires `out->size >= sizeof(sc_quantum_state)` (32 bytes),
  writes only that known prefix and returns copied enabled/held, phase, block,
  progress and opaque session generation. The generation is not an identity.
  Progress is integer millionths in 0..1000000. Unknown/unavailable observations
  return idle/zero progress with an explicit block and `SCO_UNAVAILABLE`.
  Invalid provider output is `SCO_FAILED` with restart-required block. Bad
  arguments/wrong thread/stopped admission leave output unchanged.

The game-pack tick samples admission and cancels controls if the drive becomes
unavailable, offline/EAC state becomes unknown, a block prevents holding, the
lease expires or the owner is released/quarantined. Ordinary owner unload clears
enable/hold through a runtime release hook. Stop withdraws the service, removes
its tick callback, clears controls and rolls back only its own hooks. The shared
spawn.cpp release dispatcher forwards owner cleanup without consuming a ninth
kernel release-hook slot. Standalone fixture composition registers that forwarder;
a nonempty configured game bridge must retain the shared dispatcher for its lifetime.
Mutation is refused without cleanup readiness even if observe/apply are supplied.
Repeated stop is harmless; a fresh successful start increments the generation.

The private bridge's apply(false, false) must clean up charging/audio/effects and
restore normal drive forwarding even without a currently available drive. That
obligation is tested with a synthetic control sink, **not engine effects**.
A failed/throwing bridge call or invalid observation permanently closes mutation
admission and requires process restart. The owner is recorded before the first
apply, so partial failure cannot make uncertain mutation appear unowned. The
latch suppresses all subsequent observation/apply/cleanup callbacks, including
public disable, tick, owner unload, stop and backend replacement. Backing bridge state and installed hooks
are retained when cleanup is uncertain. Direct native calls and memory corruption
are not sandboxed; C++ exception handling is not Windows SEH recovery. Retained
provider faults are not unconditional unload/reload safety.

## Hook and language boundary

A private start accepts at most four game-pack hook descriptors. Aliased targets
are refused before code writes; the shared `sco::hook` registry refuses targets
owned elsewhere. A transaction installs/rolls back the group. Installation
requires explicit offline and anti-cheat-absent observations. No descriptors or
engine addresses are accepted from plugins. Successful native hook installation
and original forwarding have **not** been exercised on this ARM64 development
host; alias refusal is a synthetic pre-install test.

C and C++ use the public table directly or its `sc_quantum_*` inline adapters,
which reject partial pointer extents and full-size null callbacks. Query through
`api->query_service` only after checking its complete member extent and non-null
pointer, then request exactly `SC_QUANTUM_SERVICE_VERSION`. Check capabilities
and results separately from table discovery. Borrowed table storage remains
mapped but a stopped session refuses calls; reacquire after a new session.

**C# and Lua quantum routes are unsupported.** No managed layout or sandbox
mutation binding is claimed. No end-to-end native plugin distribution or gameplay
acceptance is claimed by the synthetic provider fixture.

## Focused evidence

`tests/test_quantum.cpp` exercises the real host API, runtime tick/release and
quantum provider with copied synthetic observations: absent bridge/drive,
bounded hold expiry, cancellation, repeated enable/release/stop, competing owner,
released handle, wrong thread, offline/EAC-unknown refusal, session reset,
duplicate target refusal and restart-required invalid observation. The R1
activation fixture also proves configured production-start routing, absent
cleanup refusal without callbacks, capability publication only after successful
registration, first-tick and clock-wrap expiry, and zero callbacks after latching.
`tests/abi_game_quantum.c`, compiled independently as C11 and C++20, pins layout
and uses physically truncated allocations and full-size null tables under ASan.

Windows/MSVC, actual detour execution, NAV/boost input, charging/sound/effect
cleanup, normal drive restoration and quit during boost: **not verified; not run
in game**. Runtime networking, official servers, account/entitlement operations
and anti-cheat manipulation are outside the contract.
