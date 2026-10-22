# game.gamerules — read-only server GameRules parameters

`sc_gamerules.h` · service `game.gamerules` · version 1.1 (`read` is the frozen 1.0 prefix;
1.1 adds `list_params`) · game thread only · capability `game.gamerules.read` (default **OFF**,
gates `read` only).

The server tells the client which game rules apply: whether quantum travel and quantum
boost are allowed, whether group QT is enforced, and the travel/target-lock thresholds the
HUD and driving code obey. Plugins currently cannot see any of it — a server gate that is
off looks identical to a feature that is available. `game.gamerules.read` returns the
client's received values for the current session.

The plugin ABI is the header; the rest of this page is for the game pack and hosts.

## Where the values live

The client's GameRules params sync stores the server parameter set behind a module
accessor chain (evidence: F-4.10.196.36804-007/-008; verified at
4.10.196.36804 and the 4.10.197.2878 hotfix):

* the module-name string `IGameRulesParamsModule` in `.rdata` — the row anchor;
* `gamerules.params_sync` resolves the sync function (label-anchored lea scan filtered by
  prologue; the row fails loudly on adds/losses, never silently re-picks);
* the parameter blob is reached through the module accessor; observed offsets: byte
  +8 enforce-group-QT, +9 quantum-travel-allowed, +10 quantum-boost-allowed, floats
  +0xC/+0x10/+0x14/+0x18/+0x1C/+0x20/+0x24. That is seven float slots; the header names six
  of them as thresholds (`travel_target_threshold_angle`, the four `target_lock_*` values and
  `boost_group_distance_threshold`) and does not expose the seventh. No backend maps the slots
  yet.

Offsets enter the provider's engine adapter (host side), never the public header: the
header exposes named fields only. No pointer, no write, no action crosses the ABI.

## Backend binding

Reads reject foreign threads and null, unknown or released plugin owners before
backend access. Backend exceptions and native faults return `SCO_FAILED` once,
then latch reads unavailable until process restart. Recursive reads and lifecycle
replacement from a backend callback refuse. Output is copied only after success.

`services::StartGameRules(backend)` publishes the table. With no backend bound (default
production config) `read` returns `SCO_UNAVAILABLE`; the capability stays OFF. A host
binds a backend only after grounding the layout on its exact build (rule 8 evidence:
sigcheck rows OK on that build). Values are a snapshot of what the client received;
`generation` changes when a new parameter set is applied, so a plugin can detect updates
without polling intervals.

## Parameter registry (`list_params`, 1.1)

`list_params` is a static list of parameter *definitions*: a name, a group and a type per
entry. It never reports a value, reads no game memory, calls no backend and needs neither the
`game.gamerules.read` capability nor a bound backend, so it answers the same on every build.
`read` is unchanged: without a backend it stays `SCO_UNAVAILABLE`, and listing a definition is
not evidence that its value can be read.

The registry has 15 entries: the GameRules console variables whose names occur on the verified
build 4.10.197.2878 as `g_gameRules.<group>.<name>` or `g_gameRules.<name>`
(F-4.10.197.2878-054), in a fixed order. The names and groups are verified on that build; the
`INT`/`FLOAT` types are not verified on a captured build. These are console variables, not the
fields of `sc_gamerules_state`. Each record is
`sc_gamerules_param_info`: `size`, `type` (`SC_GAMERULES_PARAM_INT` or `_FLOAT`), `name[48]`
and `group[32]`, both NUL-terminated copies; `group` is the console-variable group that holds
the entry (`control`, `state`, `scoring`, `damagehandling`) and is empty for a top-level
variable. No offset or engine pointer is exposed.

Table layout: `read` stays at offset 8 and the 1.0 extent is 16 bytes, so a 1.0 caller and its
old 16-byte record keep working; `list_params` is appended at offset 16 and the 1.1 table is 24
bytes. The service is published as 1.1; a query for 1.0 (`SC_GAMERULES_SERVICE_VERSION`) still
returns the same table. Check `table->size >= offsetof(list_params) + sizeof(list_params)` and
a non-null member first, as `sc_gamerules_list_params` does.

```c
sco_result list_params(sco_plugin* self, uint32_t offset, sc_gamerules_param_info* out,
                       uint32_t stride, uint32_t cap, uint32_t* out_count, uint32_t* out_total);
```

Enumeration is deterministic, the registry order is fixed, and `offset` is an element index:

* `out_total` is the registry size and `out_count` the records copied; the list is complete when
  `offset + out_count == out_total`. A short last page is normal. `offset == total` is valid and
  copies nothing.
* `cap == 0` is a count-only call: `out` must be NULL, `stride` is ignored, `out_count` is 0 and
  `out_total` is set. Otherwise `cap` is at most `SC_GAMERULES_PARAM_MAX_PAGE` (64), `out` is
  non-NULL and `stride` is in `[sizeof(sc_gamerules_param_info), SC_GAMERULES_PARAM_MAX_STRIDE]`
  (4096); slot `i` is at `(char*)out + i * stride`.
* Every slot that will be written must advertise `size >= sizeof(sc_gamerules_param_info)`.
  Only `type`, `name` and `group` are written: the caller's `size` and any bytes past the known
  fields are preserved.
* Order of checks: wrong thread is `SCO_WRONG_THREAD`; a NULL, unknown or released `self` is
  `SCO_BAD_ARG`; then `SCO_BAD_ARG` for a NULL or aliased `out_count`/`out_total`, `offset > total`,
  a bad `cap`, `out` or `stride`, or a slot size that is too small; then `SCO_UNAVAILABLE` once the
  service is stopped or the runtime is stopping.
* Every failure leaves the buffer and both counters unchanged, including the valid slots before a
  short one. The call allocates nothing and costs O(count).

Tests: `tests/abi_game_gamerules.c` (layout, frozen 1.0 prefix and the helper's extent checks) and
`tests/test_gamerules_params.cpp` (the real host API with service start and stop). Not run in game.

## Boundaries

Read-only by contract (rule 11: interoperability, not enforcement): the service exists to
explain *why* travel features are unavailable — for example, `game.quantum`'s physical
travel cvar is written by the game itself exactly when this module reports
quantum-travel-allowed (F-4.10.196.36804-007). A plugin that wants travel is looking at
the server's answer, not a lock to work around.

## Language routes

The new `list_params` method is supported through the C table; typed C++/C#/Lua
wrappers for enumeration are unsupported. Existing read wrappers are unchanged.

Read-only in every language; nothing writes a value, and no route is a way around a server gate.

| Language | Route | Notes |
|---|---|---|
| C | `sc_gamerules_read(table, self, &state)` in `sc_gamerules.h` | direct; checks the table extent and the member |
| C++20 | [`scosdk/game/gamerules.hpp`](../../include/scosdk/game/gamerules.hpp): `sco::sdk::game::GameRules`, `Open(plugin)` then `Read(state)` | header-only, not in `scosdk.hpp`; `Read` zeroes `state` and sets its `size` first, so a failure leaves it zero apart from `size`; answers `SCO_UNAVAILABLE` for a short table or a null member |
| C# | `Sco.Sdk.Game.GameRules`: `Open(plugin)`, `Read(out ScGamerulesState)` | same zero-and-size rule; `Unavailable` for a short table, a null member or a table that is not open |
| Lua | `sco.game.gamerules.read()` | present only when the host publishes `game.gamerules`; returns a table of `valid`, `generation`, the three server gates as booleans and the six thresholds as numbers, or `nil, "<result name>"` |

Check `valid` (`state.valid`, `s.valid`) first: a result of OK with `valid == 0` means the client has not received a
parameter set yet, and every other field is zero. The table has no `last_error` member, so the wrappers
carry a result and no message. Tests: `tests/test_sdk.cpp` and `tests/test_language_routes.cpp` (C++, including the
extent sweep), the fake-host test of `sdk/csharp/Sco.Sdk.Tests` (C#, including the layout and extent checks) and
`tests/test_lua.cpp` (Lua). None of them runs in game: not run in game.
