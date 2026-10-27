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

**A default production adapter exists (Windows game pack; synthetic evidence only).**
`src/game/services/quantum_engine.cpp` ports the sc-offline v0.7.7 engine code under
the [amended rule](#amended-rule-2026-10-15). `spatial.cpp` configures it before
`StartProductionQuantum` only when every row it needs resolves **and** fresh positive
offline admission holds at that moment; otherwise the empty configuration stays: the
table remains discoverable but capability is OFF and calls return `SCO_UNAVAILABLE` /
`SC_QUANTUM_BLOCK_UNAVAILABLE`. Every layout constant is a v0.7.7 value and every
function and cvar storage is a sigcheck-resolved row; no offset is inferred and there
is no signature-only readiness. It has never run in the game: real boost parity is
**not verified**. Plugin input/commands/UI use the unchanged public table.

### Explicit host-source integration contract

`src/game/services/quantum.h` is private game-pack/host-source code, **not a
plugin SDK header**. Serialized host setup may call `ConfigureProductionQuantum`
before services start. A complete observe/apply pair is mandatory; an empty pair
clears configuration. An explicit backend takes precedence over the implicit default
at normal startup; an implicit default can be refreshed on a later stopped-state start.
A selected backend that cannot start is not replaced by a fallback. Successful stop clears
the selection, including after a non-faulted failed start; hosts must register again for
another lifecycle. Stop must run on the game thread outside a callback. A failed implicit refresh
leaves the service unavailable. Replacement while started, inside a callback or after a
restart-required fault is refused. Configuration performs no engine call.
`spatial.cpp` starts through `StartProductionQuantumWithDefault`, passing
`SpawnReleaseHooked()`, so actual
shared dispatcher installation gates mutation and hook installation; merely
having a release-dispatch function compiled is insufficient. Standalone fixture
hosts must install the forwarder before asserting cleanup readiness.

A supplied adapter must establish all of these obligations:

- Positively inspected offline/EAC-absent admission, never derived from matching
  rows, launcher flags, an absent DLL alone or transport installation. Unknown
  stays false. Observe supplies copied phase/block/progress within public bounds.
- Exact-build call/layout validation and live **local-player-owned** drive
  association; identity/lifetime validation before every write and restoration.
- Save first-prior state before each mutation; write only owned state, plus the
  cvars and the one shared drive parameter cell the amended rule below permits.
  Validate every value separately: finite charge in 0..1, input mode/action on an
  exact allowlist, input value only 0/1, and bounded effect operations. The provider itself passes only boolean controls and a
  1..1000 ms lease; it does not authorize arbitrary field/action writes.
- `apply(false,false)` restores only this adapter's changed input/charge state
  and owned sound/effect activity, including after drive/admission loss. It must
  not overwrite another actor's state or assume disabling is blanket restoration.
- All callbacks/context and hook original slots remain valid through clean stop,
  or until process exit after uncertain mutation/restoration. A false/throwing
  callback permanently latches restart; no retry or further callback is allowed.

The legacy product's historical layout facts (`usable - 0x1C0`, drive charge at
`+0xCA8`, parameters through `+0x18`, input 582) are what the default adapter
uses; its unguarded legacy behaviour (global keeper, unrestored `disableCharge =
1e9`, uncounted charge write) is not carried over. The effect-tag warm-up, sound
stages and the engage-key hook (`quantum.on_action`) are **not** ported: they have
no restoration proof and the public table already carries input. Windows/MSVC and
exact-game execution are not verified.

### Amended rule (2026-10-15)

**Maintainer decision (approved; scubamount (maintainer), 2026-10-15):** a quantum
backend may (a) write the cvars the v0.7.7 keeper forced -
`v_qdrive2.quantumTravelAllowed`, `v_qdrive2.quantumBoostAllowed`,
`v_qdrive2.setting_ignoreBlockedBoost`, `v_qdrive2.setting_ignoreBlockedTravel` and
`p_enable_physical_quantum_travel` (not logging or the target-lock thresholds, unless
the boost provably needs them) - and (b) write the shared drive parameter value
v0.7.7 set (the drive's parameter record at `+0xC0`, `1e9`), **only if all of**:

1. positive fresh offline admission (`FreshAdmissionAllowed`) precedes every write;
2. the first prior value of each cvar and field is saved before the first write;
3. every saved value is restored on release, `apply(false, false)`, admission loss,
   drive loss, stop and fault, and a restoration failure latches restart as above;
4. each cvar's actual engine type is verified before any read or write (if the float
   `GetCVarNow`/`SetCVarNow` ABI cannot be grounded, a different verified route is
   used or the cvar is not written);
5. writes happen only while the backend holds control.

No arbitrary console execution is allowed. Ownership, the single controller,
bounded values and the restart latch stay as written above.

How the default adapter applies it:

- **Holding control** means an accepted `hold` whose lease has not ended. `enable`
  alone writes nothing; release, lease expiry, disable, owner unload, stop, admission
  loss, drive loss and fault all restore. A value another actor changed after the
  adapter's write is left alone on restore.
- **Cvars.** The four `v_qdrive2` cvars are written through their sigcheck-resolved
  int storage rows (`cvars.quantum_travel_allowed`, `cvars.quantum_boost_allowed`,
  `cvars.ignore_blocked_boost`, `cvars.ignore_blocked_travel`). The row resolver finds
  each storage through the cvar's int registration (vtable slot `0x40`), which is the
  type check; a row that does not resolve, or a type other than int, refuses before
  any read or write. Writes are read back. The console is never used.
- **`p_enable_physical_quantum_travel` is not written.** Its registration passes
  member storage (`lea r8, [rbx+0x4A0]`, no global), so no sigcheck row can ground it,
  and the float console ABI (`0xC0`/`0x20`/`0x38`) is present only in v0.7.7 and
  documented as unverified in [game cvars](game/cvars.md). It stays unowned until a
  verified route exists.
- **Drive parameter cell.** Written only while holding, only when the current value is
  below `1e9` (the data pack's `quantum_drive.toml` already sets `1e9` on the pack's
  drive, so nothing is written or restored there), with the prior saved first and
  restored only if the cell still holds the adapter's value.
- **Ownership.** The `StartUse` hook compares its `user` argument with
  `reads::LocalPlayer`'s actor and entity and tracks the drive only on an exact match;
  every other call passes through untouched. Which of the two the engine passes there
  is not verified in game; a non-match fails closed.
- **Charge and effects.** The v0.7.7 keep-full charge write and the effect-handler
  null-skip run only while holding; the charge prior is saved first and restored.

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
`tests/test_quantum_owned.cpp` drives the default adapter's bookkeeping
(`quantum_owned.cpp`) with a fake engine that logs every read, write and admission
check: first-prior saved before each write with admission immediately before it,
restore on `apply(false,false)`, hold release, lease expiry, admission loss, drive
loss, stop and owner unload (the last four through the real provider tick), a
non-local `StartUse` user passing through untouched, a non-int cvar type refusing
before any read or write, charge keep-full with its prior restored, and a failed
restore latching restart so no later callback runs. The Windows engine access in
`quantum_engine.cpp` is **not** exercised by any test.
`tests/abi_game_quantum.c`, compiled independently as C11 and C++20, pins layout
and uses physically truncated allocations and full-size null tables under ASan.

Windows/MSVC, actual detour execution, NAV/boost input, charging/sound/effect
cleanup, normal drive restoration and quit during boost: **not verified; not run
in game**. Runtime networking, official servers, account/entitlement operations
and anti-cheat manipulation are outside the contract.
