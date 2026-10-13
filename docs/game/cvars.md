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
engine type before copying a bool. Default production adapter is absent and
`game.cvars.read` is OFF; signature matches alone never activate it. Capabilities
are published only after successful service registration.

Product `src/cvars.cpp` uses float virtual GetCVarNow/SetCVarNow calls. Their ABI
and actual engine types are NOT VERIFIED. A future guarded game-pack adapter must
verify each name/type, calling convention and lookup/get/set contract. If a
verified float bridge is necessary, accept only exact finite 0.0/1.0; do not
normalize arbitrary nonzero values. No guessed vtable calls were ported.

No arbitrary console execution, networking/anti-cheat cvars, quantum keeper
values, optional bridge values, game extracts or `USER.cfg` writes are included.
C and C++ are supported (C++ `scosdk::game::Cvars` checks complete member extent,
null callbacks and clears stale state on failed reopen). C# and Lua routes are
explicitly unsupported; no blanket binding parity is claimed.

Portable tests use the actual game-pack provider, neutral host registry and
native loader with synthetic engine callbacks; extent tests physically truncate
pointer tails under ASan/UBSan. macOS module loading is not Windows SEH, MSVC or
in-game acceptance. Owner-run Windows and four real-value observations remain owed.
