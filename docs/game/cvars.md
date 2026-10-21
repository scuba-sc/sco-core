# Scoped game cvars (G7 / #128)

`game.cvars` 1.0 (`sc_cvars_v1`, 16 bytes) reads canonical bool values on the
game thread using a live plugin handle. It is not `sco.settings`.

Only these exact, case-sensitive interoperability names are accepted:
- `i_target_selector.targeting2_enabled`
- `pl_lensdisplay.minimap_enabled`
- `pl_lensdisplay.greebles_enabled`
- `g_squadron_frontend`

The request type must be `SC_CVAR_BOOL`; outputs are copied `uint32_t` 0 or 1,
unchanged on failure. Unknown names/types and invalid/released handles are
`SCO_BAD_ARG`; other threads receive `SCO_WRONG_THREAD`. Missing readiness,
capability, admission or stopped runtime is `SCO_UNAVAILABLE`. Reentrant calls
are `SCO_FAILED`. An adapter-reported `SCO_CRASHED` latches restart-required;
no more adapter calls or replacement/restart are allowed in that process.

## Separate reversible writes (4B second acceptance)

`game.cvars_write` 1.0 (`sc_cvars_write_v1`, 24 bytes) has `set_bool` and
`restore_all`. `scosdk::game::CvarsWrite` validates the complete table extent and
both callbacks; failed reopen discards the old table. C# and Lua remain unsupported.
Writes need independently verified write callbacks, offline admission, read/write
capabilities and the existing spawn release dispatcher. They are unavailable if
that dispatcher failed to install; no ninth kernel release-hook slot was added.

One lease per exact name: the first canonical prior value is saved BEFORE engine
mutation; repeated same-owner updates preserve it. Another owner receives
`SCO_UNAVAILABLE` without touching the engine. All writes require readback match.
Explicit restore removes only the caller's leases and is idempotent when none exist.
Restoration ignores feature capability loss to permit cleanup, but public calls are
refused after session/terminal admission closes. No load-time mutation is performed
by the provider; the test-only consumer deliberately requests one write on load.

Ordinary unload/crash owner cleanup is forwarded by `spawn.cpp` to
`ReleaseCvarsOwner`. A `game.exit` subscription and `StopCvars` restore all leases
before closing admission. The host MUST emit session exit/stop while engine context
is still valid, before replacing/releasing it; an ended session does not auto-reopen
on `game.ready`. A fresh stop/install/start is required for the next reviewed port.
The copied port/context must outlive every outstanding lease and any retained fault.

A failed engine write, restore or post-write verification conservatively retains
unrestored leases, disables both capabilities and latches process restart-required.
There are no unsafe retries or port replacement after a fault; host diagnostics can
inspect `PendingCvarRestores`, and `StopCvars` returns `Failed` while withdrawing
tables. Never free retained engine context merely because discovery was withdrawn.
A loader-retained provider fault skips ordinary resource hooks; those leases remain
until the host-owned session/stop restoration (if the engine itself is still trusted).

Synthetic write fixtures cover native load/write/unload twice, actual runtime owner
release, actual loader retained-provider verdict, first-prior preservation,
contention, session exit/reset, stop, wrong thread/live handles, unavailable
admission, failed restore, failed write and false-success readback. The portable
release dispatcher is a labeled stand-in: Windows `spawn.cpp`/SEH and gameplay
remain unverified. Only engine callbacks are synthetic; the SDK host/loader are real.

## Engine boundary and evidence

Game-pack-only `src/game/services/cvars.h` defines a host-injected reviewed port.
Callbacks get a bounded index, never arbitrary command text or engine pointers.
Adapters must guard native faults, return `SCO_CRASHED`, and validate the actual
engine type before copying a bool. The Windows reference-integer adapter is
default OFF. `SCO_CVARS_NATIVE=1` requests the read-only adapter, requiring the
console, lookup, reference-integer vtable and method identities to resolve.
Every read re-queries the exact allowlisted name, validates its receiver/type and
method identities, checks fresh host admission, and copies only canonical 0/1.
Receiver drift is refused; native exceptions latch restart-required. Capabilities
are published only after successful service registration. Signature resolution
and this opt-in do not establish in-game acceptance.

Production native writes remain unavailable, including when an environment
variable requests them. Address/vtable/name equality does not rule out native
object destruction and same-address reuse across setter callbacks or a retained
lease. The adapter's setter and restore mechanics are exercised with synthetic
objects; no native lifetime pin or generation contract is claimed. Do not enable
the production write port until object and backing-storage lifetime are grounded.

For host teardown, ordinary fresh admission closes before unload. A separate
game-thread-only CVar cleanup scope is established only after a fresh positive
inspection. The optional native restore callback rechecks the full actual
offline admission predicate before access, immediately before mutation, and after
setter callbacks; ordinary plugin reads/writes cannot use this scope. Genuine
admission loss refuses restoration and retains the lease with restart-required.
The cleanup callback verifies readback itself before the provider clears a lease.
Legacy injected ports without it retain the ordinary write/readback restore path.

The Windows native-boundary fixtures cover callback-time admission/console/method
changes, rejected active/faulted reconfiguration, receiver drift and retained
leases. The host-stop fixture runs actual host/app Stop with the production spawn
cleanup dispatcher and synthetic engine objects. It verifies restoration while
offline evidence remains valid, no write after real admission loss, and refusal
of ordinary service calls and worker-thread cleanup during teardown. These tests
are not native-object lifetime or game evidence.

The production-route fixture (`tests/test_cvars_production.cpp`; CTest
`cvars_production_refusal`) uses the real signature registry with synthetic
resolver outputs and positive synthetic offline admission. Its assertions map to:
`CHECK(FreshAdmissionAllowed())` — positive-admission precondition only;
`CHECK(ConfigureProductionCvars() == Result::Ok)` followed by
`CHECK(read->get_bool(...))` — production configuration and successful canonical
read through the actual configured provider; `CHECK(write->set_bool(...) == SCO_UNAVAILABLE)`
— production write refusal even with `SCO_CVARS_NATIVE=1`; and
`CHECK(setters == 0 ...)` — no synthetic setter executes. These are assertion
descriptions, not executable test-case IDs. Synthetic host evidence only; not run
in game.

The existing boundary matrix maps as follows: `test_cvars.cpp` —
`cvars_four_exact_names_and_types`, `cvars_canonical_0_1_and_unchanged_outputs`,
and `cvars_read_fault_latches_restart`; `cvars_engine` —
`native_exact_allowlist_and_canonical_values`; `cvars_engine_fault` —
`native_SEH_latches_restart_and_preserves_output`; `cvars_engine_callback_admission`
— `fresh_admission_loss_refuses_native_access`; `cvars_engine_replace` and
`cvars_engine_callback_method` — `receiver_and_method_identity_drift_refuse`;
`cvars_engine_lease_replace` and `cvars_engine_lease_admission` —
`first_prior_lease_is_retained_when_safe_restore_cannot_proceed`; `cvars_host_stop`
— `stop_restores_lease_while_positive_cleanup_admission_remains`; and
`cvars_host_stop_loss` — `admission_loss_refuses_restore_and_retains_lease`.
The synthetic engine and host fixtures do not constitute in-game evidence.

The legacy product's float GetCVarNow/SetCVarNow calls are not the new adapter's
ABI. The new route uses independently checked reference-integer methods and
rejects arbitrary nonzero values rather than normalizing them. Native setters
preserve engine callbacks; direct backing-storage writes are not a substitute.

No arbitrary console execution, networking/anti-cheat cvars, quantum keeper
values, optional bridge values, game extracts or `USER.cfg` writes are included.
`game.cvars` still carries none of the quantum cvars.

**Maintainer decision (approved; scubamount (maintainer), 2026-10-15):** the
`game.quantum` backend, and only it, may write `v_qdrive2.quantumTravelAllowed`,
`v_qdrive2.quantumBoostAllowed`, `v_qdrive2.setting_ignoreBlockedBoost`,
`v_qdrive2.setting_ignoreBlockedTravel` and `p_enable_physical_quantum_travel`, and the
shared drive parameter cell v0.7.7 set to `1e9`, under the conditions in
[the amended rule](../quantum.md#amended-rule-2026-10-15): fresh positive offline
admission before every write, first prior saved first, every value restored on every
release path, engine type verified before any access, writes only while it holds
control, no console execution. The default adapter owns four of the five (int storage
rows); `p_enable_physical_quantum_travel` has no grounded route and is not written.
C and C++ are supported (C++ `scosdk::game::Cvars` checks complete member extent,
null callbacks and clears stale state on failed reopen). C# and Lua routes are
explicitly unsupported; no blanket binding parity is claimed.

Portable tests use the actual game-pack provider, neutral host registry and
native loader with synthetic engine callbacks; extent tests physically truncate
pointer tails under ASan/UBSan. macOS module loading is not Windows SEH, MSVC or
in-game acceptance. Owner-run Windows and four real-value observations remain owed.
