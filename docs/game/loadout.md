# Typed offline local-player loadout (`game.loadout` 1.0)

**Synthetic/native-boundary checked; not run in game.** Production publishes the
table when Windows game services start, but `game.loadout.apply` stays **false**.
The game pack has no verified typed per-slot equip implementation yet. Signature
resolution or a successful query is not permission to mutate a player.

## Inputs and outputs

`sc_loadout.h` adds a separate size-prefixed service; frozen `sco_api` 1.1 is
unchanged. Requests and reports also start with size, requiring the full v1 extent.
The item/result records are frozen for 1.x. Supply exact `item_stride`, 1..16 items,
unique numeric slots and NUL-terminated ASCII `[A-Za-z0-9_]` class names (1..95 bytes).
Reserved fields are zero. No XML, file/path ingestion, raw game pointers, account
inventory, entitlements, telemetry or network operations are exposed.

HEAD, TORSO, ARMS, LEGS, UNDERSUIT, BACKPACK and VISOR identify *logical* slots,
not verified engine port names. Unknown numeric slots are valid input, reported
UNSUPPORTED without an equip call. Known slots can also be UNSUPPORTED by the adapter.
List/outfit parsing, spoiler confirmation and labels remain consumer policy.

Structural errors reject the whole batch before any equip: BAD_ARG (including null,
foreign/released self, short size, bad stride/name, duplicates), or TOO_MANY above 16.
Report stays unchanged on structural errors and WRONG_THREAD. Valid requests receive
input-ordered copied results and applied count. OK means every slot APPLIED; FAILED
means a partial or wholly refused processed batch. ITEM_MISSING skips that slot and
continues. No rollback or duplication-prevention claim is made: only a verified
adapter may report APPLIED after confirming its operation. Consumers must not retry
already applied slots blindly. Successful changes are player state, not plugin-owned
entities, and remain on ordinary consumer unload.

Absent service queries return NOT_FOUND. Missing/unverified backend, missing method,
revoked capability or unknown/online/EAC-present admission returns UNAVAILABLE with
all slots UNAVAILABLE and zero engine equip calls. Admission is checked before each
supported slot; loss mid-batch preserves earlier applied slots and leaves later slots
UNAVAILABLE (aggregate FAILED). Report count/status is synchronous completion of
each adapter call, not a streaming ticket or global inventory snapshot.

## Thread, lifetime and faults

Game thread only (command/tick/queued game-thread task); caller data lives until
return. Bounded stack copies only, no retained input, work queue or cancellation.
The host owns publication and withdraws after consumers unload. Borrowed tables must
not be called after terminal shutdown. Recursive apply returns UNAVAILABLE without
a nested engine call; backend Start is refused and Stop ignored during apply.

Internal `LoadoutBackend` is game-pack/host code, **not a public plugin installation
API**. It supplies verified typed equip and live fail-closed offline admission;
current production calls `StartLoadout()` without it. Callbacks live through Stop.
Portable guards catch C++ exceptions. Windows MSVC guard frames additionally use
SEH, not locally exercised. A callback fault or invalid callback result disables
apply for the rest of the process; restart is required even after Stop/Start.
This does not guarantee native memory corruption recovery or callback attribution
outside the host loader guard.

## Language and package routes

C calls the full-extent, non-null `apply` member directly. C++20 uses
`scosdk/game/loadout.hpp` (`Loadout::Open/Apply`), which rejects complete-pointer-tail
truncation/null callbacks and clears failed reopen state. C# is unsupported for loadout 1.0;
Lua mutation is unsupported (no FFI/binding or read-only inventory route).
Header, wrapper, docs and ABI fixture are in `game-sc` and combined legacy profiles,
never the neutral core/core-source archive. Repository-only provider tests compile
an independent C native module from copied public headers, load it through the real
host loader and invoke the actual game-pack provider with synthetic engine callbacks.
These are not Windows DLL, renderer, engine or gameplay acceptance.

## Pending engine acceptance (G6 / charter 4A)

- Identify exact supported per-slot engine calls, item-existence checking and local
  player selection; do not infer individual status from the old all-at-once XML loader.
- Verify build-specific slot/outfit/head/visor mapping, no duplication/corruption,
  no-player and missing-item behavior on an owner-approved offline Windows build.
- Supply current session offline/EAC admission from game-pack composition (unknown
  refuses), and verify calling conventions/SEH with MSVC.
- Only then install/attest the typed backend and enable its capability. Consumer
  extraction/engine activation and charter gameplay acceptance remain blocked.

**Decision pending owner:** extend logical slots only from observed consumer needs
and verified port mappings. No arbitrary console, XML loader or account inventory
fallback is authorized by this contract.
