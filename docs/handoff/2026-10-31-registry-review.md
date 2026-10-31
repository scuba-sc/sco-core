# Registry ownership follow-up — HOLD

This branch is separate from core #304 and offline #184. It is a synthetic
host-source candidate, not a merge, release, game result or native barrier proof.
Its held reference head was `cf3b62679dbd4e065984e32d58662ab9be0c60b1`.

The owner reserves worker starts before running user-defined callable operations,
and counts them through callable destruction and thread-handle publication.
Worker execution, reads and startups refuse same-thread drainage rather than
waiting on themselves. Reads are thread-bound; invalid transfer faults the owner.
An internal host capability closes reader and worker admission from Prepare through
the Running publication. Each phase publication is serialized against fault and
closure. Direct public app startup with an owner is refused.

Owned shutdown closes admission, attempts the real reader/worker drain, and then
returns Failed while retaining the app, registry and dependencies. It does not
run exit, task-drop, unload or host-service callbacks: those paths are not known
to form a failure-atomic transaction. Owner fault closes runtime admission;
already-admitted work can finish, while later queued work remains retained.
A fault during DataCore loading refuses
the result and retains any mutated state. The synthetic transform is refused
before Running and serializes reentry without holding its input lock across I/O.
Early native DataCore loading, legacy callbacks, game services and hooks remain
unsupported. Legacy fixtures that cannot run under this contract return CTest's
skip code instead of claiming their original assertions passed.

The old `void plugins::UnloadAll` type and behavior are retained alongside the
checked `TryUnloadAll`. The top-level plain-C `sco_api.h` and `sc_game_pack.h`
are unchanged versus `5b010dc`. The broader branch changes C++ host-source APIs,
including `app::Stop`, `Platform::registerSignatures` and resolver signatures;
consumers need a deliberate coordinated transition. Source archive manifests
include the new internal headers and tests.

## Remaining blockers

* Runtime ownership binds before any owned service publication. A private
  startup capability is passed explicitly to owned publishers. Public C++
  `ProvideService` refuses throughout an owned session, including Running;
  public capability mutation refuses during startup. The private owned registry
  never enters the legacy global slot, which is reserved empty. Previously used
  legacy runtime state or an occupied registry refuses startup. Public app
  plugin/content reference getters return empty views in owned sessions; the
  trusted host copies data under its explicit startup transition. This is not yet
  a complete census of all externally callable entry points or raw-table users.
* The isolated synthetic session supports successful `QuiescedRetained` retirement
  after cooperative readers and workers drain and workers join. It keeps its
  backing rooted for process lifetime and refuses restart. A timed-out or faulted
  session retains backing and reports failure; its test intentionally retains an
  unjoined worker, which unsuppressed ThreadSanitizer reports as a thread leak.
  This result does not establish successful application or native retirement.
* Every relevant reader/worker must enroll and keep its lease throughout use.
  The protocol cannot interpose on hidden native threads, cached raw tables,
  arbitrary plugins or engine execution. No synthetic test proves native safety.
* Hosted Windows/MSVC /WX full CTest and extracted SDK builds passed at
  `49eb153d1d0952ebbc7baa258988e6dc6cc67320`; a later head requires its own
  checks. SEH behavior in the target, native exclusion and quit continuation
  remain unproven. The earlier registry-budget warning was resolved for that CI run.
* Core #304's earlier named candidate was `571fe8e2c911d7c63c372103724896feaabe58f2`;
  its remote head is `1b6782bc5c8b7d189cdff4c83e81957f09af35d7` and is merged.
  Offline #184's remote head is `aaf79475b159f0fd76000ba315bef11778cb53b7`
  and is merged. The integration candidate includes core main
  `7ee83fa95680aa482d8b160987b472547a67b888` with #304's bounds and ASOP
  changes; it does not incorporate offline #184. Coordinate deliberate C++ API
  migration and final SHAs; do not silently fold this branch into offline. Offline native activation must remain refused until
  its real product-owned barrier is proven.
* Timed waits do not guarantee a hard wall-clock bound for mutex scheduling or
  thread creation. A timed-out owner, workers and borrowed backing remain live;
  destruction is only legal after all callers, leases and starts have ended.

## Follow-up: bounded table and retirement design

The synthetic session's one-slot table is a private, typed fixture: it copies a
scalar under an owner read, retains its capsule for process lifetime, and has no
queue, plugin, native entry, mapped view or external callback in its production
surface. Its terminal `QuiescedRetained` result is not an app-stop result.
All UI, storage and network table functions acquire admission for their complete calls,
including input and output copying. Refusal at admission returns unavailable
before invoking the entry; output values are left untouched except that
`normalize_chord` clears its size; network scalar entries return zero and its
result entries return unavailable without writing outputs. An already admitted
network call may return its computed result after a concurrent fault, while the
fault prevents successful retirement. Network C++ scalar/copy observers now
use a read-only runtime census that permits completed pre-bind observations and
counts active owned reads through their last copy. After closure their false,
zero, Idle or empty defaults indicate refusal or a real default, not cleanup.
An admitted `WaitForDelivery` holds its read until delivery or its own timeout;
it can force a retained drain failure. This is not a census of other service tables,
callbacks or pointers returned by tables. The current C query API can return arbitrary opaque plugin tables;
it cannot interpose on calls after lookup, and IPC mapped views outlive calls.
Those paths cannot establish successful application retirement.

A future supported application subset needs a session-owned dependency capsule
that strongly owns every registry, image, table, callback module, task and context
it may retain. Close all admitted work, drain enrolled readers and worker starts,
and join workers including TLS teardown. Under the owner lock, serialize fault
against a nonallocating retained-root commit, with no cleanup callbacks or custom
finalizers in that commit. Return `QuiescedRetained` only for this transaction;
physical cleanup requires separately proven no-fail finalizers. Any timeout or
fault retains the full closure and refuses restart. Borrowed pointers are not
ownership tokens. Native patches, old plugin tables, external threads, mapped
views and the engine quit continuation require their own enrollment and actual
product exclusion proof before activation can be enabled.

The public `app::Start` refuses `Platform::gameServices` before startup work,
including when its legacy barrier callback would return true. The separate public
C++ `game::services::Start` entry also refuses before native reads or provider
mutation. Fresh admission now enters the runtime binding census before taking
its inspector-owner snapshot, then holds reader leases through the callback;
an earlier enabled legacy call conservatively prevents later owned binding.
The separate game-thread cvar cleanup inspection is not covered by this change
and must remain unavailable in an owned transaction until independently proven.
Unowned callbacks and returned views still block successful real-application
retirement. The test-only lifecycle fixture is not native proof.
In a fresh process, after direct `game::services::Start` refusal, `Stop` returns
`WrongThread` off the game thread and is an idempotent game-service cleanup no-op
on it. Its thread check still counts as legacy runtime use, so a subsequent
strict owned-runtime binding is refused. This observation is a source contract;
the corresponding Windows-only regression requires hosted execution.
The earlier Windows app fixture's real game-service table and capability coverage
is explicitly skipped while production startup refuses these services. The mock
boundary fixture preserves its failure-order tests but does not replace that coverage.

Consumers must migrate to `app::StartDetailed`. `Started` transfers the usual
lifetime responsibility through successful `Stop`; `RefusedBeforeExposure`
means this invocation exposed no platform dependencies; `RetainDependencies`
means startup failed after possible exposure and the consumer must preserve the
dependency closure until process exit. A refused invocation says nothing about
the same pointers retained by an earlier invocation. The legacy `bool Start` combines the two
failure cases and cannot guide cleanup. After `Started`, preserve backing after
every non-`Ok` `Stop`; only a later `Ok` permits the normal cleanup. A startup
`RetainDependencies` remains process-retained even if `Stop` later says `Ok`.
These are
C++ host-source changes, not a change to the frozen plain-C plugin table.

## Bounded non-native mock host integration

An engine-owned mock host can use the existing **legacy** app path in a fresh,
isolated process with no prior app, registry, runtime or foreign provider, without
asserting that the app has a successful owned retirement implementation. Its
product capsule must actually own every referenced table, callback context,
backing object and executable module; code may instead be linked into an
executable guaranteed to stay mapped. A dummy `shared_ptr` or a borrowed
pointer is not ownership. Select `gameServices=false`, no image or signature
resolver, no external plugins, scripts, built-ins, private session, native hook,
runtime inspection or host menu. Avoid IPC mapping views and native network
sessions. Any mock provider must be an explicitly enumerated, capsule-owned
fixture, never an arbitrary foreign provider or unloadable module.

This is a product-side closed-world contract, not an enforced core reservation.
Today `app::StartDetailed` does not reserve all core startup/mutation entry
points before consumer globals change: a reentrant legacy startup callback can
successfully publish and query a foreign service. A shared process gate with
explicit private startup capability and lifetime-long reservation is required
before claiming core-enforced exclusive startup. Failed or uncertain retirement
must retain that reservation. An OS singleton alone does not prevent another
call path within the process from mutating core globals. The core targets are
static libraries, so multiple modules can also contain separate gate instances;
the supported composition must use one physical gate or explicitly refuse
duplicate core instances.

The engine must keep a thread-safe admission gate around **every entire ordinary
call** into the app, each mock table, its callbacks and any output copy;
returning a raw table or a view outside that gate is not sufficient. The
serialized `StartDetailed`/`Stop` control frame is excluded from the reader
census and itself keeps its dependencies alive. No untracked caller, worker or
cached view may survive the gate. Close new admission, drain in-flight calls and
join all workers including thread-local destruction before cleanup. The
`beforeDependentStop` adapter must maintain this exclusion throughout `Stop`,
including synchronous cleanup callbacks. Controlled callbacks invoked directly
by `StartDetailed` or `Stop` run as part of that serialized control frame; they
must not re-enter `StartDetailed`/`Stop`, wait for the closed ordinary-reader
gate, or enqueue or spawn work surviving teardown. The adapter can also be
invoked on a partial startup failure and must not wait for its own control frame. A boolean callback
without that durable engine-side behavior is not evidence of exclusion. A
host-service start callback, if used, must be failure-atomic or leave nothing
exposed when it fails; otherwise omit it.

Use `StartDetailed` and retain the capsule after `Started` through a proven
successful `Stop`. `RefusedBeforeExposure` concerns only that invocation and
does not free aliases retained by a prior invocation. On `RetainDependencies`,
or an uncertain startup exception, retain the full capsule until process exit;
do not infer cleanup from a later `Stop`. After `Started`, a non-`Ok` stop
retains the closure and forbids restart unless a later `Stop` returns `Ok`.
Even then, release only after the engine gate and all returned views and worker
lifetimes are accounted for. This is a contract for a separately tested mock
integration, not proof that arbitrary providers or native targets are safe.

For that integration, an engine capsule may choose the stricter terminal states
`Released` (this invocation exposed nothing and no other invocation retains its
dependencies, or it started and stopped cleanly). Either case also requires
closed admission and all relevant readers, views, callbacks and workers drained
or proven never to have existed. `FailedRetained` covers any uncertain start or
stop, with the whole closure rooted until process exit and no restart. A
`QuiescedRetained` state requires closed admission, proof of that full drain,
fault-versus-terminal-commit serialization and retention of the whole closure
until process exit; it cannot authorize cleanup or restart. It is not returned
by core's app API and cannot be inferred from a timeout or the private
synthetic-session fixture.
