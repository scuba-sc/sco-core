# Offline host composition — held synthetic candidate

This branch is a **do-not-merge host-source experiment**. It does not establish
native instruction-patch safety, game reader exclusion, an in-game result, or a
release-ready SDK. The plain-C plugin ABI is separate from these C++ host-source
interfaces. Product adoption needs an explicit, coordinated SDK/API change.

The currently supported fixture has an owner-held `OwnedShutdown`, an external
quit and DataCore transport, no native game services or hooks, no plugins,
built-ins, scripts, host menus, host-service adapters, or early requirement and
capability callbacks. Legacy configurations are refused before activation.
The owner and every borrowed image, resolver, context and backing resource must
outlive all calls and remain retained after failed shutdown. An owner lease is
cooperative: it cannot interpose on hidden native execution, cached tables or
unmanaged threads. Read leases are thread-bound and cover the complete use.

The owned fixture binds the runtime before publishing DataCore and requires an
explicit private startup capability for subsystem publication. Public runtime
publication does not inherit startup authority from a reentrant callback. It
reserves the global signature-registry slot empty and keeps its registry private.
A previously used legacy runtime or occupied registry refuses owned startup.
The fixture remains quarantined: full public entry coverage and native resource
lifetimes require independent proof before product activation.

`Prepare`, `Activate`, and `Start` are separate host entries. Owner admission is
closed across all three phases; an already-entered reader or worker start prevents
startup. Each entry holds a host transition and publishes its phase under the
owner lock. A transition must be destroyed on its acquiring thread; a foreign
release faults and closes the owner, retaining backing and refusing maintenance
from the original thread. Other control threads may still reap retained handles;
that does not authorize retirement. A close or fault observed under that lock
before publication prevents
that publication and retains partially created state. A request that linearizes
after successful publication does not retroactively cancel it. `Start` opens cooperative admission only with the Running phase.
This is a synthetic lifecycle protocol, not evidence of a native barrier. Product
native preparation and mutation must remain refused until the product supplies a
real exclusion protocol and Windows synthetic-target proof.

The optional synthetic DataCore service can start during activation. Its transform
refuses until Running, obtains a read lease, and rejects reentry or a concurrent
transform. An owner fault during a transform refuses the result and keeps mutated
service state retained. Early engine loading and the earlier plugin/requirement
callback scenarios are unsupported; those legacy tests are explicitly skipped.

`Tick` admits a full-operation read lease and refuses reentry. The fresh admission
boolean is an observation, not a substitute for that lease. Owner fault or admission
loss closes runtime admission. Already-admitted work can finish; later queued
callbacks remain retained.

`Stop` closes admission and attempts to drain all cooperative reads and owned
workers, including their thread-local destruction. It **returns Failed and retains
the app, registry, provider code, image and dependent backing**, even after a
successful drain. Existing cleanup can call arbitrary code or irreversibly destroy
state, so there is no proved failure-atomic cleanup commit. Restart is refused;
the host must keep retained resources valid. Timed waits do not guarantee a hard
wall-clock deadline for mutex scheduling or thread creation. No fail-stop process
termination is supplied.

## Direct engine quit and dependent providers

The direct detour requests non-draining closure of the process-retained legacy
owner, then calls the original engine quit synchronously with the original
arguments. The owner is published before legacy startup transitions. An actual
detour call before owner publication marks the attempt terminal; a later
publication refuses startup and attempts to close the supplied owner. The
detour cannot itself close an owner it has not yet been given. A failed owner
request leaves closure unproven; if notification returns, a non-null original
runs once. The mutex may block, and structured exceptions remain unproved. The separate public
`BeforeQuit` retains its synchronous `Stop` meaning. Neither operation proves
engine-owned objects survive outstanding product reads. The detour does not call
`app::Stop`, dispatch `game.exit`, unload plugins, or run `game::services::Stop`
/ `WithdrawGameServices`. Current activation policy rejects the required-hook
configuration before installation; synthetic detour tests do not alter this gate.
The terminal marker affects only legacy required-hook preparation with an owner;
it is not a process-wide barrier for external or detailed compositions. Published
legacy owners remain retained after failed preparation, and a later owner cannot
replace them. Once quit closes an owner during an already-entered inspector,
preparation may still perform registry or image work before its transition fails.
No check here proves that work is safe after the engine original has run.

The ordering matters to dependent services. `app::Stop` dispatches `game.exit`
while services remain published, then stops plugins and game services. A direct
`BeforeQuit` does not run that sequence, so an ASOP `game.exit` subscriber cannot
withdraw its provider here. The current owned `HostCapsule` also refuses game
services, plugins and product host-service adapters; it cannot own or withdraw
the offline product's ASOP composition.

There is no safe ASOP completion API to add on this path with the present
contract. Calling `app::Stop` inside the quit detour would run synchronous event,
plugin and provider callbacks on the engine hook. The detour's
`OwnedShutdown::request_stop()` closes only this owner's admissions; it neither
notifies the product controller nor drains ASOP callbacks, and the original quit
could terminate before another safe game-thread turn. The request itself takes
the owner mutex and is not a hard-time-bounded or lock-free hook primitive.
Neither option proves ASOP withdrawal or safe original-quit continuation.

Product integration needs a shell-owned pre-quit notification or an
engine-specific deferral mechanism, plus a completion receipt the detour can
observe. On a safe game-thread path, the product must keep `game.exit` available
until its callbacks return, close dependent-provider admission, establish that
provider's own reader/callback and native-hook quiescence, and withdraw the
provider only after that proof. The detour may forward the original quit exactly
once only after observing that completed sequence; it must not repeat teardown
or wait for completion on the hook. The current offline product has neither the
pre-quit notification nor this completion receipt. If it cannot arrange the
sequence before the engine may terminate, the dependent provider and its backing
must remain retained and ASOP withdrawal on direct quit remains unresolved. A
native ASOP hook adapter also needs its own hook-entry census and return barrier;
`OwnedShutdown` and service-table withdrawal do not supply them.

Deferred product acceptance must first exercise the shell/engine handshake:
`game.exit` callbacks can still query the published ASOP provider; no provider
withdrawal occurs until the callback and provider-owned readers have returned;
the detour observes one completion receipt and forwards the original quit once;
and missing/uncertain completion retains the provider and backing. Native ASOP
activation additionally requires Windows evidence that hook admission closes and
all entered hooks return before any hook target, table or backing is withdrawn.

## Request closure before control-thread drainage

The additive C++ host-source contract is:

```cpp
// include/sco/owned_shutdown.h
void sco::OwnedShutdown::request_stop();

// include/sco/game/host.h (link sco_game_host)
sco::game::host::HostReport sco::game::host::RequestStopDetailed() noexcept;
```

`request_stop()` closes new read, worker-start and transition admission under the
existing owner mutex and sets the existing cooperative worker stop predicate.
It returns without waiting for admitted readers, reserved worker starts, worker
bodies, callable destruction or TLS. A reader, worker, callable constructor or
startup callback can request stop and then unwind normally. The request does
not fault the owner, consume its one drain attempt, set readiness, invoke user
code, or release dependencies. Repeating it preserves an existing fault or drain
result. An already-reserved worker start may still enter its body with the stop
predicate set; that start remains counted until handle publication or cancellation.

This is a non-draining operation, not a lock-free one: mutex scheduling has no
hard deadline, and mutex acquisition can throw. It is not an interface for
`DllMain`, loader lock or signal handlers. If it throws, closure is unproven and
the caller must retain the complete owner/backing closure. Every ordinary use
must already hold the same owner's Read through its final output copy.

`RequestStopDetailed()` obtains the owner from the immutable, process-retained
capsule root. It does not acquire the host control gate or read mutable Options,
so an admitted callback or the Prepare inspector can request closure without
waiting for its own frame or inheriting publication authority. Its returned
`drain` field snapshots cooperative owner progress after the request; drainage
can advance concurrently, so later reports may show a later state. It runs no
inspector, OS policy action, app Stop, cleanup callback or phase transition.
The capsule and registry remain rooted on every outcome.

| Request result | Meaning |
| --- | --- |
| `Ok`, adopted session ID, `ProcessRetained` | The rooted owner's admission is closed and enrolled workers see the stop request. No drain or cleanup is claimed. |
| `Unavailable` | No root was observed, so the request was not delivered or queued for a later capsule. A concurrently observed retained report does not turn refusal into closure proof. |
| `Failed`, adopted session ID, `ProcessRetained` | The close call threw. Closure is unproven; retain everything. |

The report's `drain` field is a separate `DrainDisposition`: `NotAdopted`,
`Open`, `Requested`, `InProgress`, `Drained`, `Retained` or `Unknown`. The
reported phase is only the observed host phase; it may still be `Running` after
the request closes admission. Do not use phase or `drain` to admit work or
display cleanup as complete. `Drained` records successful completion of the
owner's one drain attempt; it remains only a historical cooperative reader and
worker observation even if the host later faults the owner and latches failure.
If closure is observed under the owner lock before the Prepare transition
publishes success, that publication is refused and partial state stays retained.
A request that overlaps Prepare in wall-clock time but linearizes after successful
publication does not retroactively cancel the completed publication.

The offline consumer sequence is deliberately split across owners of work:

1. Before exposure, construct the actual dependency closure and retain the same
   `std::shared_ptr<OwnedShutdown>` in the `HostCapsule` and all admitted product
   readers/workers. Use `PrepareDetailed` to root an accepted capsule. A dummy
   backing token cannot enroll foreign threads or pin native code.
2. An admitted frame calls `RequestStopDetailed()` (or its own owner's
   `request_stop()` for a private, not-yet-adopted composition), then releases
   its read only after its final output copy and callback return obligations.
3. A separate control thread, outside all that owner's reads, workers, worker
   constructors/destructors and startup transitions, calls
   `owner->drain_until(deadline)`. Serialize this once-only attempt in the
   product; never run it inside the worker being joined or while retaining a
   read on the controller. Retain the capsule, owner and transitive backing
   through both closure and drainage.
4. The host reports `Requested` before the owner drain, `InProgress` while it
   runs, and `Drained` only when the owner's actual attempt completed. A later
   `StopDetailed` reuses that completed proof and does not call the one-shot
   drain again. Its `result` still reports retained failure and
   `dependencies` remains `ProcessRetained`; `drain: Drained` is never app
   cleanup authorization. If a drain is `InProgress`, failed or uncertain when
   `StopDetailed` arrives, the host retains failure and refuses a second attempt.
   Complete the product drain before calling game-thread Stop.
5. A true drain result proves cooperative census completion and actual worker,
   callable, TLS and helper joins. It does not prove application cleanup,
   foreign callback return, native instruction exclusion, safe original-quit
   continuation or module unload. A false/exceptional drain is retained failure;
   use `reap_retained_until` only for the documented later handle maintenance.
   The current adopted game-host remains `ProcessRetained` on either outcome.
   `StopDetailed` remains a game-thread operation that returns retained failure;
   it is not a control-thread cleanup method.

This foundation adds no provider, plugin ABI, capability or positive native
admission. `HostCapsule`'s restricted configuration, `StartOwned` refusal of
plugins/native services, and product native preflight remain unchanged. In
particular, an owned close/drain does not make the separately preserved startup
snapshot writer eligible to run on this restricted path.

### Deferred acceptance for this increment

At the 2026-11-04 local checkpoint, no new cases have been built or executed. Authored cases in
`tests/test_owned_shutdown.cpp` cover reader and worker self-request, actual TLS
joins, blocked callable construction, startup publication cancellation, repeated
requests and timeout followed by retained maintenance. The existing
`game_host_report_*` fixture has three new cases: `request-stop-no-root`,
`request-stop-prepare` and `request-stop-running`. They use the actual capsule
root, inspector/control gate, admitted frame and independent controller. The
last case drops caller capsule/backing roots before the request, asserts drainage
cannot complete while the frame is leased, observes `Requested`, `InProgress`
and `Drained`, and still expects retained app stop.

Deferred Linux/Windows execution must confirm all existing owned shutdown and
host report cases as well as these additions. Additional product acceptance
must prove ownership of OS thread handles and callback code, exact full-frame
and output-copy census, and original-quit lifetimes. Nothing here supplies native
or game acceptance or authorizes enabling a previously refused consumer.

The legacy `retireConsumer` and native barrier callbacks are not accepted as
ownership proof. A future successful retirement needs a complete reader/worker
census, owner-controlled cleanup, failure injection, and Windows native evidence;
a callback that merely returns success cannot provide those guarantees. Native
evidence is required for native consumers, not for a bounded non-native fixture.

## Detailed exposure reports (local host-source increment)

`HostCapsule` and the additive `PrepareDetailed`, `ActivateDetailed`,
`StartDetailed`, `StopDetailed`, `RequestStopDetailed` and `StatusDetailed` entries
describe dependency exposure and cooperative drain progress on this game-host route.
They do not call public `app::StartDetailed`
instead of `app::detail::StartOwned`. The old `Result` function signatures remain
available but cannot tell a product whether failed startup exposed dependencies.
`HostReport` is a C++ value returned by value and its layout has changed; rebuild
all host consumers and Core together. The `app::Platform` C++ layout also differs
from the published 2.4.0 source. The frozen plugin C ABI and game.build v1 do not
change in this increment.

Construct a capsule privately using `HostCapsule(options, backingOwner)` before
calling detailed Prepare. The capsule owns an Options copy, Image descriptor and
host-version string. `backingOwner` must actually own every referenced image
byte, mapping and accepted context. Resolver and inspector code must be strongly
pinned or linked into the audited process-resident executable. A dummy shared
pointer, no-op deleter or borrowed pointer is not ownership. A non-null token
cannot mechanically prove this contract. Capsule construction can throw without
core exposure; the original input pointers must remain valid through construction.

The detailed path is deliberately restricted: external quit/data hooks, no
native game services or native DataCore, no DataCore/storage root, plugins,
built-ins, scripts, private sessions or host adapters. No app/global raw service
tables, mapped views or foreign workers are enrolled by this API. A separately
audited capsule-owned copy facade must hold the owner's Read through its last
output copy and unwind. Rooting dependencies is not a native execution barrier.

Detailed Prepare privately allocates retention storage, then roots the actual
capsule before existing Prepare reserves the registry, binds the runtime, copies
Options or invokes the inspector. The private registry is also pinned by that
root. The root is not destroyed during module-static teardown. Adoption issues
one nonzero process-scoped session ID; there is no second session or root
replacement in the same process. A failed adopted Prepare closes/faults its owner
and preserves the capsule, including a failure partway through Options copying.

Each `HostReport` contains the operation result, observed phase, session ID and
`NeverExposed` or `ProcessRetained` dependency disposition. A fresh invalid or
unsupported request, or allocation failure before adoption, can be NeverExposed.
Every post-adoption result remains ProcessRetained, including refusal, exception,
wrong-thread calls and stop failure. No result authorizes destruction, unload or
restart. The existing activation refusal may retry the same prepared transition;
it cannot replace the capsule or start a new session.

Legacy control use before adoption conservatively reports ProcessRetained with
ID zero and prevents detailed Prepare: its caller-owned backing is not
retroactively adopted. A refused overlapping control before adoption also latches that
conservative floor. Busy status returns Unavailable with conservative retained
data; it is not a completed control/adoption decision. Pure status observation
does not latch exposure or poison a private prepare that subsequently fails. ID zero does not assert that a new input was rooted. A later
legacy idle Stop returning Ok cannot clear that floor. NeverExposed does not
release aliases retained by another invocation. Status is an observation, not an
admission permit or cleanup certificate. Busy means the initial gate observation
found it held; completing Prepare cannot turn that busy observation into success.
Overlapping status calls may nevertheless return older observations after newer
ones. There is no monotonicity guarantee in completion order. Only the completed
PrepareDetailed report classifies adoption of that invocation's input.

One nonblocking control gate covers old and detailed game-host transitions. The
separate close request uses only the already-rooted owner mutex. No
mutex spans the inspector or other callouts; synchronous reentry refuses. Private
internal transitions preserve Start-to-Activate and Tick-to-Stop behavior without
granting an external callback ambient startup authority.

`game_host_report_*` fresh-process tests exercise this production route with
synthetic image bytes and executable-pinned code. They cover capsule destruction
counters, descriptor/string copies, pre-adoption refusal, actual ordinary
allocation failure inside Options assignment, partial-start failure, reentry,
wrong-thread stop, a read held through final output copying, timeout/self-drain,
and a no-read negative control. A test-only static-reference release checks the
private registry's process root. These are not native product, original-quit,
physical cleanup or successful application retirement proofs. Fixed fault seams
exist only in the separately instrumented test executable.

A successful worker drain joins both worker threads (including callable/TLS
destruction) and its internal helper before publishing readiness. The process
root still retains backing. If the timed wait expires before the helper's
worker-join completion marker is observed, the helper and owner remain retained:
a later exit cannot upgrade terminal failure. That timeout path does not detach
or perform an unbounded join. After observing the marker, the final helper join
can still be scheduler-delayed; the deadline is rechecked afterwards.

`OwnedShutdown::reap_retained_until(deadline)` is explicit post-failure handle
maintenance. `ReapedRetained` proves actual worker/callable/TLS and helper joins;
`PendingRetained` permits a later maintenance retry, and `Refused` covers wrong
phase, self-obligation or overlapping join control. It reuses an existing helper
instead of launching another. All three results preserve the original failed
retirement, closed admission, backing root and restart refusal. None permits
destruction, unmapping, provider cleanup or native activation. A completed
maintenance call cannot upgrade `FailedRetained` to `QuiescedRetained` or make
`ready()` true. The caller must retain the owner and all backing through every
maintenance call; resources are not automatically collected after a timeout.

The synthetic timeout fixture captures its real process-rooted owner before
disposing the wrapper, then explicitly collects late worker handles and checks
TLS destruction, cached-call refusal and retained backing. A test-only observer
checks every worker/helper handle plus the in-progress control flag; readiness
alone would not distinguish a finished but unjoined helper. No warning is
suppressed and no fixture detaches a worker or fabricates a retirement success.
