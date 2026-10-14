# Reputation rows

The rows `game.reputation` 1.0 ([`sc_reputation.h`](../../include/sc_reputation.h)) uses. All of them already existed: no row was added for the service. They are the reputation-service check rows of [sc-offline's own features](features.md) ([`src/game/features_sigs.cpp`](../../src/game/features_sigs.cpp), [`include/sco/game/features.h`](../../include/sco/game/features.h)), grouped there as the capability `contracts.reputation`, which sc-offline's offline contracts feature patches ([contracts rows](contracts.md)). The policy (thread, self and argument checks, the capability gate, `last_error`) is in [`src/game/services/reputation.cpp`](../../src/game/services/reputation.cpp). The engine reads are in [`src/game/services/reputation_engine.cpp`](../../src/game/services/reputation_engine.cpp). It calls nothing in the game, installs no hook and writes no patch.

**The capability is OFF by default** (evidence level `off_pending_probe`, [status](status.md)). The rows resolve, but nobody has run these reads from a plugin in a game session. The game pack holds the capability off until such a run confirms it. While it is off, `checks` answers `SCO_UNAVAILABLE` with the reason in `last_error`, and the game is not touched. Nothing on this page has been verified in game.

| Capability | Function | Rows it reads | What it does with them |
|---|---|---|---|
| `game.reputation.checks` | `checks` | `contracts.reputation_check.1` .. `.10` | `sites`: how many check sites the rows located on this build. `intact`: how many of them still match the check form their row matched, from the services load through the check's call. The offline contracts feature writes its patch inside that span, so a site that no longer matches has been rewritten since the game loaded |
| `game.reputation.checks` | `checks` | `contracts.reputation_services` | `services`: whether the services global the reputation-service checks load holds an object (1) or is null (0) |

What the repository documents about these rows, and so all this service relies on: `contracts.reputation_services` is "the services global the reputation checks load", and each `contracts.reputation_check.N` is a check site on that global, where "contracts writes its patch" ([`features.h`](../../include/sco/game/features.h)). Rows only find addresses; sc-offline writes the patches ([features rows](features.md)).

Not wrapped, because the repository doesn't document a meaning for them:

- **Standing.** No row locates a player's reputation or standing value, a faction, or the call that reads one. The check sites load the services global and call through it, but which service they reach and what that call returns is not documented, so `game.reputation` makes no such call.
- **Standing changes.** No row or document describes adjusting a standing, so there is no write.
- **Turning the checks off or on.** The patch at the check sites belongs to sc-offline's contracts feature, and its bytes aren't documented here. `game.reputation` only reports whether a site is still the game's own.

[API header](../../include/sc_reputation.h), [game services](../game-services.md).

## C++ wrapper

[`scosdk/game/reputation.hpp`](../../include/scosdk/game/reputation.hpp) (header-only, not in `scosdk.hpp`) wraps the table as `sco::sdk::game::Reputation`: `Open(plugin)` queries `game.reputation` 1.0, then `Checks(out)` and `LastError()`. `Checks` zeroes `out` and sets its `size` before the call, so a failed call leaves it zero apart from `size`; like `LastError`, it checks that the published table's `size` covers the member and that the member is set, and answers `SCO_UNAVAILABLE` otherwise. `game.reputation.checks` stays OFF by default, so on a stock product `Checks` answers `SCO_UNAVAILABLE`. Tests: `tests/test_sdk.cpp` (`TestGameReputation`, over a stand-in table) and `tests/test_language_routes.cpp` (the size sweep for each member). Nothing here has been verified in game.
