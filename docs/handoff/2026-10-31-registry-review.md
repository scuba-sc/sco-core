# Registry ownership follow-up — HOLD

This is a partial repair of the held `cf3b62679dbd4e065984e32d58662ab9be0c60b1`
prototype, not a candidate for merge or native activation. The branch includes
an older registry integration and is separate from core #304 and offline #184.
Do not interpret its tests as gameplay, engine-hook or Windows proof.

## Changes in this follow-up

Worker admission reserves an in-flight start under the owner mutex, then moves
and destroys arbitrary work objects and creates worker threads outside it. The
reservation remains counted until the thread handle is published, or callable
construction/destruction has completed on cancellation. A post-reservation
construction or thread-launch exception faults and closes the owner. Refusals
before reservation, including allocation failure or the slot limit, return
false; the caller must handle them. A timed-out drain remains terminal and requires retaining
the owner and all borrowed backing. Joining, including thread-local destruction,
is still needed for readiness. This does not produce a hard wall-clock deadline:
mutex scheduling and starting the joiner remain outside the timed wait guarantee.

The legacy `void plugins::UnloadAll` signature and behavior are restored;
`TryUnloadAll` is the separate checked API used by the prototype app path.
The plain-C `sco_api.h` and `sc_game_pack.h` are unchanged versus `5b010dc`.
The wider registry branch nevertheless changes C++ host-source interfaces,
including `app::Stop`, `Platform::registerSignatures`, and resolver signatures;
consumers need explicit versioned coordination. A failed host-service stop now
invalidates the owner and returns before subsequent dependent cleanup; earlier
unloads cannot be undone. Manifest entries cover the new header and tests.

## Unresolved release blockers

* A point-in-time `open()` check during activation races owner closure. Startup
  needs a durable owner-scoped activation/retirement protocol. The product must
  prove exclusion from first native mutation through publication, and a separate
  safe transition through shutdown. A callback claiming success is not proof.
* `ready()` checks in app cleanup race asynchronous `fault()` and callbacks.
  In particular a host-service stop can fault the owner yet return success after
  the final check. Simply sampling again cannot restore already-retired state.
  Arbitrary legacy callbacks and consumers cannot be treated as an atomic
  retirement transaction; they must be refused or redesigned with a provable
  owner-scoped protocol and retained closure. Failed retirement must prevent
  restart and keep all still-needed code, contexts, images and backing live.
* Generic app and old host fixtures still need reconciliation; restore behavior
  only where durable reader exclusion and teardown dependencies are established.
  Do not weaken refusal or use always-true barrier callbacks to make tests green.
* No Windows/MSVC /WX full suite, SEH, instruction-patch exclusion, quit
  continuation, or native reader/worker census has been proven on this candidate.
  The runbook's budget-allocation failure note conflicts with ancestor b420d1a's
  recorded Windows fix and test claim; neither establishes this exact head.
* #304 `571fe8e` is the named handoff candidate, not a merged dependency of this
  branch; #184 must retain native preparation/activation refusal until a real
  product-owned barrier exists. Coordinate final exact SHAs and deliberate API
  choices with both owners; do not silently port this branch into either one.

A possible direction is a product-owned lifecycle object with serialized phases
for preparing, activation, closing admission, draining all enrolled readers
and worker joins, cleanup, and terminal retirement/fault. Each phase needs a
precise ownership and reentry contract; the transition into cleanup must exclude
new readers for its entire duration, and a fault or incomplete cleanup must
retain dependent backing and prevent restart. It cannot make arbitrary callbacks
failure-atomic or interpose on hidden native callers. Such paths stay unsupported
until their complete census, Windows behavior and failure handling are proven.
