# Offline quantum control 1.0

`game.quantum` / `sc_quantum_v1` (`sc_quantum.h`) is a bounded game-pack service,
not a hook/patch API. The frozen `sco_api` 1.1 table is unchanged. Plugin input,
commands and UI remain in the consumer. The game pack owns control state, lease
expiry, hook transaction and all future drive/charging/audio/effect integration.
No player/account/ship identifiers, game pointers, raw block strings or memory
cross the public boundary; snapshots are copied into caller storage.

## Availability and evidence

**Production drive control remains unavailable.** The default game-pack start
publishes the table but `game.quantum.control` stays false. Its empty bridge
returns `SCO_UNAVAILABLE` / `SC_QUANTUM_BLOCK_UNAVAILABLE`. The private provider
bridge in `src/game/services/quantum.h` exists for game-pack integration and
explicitly synthetic tests only; it is not a shipped SDK header. There is no
engine implementation, inferred offset write, product callback fallback or
signature-only readiness claim in this change.

The existing `quantum.*` signature rows do not verify the drive layouts, call
ABI, NAV/input forwarding, charging, audio handles or effect teardown. Those
mechanics require a reviewed game-pack bridge and exact-build Windows evidence
before capability publication. Do not remove product boost based on this change
alone: consumer extraction and full charter 4C gameplay parity remain blocked.

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
  Each call refreshes the bounded lease from the latest game tick. Missing ticks
  cannot provide wall-clock teardown: expiry occurs at the next game tick.
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
a future nonempty game bridge must retain the shared dispatcher for its lifetime.
Repeated stop is harmless; a fresh successful start increments the generation.

The private bridge's apply(false, false) must clean up charging/audio/effects and
restore normal drive forwarding even without a currently available drive. That
obligation is tested with a synthetic control sink, **not engine effects**.
A failed/throwing bridge call or invalid observation permanently closes mutation
admission and requires process restart. Backing bridge state and installed hooks
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
duplicate target refusal and restart-required invalid observation.
`tests/abi_game_quantum.c`, compiled independently as C11 and C++20, pins layout
and uses physically truncated allocations and full-size null tables under ASan.

Windows/MSVC, actual detour execution, NAV/boost input, charging/sound/effect
cleanup, normal drive restoration and quit during boost: **not verified; not run
in game**. Runtime networking, official servers, account/entitlement operations
and anti-cheat manipulation are outside the contract.
