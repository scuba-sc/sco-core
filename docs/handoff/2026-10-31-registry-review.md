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

* Startup admission is not complete: public `ProvideService` inherits the
  transition-thread exemption, so a reentrant log or other callback can publish
  a service while reader admission is shut. DataCore service setup also occurs
  in Activate before the app binds its runtime owner. The safe fix needs an
  explicit internal startup capability threaded through subsystem publication,
  and runtime binding before any publication; do not enable product startup on
  the basis of the current synthetic Running path.
* No successful owned retirement exists. Supporting one requires a callback-free,
  non-failing commit or a different provable lifetime architecture; partial
  cleanup cannot be made atomic by sampling owner readiness.
* Every relevant reader/worker must enroll and keep its lease throughout use.
  The protocol cannot interpose on hidden native threads, cached raw tables,
  arbitrary plugins or engine execution. No synthetic test proves native safety.
* No exact-head Windows/MSVC /WX full suite, SEH, native target exclusion or quit
  continuation proof has been obtained. The older MSVC registry-budget warning
  conflicts with ancestor b420d1a's recorded fix and historical test claim; neither
  substitutes for exact-head verification.
* Core #304's earlier named candidate was `571fe8e2c911d7c63c372103724896feaabe58f2`;
  its observed remote head is now `1b6782bc5c8b7d189cdff4c83e81957f09af35d7`.
  Offline #184's observed remote head is `80dba91491228d7c5ed0b451c5a55eb7954b484f`.
  These are independent candidates, not dependencies merged into this branch.
  Coordinate final SHAs and deliberate API choices; do not silently fold this
  branch into either one. Offline native activation must remain refused until
  its real product-owned barrier is proven.
* Timed waits do not guarantee a hard wall-clock bound for mutex scheduling or
  thread creation. A timed-out owner, workers and borrowed backing remain live;
  destruction is only legal after all callers, leases and starts have ended.
