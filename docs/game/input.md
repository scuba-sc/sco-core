# game.input 1.0

The declared service is a read-only action vocabulary. Consumers discover it
through `query_service` when a provider publishes it. Include `sc_input.h`; use
`SC_INPUT_SERVICE_NAME` and `SC_INPUT_SERVICE_VERSION`.
The frozen kernel ABI is unchanged. No input is dispatched and no key binding is read.

`count` reports 1,297 named actions. Valid ids are exactly 1..1297 with no gaps,
and each id has exactly one name. 0 (`SC_ACTION_INVALID`, an SDK-only "no action"
value) and ids above 1297 are not actions. Id lookup is O(1); exact
case-sensitive name lookup is binary search. Unknown names or ids return NOT_FOUND.

All methods require the game thread and a live plugin handle. Records begin with
`size`; set it before lookup. Null pointers, undersized records, empty names or
names without a terminator in 96 bytes are BAD_ARG. Failure leaves outputs unchanged.
The copied name is always NUL-terminated. Stop withdraws the service; retained tables
return UNAVAILABLE. A newer same-major caller must still check table size.

## Evidence and limits

Ids and names come from the action registry of game build 4.10.197.2878 itself,
re-derived statically from that build's code (F-4.10.197.2878-050): two independent
id-to-name tables in the build agree on all 1,297 actions, and its name-to-id
routine agrees on 1,296. Ids are build-scoped: another build may add, remove or
renumber actions, so the `SC_ACTION_*` constants describe 4.10.197.2878 only.
No record sets `SC_INPUT_ACTION_BUILD_UNRESOLVED` or `SC_INPUT_ACTION_SENTINEL`;
both flag bits stay reserved. Metadata is not proof that an id is safe to dispatch.
No game addresses or engine pointers cross the ABI. The capability
`game.input.registry` advertises metadata availability only.

Not run in game. Native host/service tests and C/C++ layout checks exercise the
existing registry without the game. `tests/test_input_cpp.cpp` exercises the
consumer wrapper against a synthetic service table.

## C++20 wrapper

The header-only consumer wrapper is `scosdk/game/input.hpp`:

```cpp
sco::sdk::game::InputRegistry input;
if (input.Open(*this) == SCO_OK) {
    uint32_t count = 0;
    sco::sdk::game::InputAction action;
    if (input.Count(count) == SCO_OK && input.ById(SC_ACTION_RESPAWN, action) == SCO_OK) {
        // action.id, action.flags, and action.Name() are caller-owned copies.
    }
}
```

`Open` checks `query_service`, the `game.input.registry` capability, the service
version, the full v1 table size, and all required methods. Each call rechecks the
capability and re-queries the service. The wrapper borrows the API and live plugin
handles; each `InputAction` owns its fixed-size copied name and numeric id. Outputs
remain unchanged on failure. Calls retain the C service's game-thread requirement.

This adds C++ consumer support only. The existing C contract and repository
provider implementation are unchanged; synthetic wrapper checks do not establish
production provider availability in a live game. No input is dispatched. C# and
Lua typed wrappers remain unsupported.
