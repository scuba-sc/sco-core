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
a callback that merely returns success cannot provide those guarantees.
