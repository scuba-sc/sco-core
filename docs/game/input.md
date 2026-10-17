# game.input 1.0

Read-only action vocabulary, published by the game pack through `query_service`.
Include `sc_input.h`; use `SC_INPUT_SERVICE_NAME` and `SC_INPUT_SERVICE_VERSION`.
The frozen kernel ABI is unchanged. No input is dispatched and no key binding is read.

`count` reports 1,267 named members, not 1,267 distinct numeric actions. `FIRST` is
0; `LAST` and `INVALID` both have id 1265. `by_id` uses the canonical name `LAST`
for that id, while `by_name` accepts either alias. Id lookup is O(1); exact
case-sensitive name lookup is binary search. Unknown names or ids return NOT_FOUND.

All methods require the game thread and a live plugin handle. Records begin with
`size`; set it before lookup. Null pointers, undersized records, empty names or
names without a terminator in 96 bytes are BAD_ARG. Failure leaves outputs unchanged.
The copied name is always NUL-terminated. Stop withdraws the service; retained tables
return UNAVAILABLE. A newer same-major caller must still check table size.

## Evidence and limits

F-4.10.197.2878-033 and F-INTAKE-010 supply the vocabulary. Core dispatch strings
are anchored on 4.10.197.2878, but members above 186 come from a build-unresolved
type census. Their records set `SC_INPUT_ACTION_BUILD_UNRESOLVED`; metadata is
not proof that a numeric id is safe to dispatch on a particular build. Sentinel
records set `SC_INPUT_ACTION_SENTINEL`. No game addresses or engine pointers cross
the ABI. The capability `game.input.registry` advertises metadata availability only.

Not run in game. Native host/service lookup and C/C++ layout tests exercise the
registry without the game. C++/C#/Lua typed wrappers are unsupported for this
addition; plugins can use the public C service table.
