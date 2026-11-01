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
owner lock. A concurrent close or fault prevents publication and retains partially
created state. `Start` opens cooperative admission only with the Running phase.
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

The legacy `retireConsumer` and native barrier callbacks are not accepted as
ownership proof. A future successful retirement needs a complete reader/worker
census, owner-controlled cleanup, failure injection, and Windows native evidence;
a callback that merely returns success cannot provide those guarantees. Native
evidence is required for native consumers, not for a bounded non-native fixture.

## Detailed exposure reports (local host-source increment)

`HostCapsule` and the additive `PrepareDetailed`, `ActivateDetailed`,
`StartDetailed`, `StopDetailed` and `StatusDetailed` entries describe dependency
exposure on this game-host route. They do not call public `app::StartDetailed`
instead of `app::detail::StartOwned`. The old `Result` function signatures remain
available but cannot tell a product whether failed startup exposed dependencies.
No frozen plugin table or existing `Options` layout changes.

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

One nonblocking control gate covers old and detailed game-host controls. No
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
can still be scheduler-delayed; the deadline is rechecked afterwards. A failed owner
retained through process exit can retain a finished helper's joinable handle too;
there is no general late-reap or disposal API. This is separate from the fixed
successful-drain helper leak and supplies no cleanup or native guarantee.
