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
  startup capability is passed explicitly to owned publishers; reentrant public
  publication and capability mutation are refused. The private owned registry
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
* No exact-head Windows/MSVC /WX full suite, SEH, native target exclusion or quit
  continuation proof has been obtained. The older MSVC registry-budget warning
  conflicts with ancestor b420d1a's recorded fix and historical test claim; neither
  substitutes for exact-head verification.
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
surface. Its terminal `QuiescedRetained` result is not an app-stop result. The
All UI and storage table functions acquire admission for their complete calls,
including input and output copying. Refusal at admission returns unavailable
before invoking the entry; output values are left untouched except that
`normalize_chord` clears its size. This is not a census of other service tables,
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
