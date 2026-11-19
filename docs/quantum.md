# Offline quantum control 1.0

`game.quantum` (`sc_quantum_v1`, header `sc_quantum.h`) is a bounded game-pack service for controlling the ship's quantum drive. It is not a hook or patch API, and the frozen `sco_api` 1.1 table is unchanged.

The plugin keeps its own input, commands and UI. The game pack owns the control state, the lease expiry, the hook transaction and the drive, charging, audio and effect integration the host supplies. No player, account or ship identifiers, game pointers, raw block strings or memory cross the public boundary. Snapshots are copied into caller storage.

Not run in game. See [game capability status](game/status.md).

## Calls and ownership

All four methods need a live, non-quarantined plugin handle and the game thread. A worker thread gets `SCO_WRONG_THREAD` before any state is read. A released or foreign handle gets `SCO_BAD_ARG`. Terminal host shutdown refuses public calls. A recursive call during a bridge operation returns `SCO_FAILED`.

- `enable(self, 1)` claims the single controller, and only after a drive is available and offline, anti-cheat-absent admission is positive. Repeating `enable` as the same owner is idempotent. Another controller gets `SCO_FAILED` and never steals ownership. `enable(self, 0)` cancels holding and releases ownership; another owner's disable is refused. With no owner, disable succeeds, including on an empty bridge.
- `hold(self, milliseconds)` needs current ownership, an available drive and no block. The duration is 1 to 1000 ms inclusive (`SC_QUANTUM_MAX_HOLD_MS`); zero and larger values are invalid. Each call refreshes the lease from the latest game tick. A request before the first tick anchors to that tick, never to zero or absolute uptime. Without ticks there is no wall-clock teardown: expiry happens on a later tick. Unsigned tick subtraction handles ordinary 32-bit clock wrap.
- `release(self)` stops holding but keeps `enable` and ownership. Repeating it as the owner is idempotent. A non-owner, or a release with no owner, gets `SCO_NOT_FOUND`. Cleanup is allowed after the drive or offline admission disappears. It only returns control to normal and never enables an unavailable drive.
- `state(self, out)` needs `out->size >= sizeof(sc_quantum_state)` (32 bytes). It writes only that known prefix and returns copied enabled and held flags, phase, block, progress and an opaque session generation. The generation is not an identity. Progress is an integer in millionths, 0 to 1000000. An unknown or unavailable observation returns idle and zero progress with an explicit block and `SCO_UNAVAILABLE`. Invalid provider output returns `SCO_FAILED` with a restart-required block. Bad arguments, the wrong thread or stopped admission leave the output unchanged.

On every game tick the game pack samples admission. It cancels control when the drive becomes unavailable, offline or anti-cheat state becomes unknown, a block prevents holding, the lease expires, or the owner is released or quarantined. An ordinary owner unload clears `enable` and `hold` through a runtime release hook. `Stop` withdraws the service, removes its tick callback, clears controls and rolls back only its own hooks. Repeated stop is harmless, and a fresh successful start increments the generation.

If a bridge call fails or throws, or an observation is invalid, mutation admission closes permanently and the process must restart:

- The owner is recorded before the first apply, so a partial failure cannot make an uncertain mutation look unowned.
- The latch suppresses all later observe, apply and cleanup callbacks, including public disable, tick, owner unload, stop and backend replacement.
- Backing bridge state and installed hooks stay retained when cleanup is uncertain.
- Direct native calls and memory corruption are not sandboxed. C++ exception handling is not Windows SEH recovery. A retained-provider fault does not make unload and reload safe.

## Languages

C and C++ use the public table directly, or its `sc_quantum_*` inline adapters, which reject partial pointer extents and full-size null callbacks. Query `game.quantum` through `api->query_service` only after checking the full member extent and a non-null pointer, then request exactly `SC_QUANTUM_SERVICE_VERSION`. Check capabilities and results separately from table discovery. Borrowed table storage stays mapped, but a stopped session refuses calls, so reacquire the table after a new session.

C# and Lua quantum routes are unsupported. No managed layout or sandbox mutation binding exists.

## Availability and evidence

`game.quantum.control` is ON after successful publication with a complete configured host backend and an installed owner-cleanup dispatcher. ON means the bounded provider is installed. It does not mean the current drive or session is admitted: every `enable` and `hold` still checks fresh, explicit offline and anti-cheat-absent observations, drive availability, owner and game thread.

A default production adapter exists in the Windows game pack. `src/game/services/quantum_engine.cpp` ports the sc-offline v0.7.7 engine code under the [amended rule](#amended-rule-2026-10-15). `spatial.cpp` configures it before `StartProductionQuantum`, and only when every row it needs resolves and fresh positive offline admission holds at that moment. Otherwise the empty configuration stays: the table is still discoverable, the capability is OFF, and calls return `SCO_UNAVAILABLE` or `SC_QUANTUM_BLOCK_UNAVAILABLE`.

Every layout constant is a v0.7.7 value. Every function and cvar storage address is a row resolved by sigcheck. No offset is inferred, and signatures alone never make the service ready. The adapter has synthetic evidence only, so real boost behavior is unknown.

### Host-source integration contract

`src/game/services/quantum.h` is private game-pack and host-source code. It is not a plugin SDK header.

- Serialized host setup can call `ConfigureProductionQuantum` before services start. A complete observe and apply pair is mandatory. An empty pair clears the configuration. Configuration performs no engine call.
- An explicit backend takes precedence over the implicit default at normal startup. An implicit default can be refreshed on a later stopped-state start. A selected backend that cannot start is not replaced by a fallback, and a failed implicit refresh leaves the service unavailable.
- A successful stop clears the selection, including after a non-faulted failed start, so a host must register again for another lifecycle. Stop must run on the game thread outside a callback.
- Replacement while started, inside a callback, or after a restart-required fault is refused.
- `spatial.cpp` starts through `StartProductionQuantumWithDefault` and passes `SpawnReleaseHooked()`. The shared release dispatcher must really be installed before mutation or hook installation is allowed. Having a release-dispatch function compiled is not enough. Mutation is refused without cleanup readiness even when observe and apply are supplied. The shared `spawn.cpp` dispatcher forwards owner cleanup without using a ninth kernel release-hook slot. A standalone fixture host must install that forwarder before it asserts cleanup readiness, and a nonempty configured bridge must keep the dispatcher for its whole lifetime.

A supplied adapter must meet all of these obligations:

- Admission comes from a positively inspected offline, anti-cheat-absent state. It is never derived from matching rows, launcher flags, an absent DLL alone or an installed transport. Unknown stays false. Observe supplies copied phase, block and progress within the public bounds.
- Call and layout validation is exact-build. The drive must be associated with the local player, and identity and lifetime are validated before every write and every restoration.
- The adapter saves the first prior state before each mutation and writes only state it owns, plus the cvars and the one shared drive parameter cell the amended rule permits. Each value is validated separately: charge is finite and in 0..1, input mode and action are on an exact allowlist, input value is 0 or 1, and effect operations are bounded. The provider itself passes only boolean controls and a 1 to 1000 ms lease. It does not authorize arbitrary field or action writes.
- `apply(false, false)` restores only this adapter's changed input and charge state and its owned sound and effect activity, including after the drive or admission is lost. It must not overwrite another actor's state or treat disabling as blanket restoration. The private bridge's `apply(false, false)` must also restore normal drive forwarding without a currently available drive. Tests cover this with a synthetic control sink, not engine effects.
- All callbacks, their context and the hook original slots stay valid through a clean stop, or until process exit after an uncertain mutation or restoration. A false or throwing callback latches restart for good, with no retry and no further callback.

The default adapter uses sc-offline v0.7.7's layout facts (`usable - 0x1C0`, drive charge at `+0xCA8`, parameters through `+0x18`, input 582). It does not carry over that code's unguarded behavior: a global keeper, an unrestored `disableCharge = 1e9` and an uncounted charge write. The effect-tag warm-up, the sound stages and the engage-key hook (`quantum.on_action`) are not ported. They have no restoration proof, and the public table already carries input. Windows/MSVC and exact-game execution have not been checked.

A private start accepts at most four game-pack hook descriptors. Aliased targets are refused before any code write, and the shared `sco::hook` registry refuses targets owned elsewhere. A transaction installs or rolls back the whole group, and installation needs explicit offline and anti-cheat-absent observations. Plugins supply no descriptors or engine addresses. Native hook installation and original forwarding have not been exercised. Alias refusal is a synthetic pre-install test.

### Amended rule (2026-10-15)

The maintainer decided on 2026-10-15 that a quantum backend may write two kinds of state. First, the cvars the v0.7.7 keeper forced: `v_qdrive2.quantumTravelAllowed`, `v_qdrive2.quantumBoostAllowed`, `v_qdrive2.setting_ignoreBlockedBoost`, `v_qdrive2.setting_ignoreBlockedTravel` and `p_enable_physical_quantum_travel`. Logging and the target-lock thresholds stay off limits unless the boost provably needs them. Second, the shared drive parameter value v0.7.7 set (the drive's parameter record at `+0xC0`, `1e9`). It may do so only if all of these hold:

1. Positive fresh offline admission (`FreshAdmissionAllowed`) precedes every write.
2. The first prior value of each cvar and field is saved before the first write.
3. Every saved value is restored on release, `apply(false, false)`, admission loss, drive loss, stop and fault. A restoration failure latches restart as above.
4. Each cvar's actual engine type is verified before any read or write. If the float `GetCVarNow` and `SetCVarNow` ABI cannot be grounded, a different verified route is used or the cvar is not written.
5. Writes happen only while the backend holds control.

No arbitrary console execution is allowed. Ownership, the single controller, bounded values and the restart latch stay as described above.

How the default adapter applies the rule:

- **Holding control** means an accepted `hold` whose lease has not ended. `enable` alone writes nothing. Release, lease expiry, disable, owner unload, stop, admission loss, drive loss and fault all restore. A value another actor changed after the adapter's write is left alone on restore.
- **Cvars.** The four `v_qdrive2` cvars are written through their sigcheck-resolved int storage rows (`cvars.quantum_travel_allowed`, `cvars.quantum_boost_allowed`, `cvars.ignore_blocked_boost`, `cvars.ignore_blocked_travel`). The row resolver finds each storage through the cvar's int registration (vtable slot `0x40`), and that is the type check. A row that does not resolve, or a type other than int, refuses before any read or write. Writes are read back. The console is never used.
- **`p_enable_physical_quantum_travel` is not written.** Its registration passes member storage (`lea r8, [rbx+0x4A0]`, no global), so no sigcheck row can ground it. The float console ABI (`0xC0`/`0x20`/`0x38`) is present only in v0.7.7 and is documented as unverified in [game cvars](game/cvars.md). The cvar stays unowned until a verified route exists.
- **Drive parameter cell.** Written only while holding, and only when the current value is below `1e9`. The data pack's `quantum_drive.toml` already sets `1e9` on the pack's drive, so nothing is written or restored there. The prior value is saved first and restored only if the cell still holds the adapter's value.
- **Ownership.** The `StartUse` hook compares its `user` argument with the actor and entity of `reads::LocalPlayer`, and tracks the drive only on an exact match. Every other call passes through untouched. It is not known which of the two the engine passes there, and a non-match fails closed.
- **Charge and effects.** The v0.7.7 keep-full charge write and the effect-handler null-skip run only while holding. The charge prior is saved first and restored.

## Tests

`tests/test_quantum.cpp` exercises the real host API, the runtime tick and release, and the quantum provider with copied synthetic observations:

- absent bridge or drive, bounded hold expiry, cancellation, repeated enable, release and stop
- a competing owner, a released handle, the wrong thread, offline or anti-cheat-unknown refusal, session reset
- duplicate target refusal and restart-required invalid observation
- configured production-start routing, absent-cleanup refusal without callbacks, capability publication only after successful registration, first-tick and clock-wrap expiry, and zero callbacks after latching

`tests/test_quantum_owned.cpp` drives the default adapter's bookkeeping (`quantum_owned.cpp`) with a fake engine that logs every read, write and admission check:

- first prior saved before each write, with admission checked immediately before it
- restore on `apply(false, false)`, hold release, lease expiry, admission loss, drive loss, stop and owner unload (the last four through the real provider tick)
- a non-local `StartUse` user passing through untouched
- a non-int cvar type refusing before any read or write
- charge keep-full with its prior restored
- a failed restore latching restart so no later callback runs

`tests/abi_game_quantum.c`, compiled independently as C11 and C++20, pins the layout and uses physically truncated allocations and full-size null tables under ASan.

No test exercises the Windows engine access in `quantum_engine.cpp`. Windows/MSVC, actual detour execution, NAV and boost input, charging, sound and effect cleanup, normal drive restoration and quit during boost have not been checked. Runtime networking, official servers, account and entitlement operations and anti-cheat manipulation are outside the contract.
