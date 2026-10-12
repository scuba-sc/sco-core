# Guarded offline missions and local economy (#132 / #133)

## Boundary and evidence

`game.missions` 1.0 is a game-pack service; frozen `sco_api` 1.1 is unchanged.
It returns bounded copied offer/status records, never game pointers, broker internals,
account identifiers or filesystem paths. Offer index is bounded to 256, steps to 64.
Uniform phase widths are the deliberately narrow supported slice; other mission kinds
remain unavailable. Read records are not authority to mutate or pay rewards.

Every call requires the game thread, a known unreleased plugin, nonzero current session,
explicit offline admission, anti-cheat absent, and shutdown admission open. Missing
engine callbacks return `SCO_UNAVAILABLE`. A native/C++ backend fault latches
`SCO_CRASHED` for this process; stopping/restarting cannot clear it. Backend contexts
must remain alive until consumers unload and Stop returns. Stop/replacement during a
callback is refused; recursive table calls return `SCO_FAILED`. Output records are
copied only on success, preserving caller size and unknown tails.

The private `src/game/services/offline_missions.h` seam is **not** a public host or
plugin composition API. It is used by synthetic tests. No validated engine adapter
is installed: this work must not claim offer/accept/progression/payment in game.
Signature matches alone are not sufficient to install or enable a backend.

C and C++ routes are supported; the C++ wrapper checks complete callback extents,
null methods and failed reopen. C# and Lua bindings are unsupported for this service.
Native plugins remain trusted process code; table guards are not a native sandbox.

## Broker acceptance

`accept` validates a complete offer against the current bounded broker catalogue,
including generation and broker ID. No arbitrary mission identifier creation.
A live plugin/session owns each accepted handle and key. A 64-entry non-evicting
receipt ledger reserves before the effect, preserving identical success/failure
replays and rejecting altered key reuse. Faults or malformed successful engine
IDs require restart. Old session receipts become inaccessible on session change;
this is **session-local**, not persistence across plugin reload or process restart.

## Objective and phase progression

Mutations are restricted to handles accepted by the same live plugin in the current
session. Ordered objective completion and phase advance compare the expected state
with a fresh copied backend status; skips, foreign IDs, unsupported phase shapes and
state disagreement refuse without effects. Completed-step/phase replays are no-ops.
A failed attempted effect disables further progression for that handle rather than
retrying it. Last-phase advance marks completion only; there is no automatic payout.
A backend success means its effect completed synchronously; delayed operations require
a different future contract, not optimistically returning OK here.

## Decision pending owner

Approve mission-kind coverage and the concrete Windows adapter only after independent
offline/EAC/session, broker/phase and payment acceptance. Until then the default
provider is unavailable. Persistence, wallet-file precedence, labels/filtering and
user-file import remain product/plugin policy, using `sco.storage`; this service does
not import or save player files. Actual extracted engine scripts are not distributed.

## Verification

`ctest -R '^offline_missions$'` exercises the real registry and guarded provider with
synthetic callbacks. ABI pins compile as C11/C++20 (including short enums and the
Windows-x64 freestanding layout probe). Synthetic response fixtures do not prove
engine semantics, Windows fault recovery, rendering or gameplay.
