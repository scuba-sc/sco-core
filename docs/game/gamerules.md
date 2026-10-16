# game.gamerules — read-only server GameRules parameters

`sc_gamerules.h` · service `game.gamerules` · version 1.0 · game thread only ·
capability `game.gamerules.read` (default **OFF**).

The server tells the client which game rules apply: whether quantum travel and quantum
boost are allowed, whether group QT is enforced, and the travel/target-lock thresholds the
HUD and driving code obey. Plugins currently cannot see any of it — a server gate that is
off looks identical to a feature that is available. `game.gamerules.read` returns the
client's received values for the current session.

The plugin ABI is the header; the rest of this page is for the game pack and hosts.

## Where the values live

The client's GameRules params sync stores the server parameter set behind a module
accessor chain (evidence: private corpus, F-4.10.196.36804-007/-008; verified at
4.10.196.36804 and the 4.10.197.2878 hotfix):

* the module-name string `IGameRulesParamsModule` in `.rdata` — the row anchor;
* `gamerules.params_sync` resolves the sync function (label-anchored lea scan filtered by
  prologue; the row fails loudly on adds/losses, never silently re-picks);
* the parameter blob is reached through the module accessor; observed offsets: byte
  +8 enforce-group-QT, +9 quantum-travel-allowed, +10 quantum-boost-allowed, floats
  +0xC/+0x10/+0x14/+0x18/+0x1C/+0x20/+0x24 (thresholds).

Offsets enter the provider's engine adapter (host side), never the public header: the
header exposes named fields only. No pointer, no write, no action crosses the ABI.

## Backend binding

`services::StartGameRules(backend)` publishes the table. With no backend bound (default
production config) `read` returns `SCO_UNAVAILABLE`; the capability stays OFF. A host
binds a backend only after grounding the layout on its exact build (rule 8 evidence:
sigcheck rows OK on that build). Values are a snapshot of what the client received;
`generation` changes when a new parameter set is applied, so a plugin can detect updates
without polling intervals.

## Boundaries

Read-only by contract (rule 11: interoperability, not enforcement): the service exists to
explain *why* travel features are unavailable — for example, `game.quantum`'s physical
travel cvar is written by the game itself exactly when this module reports
quantum-travel-allowed (F-4.10.196.36804-007). A plugin that wants travel is looking at
the server's answer, not a lock to work around.

## Language routes

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
