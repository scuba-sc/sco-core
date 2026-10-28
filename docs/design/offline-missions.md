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

The internal mission-event poll uses the same guarded admission and session ledger.
It excludes failed accepts and retired owners, contains callback faults, and stops
calling the backend during shutdown. A terminal status remains observable for event
delivery. These checks do not establish a native mission provider.

## Abandonment and user-authored content

Abandonment is an owned once-only effect. Completed missions refuse it; repeated
successful abandonment is a no-op. Ordinary owner release revokes authority before
attempting admitted active cleanup once. Unknown/closed admission skips engine cleanup;
uncertain failures are never replayed. Release inside a backend call quarantines the
boundary for restart. Native-fault cleanup does not make further engine calls.
The Windows composition shares spawn's release hook; fixtures reserve their own hook
only when a mutation backend is installed. Hook quota failures refuse startup atomically.

User content is deliberately **declarative**, not arbitrary engine XML or executable
Lua/native scripts. Only this exact ASCII grammar is accepted (LF required):

```text
sco-mission 1
phases 2
objectives 2
```

Numbers are canonical decimal 1..64. No other directives, references, embedded NULs,
DTD/entities, executable commands, includes, file/network APIs or trailing bytes.
The name is `missions/<lowercase ASCII [a-z0-9_], 1..100>.scom`, relative to the
consumer's declared data folder. Absolute/traversal/alias paths and Windows device
names are refused. **No file is opened by this service:** the consumer/content broker
must enforce declared-root/link containment when obtaining bytes. The game-pack
adapter must use copied validated bytes, never open the supplied name.

At most 16 registrations per admitted session and 4096 input bytes each. Validation
is atomic before registration; identical data/name registration is once-only, changed
content requires restart, failed registrations are not retried. Library registrations
are session-lifetime rather than unloadable executable code. Existing engine XML
script ingestion remains unsupported until its format and containment are independently
validated; this narrow format must not be advertised as legacy mission-script parity.

## Local economy and payment boundary (#133)

`game.economy` 1.0 exposes only admitted local wallet reads, signed integer delta
application with expected-balance comparison, and owner/session receipt lookup.
Balances are [0, 1000000000000); deltas are nonzero strictly inside +/- that limit.
Range validation prevents overflow and negative balances before engine calls. The
backend must atomically compare and apply, and return the exact resulting balance.
Inconsistent success is quarantined for restart. No online ledger/account/entitlement
or shop networking is exposed. No proprietary currency IDs are part of this ABI.

A 128-entry **non-evicting** session ledger reserves a receipt before each attempted
payment. Duplicate identical keys return the same receipt/result without another
engine call or signal; changed reuse is BAD_ARG. Failed uncertain effects are retained
as failures, never retried. Quota saturation refuses before applying. Receipt authority
is a live plugin handle, not a caller-supplied owner or arbitrary mission reward.
`game.economy.transaction` emits a bounded copied successful receipt and plugin ID
at most once, synchronously through the host event bus. If host shutdown suppresses
notification, the successful receipt sets `SC_ECONOMY_SIGNAL_DROPPED`; receipt polling
is authoritative, not signal delivery. Payload lifetime is callback-only;
callbacks copy and post later reads/mutations. Notification reentrant table calls
and Stop are refused. Ordinary owner release during notification is permitted.

**Once-only means owner/session-local, not durable exactly-once across reload/restart.**
Plugin storage must atomically persist its own reward/import/save decisions; neither
mission completion nor a signal automatically pays, imports or saves. Existing
wallet.txt/storage timestamp precedence remains consumer policy. This is not the
legacy asynchronous shop hook adapter, which still needs independent Windows proof.

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
