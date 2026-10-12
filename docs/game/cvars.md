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
