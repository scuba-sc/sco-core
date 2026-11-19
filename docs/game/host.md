# Game host composition

`sco::game::host` ([`include/sco/game/host.h`](../../include/sco/game/host.h), link `sco_game_host`) is the C++ host-source API that a product uses to start Core and the game pack inside the game process. It is not part of the plain-C plugin ABI. It publishes no service table and has no table version or capabilities. Not run in game. See [game capability status](status.md).

The page covers the two host compositions, the lifecycle entries (`Prepare`, `Activate`, `Start`, `Tick`, `Stop`), the detailed report API, and what happens on a direct engine quit.

## The two compositions

`Options::composition` selects one composition for the whole process. The first `Prepare` fixes it, even when that attempt is refused. A refused process-retained attempt (admission, barrier, invalid options) leaves the host Idle with nothing installed, and the same composition may retry. A `Prepare`, `PrepareDetailed` or legacy entry for the other composition returns `BadArg` (a detailed refusal reports `ProcessRetained` with drain `Unknown`). A detailed capsule whose options ask for `ProcessRetained` is refused with `BadArg` rather than run strict. The two hosts are separate state machines and share only the fresh-admission inspector hooks.

| | Strict (default) | `ProcessRetained` |
|---|---|---|
| Runtime | Owned runtime with an owner-held `OwnedShutdown` | The normal, non-owned app runtime |
| Allowed | External quit and DataCore transport, optional synthetic DataCore service | Plugins, built-ins, scripts, native game services, host menu, host service and capabilities |
| Refused | Native game services and hooks, plugins, built-ins, scripts, host menus, host-service adapters, early requirement and capability callbacks, native DataCore, private sessions. Legacy configurations are refused before activation. | `shutdownOwner`, `earlyDataRequirement`, `retireConsumer`, `nativeDataCoreReads` and `platform.shutdownOwner` |
| `Stop` | Drains cooperative reads and workers, then returns `Failed` and retains everything | Closes admission and retains everything; returns `Ok` |
| Unload and restart | Refused | Refused |

Both compositions retain the app, registry, provider code, image and backing resources until process exit. Neither proves that hidden native execution, engine-owned objects or foreign callbacks have finished.

## Strict composition

The owner and every borrowed image, resolver, context and backing resource must outlive all calls and stay retained after a failed shutdown.

An owner lease is cooperative. It cannot interpose on hidden native execution, cached tables or unmanaged threads. Read leases are thread-bound and cover the complete use. A read destroyed on a different thread faults and closes the owner. That thread's origin stays ineligible for retained maintenance even if other reads from it end. Reusing its thread ID may conservatively refuse a later thread too. Another control thread may reap retained handles, which does not authorize retirement.

The strict host binds the runtime before it publishes DataCore. Subsystem publication needs an explicit private startup capability, and public runtime publication does not inherit startup authority from a reentrant callback. It reserves the global signature-registry slot empty and keeps its own registry private. A previously used legacy runtime or an occupied registry refuses owned startup.

`Prepare`, `Activate` and `Start` are separate entries.

- Owner admission is closed across all three phases. An already-entered reader or worker start prevents startup.
- Each entry holds a host transition and publishes its phase under the owner lock.
- A transition must be destroyed on the thread that acquired it. A foreign release faults and closes the owner, retains backing, and refuses maintenance from the original thread.
- A close or fault seen under the lock before publication prevents that publication and retains the partially created state. A request that linearizes after a successful publication does not cancel it.
- `Start` opens cooperative admission only together with the Running phase.

This is a synthetic lifecycle protocol, not a native barrier. Native preparation and mutation stay refused until the product supplies a real exclusion protocol and Windows synthetic-target proof.

The optional synthetic DataCore service can start during activation. Its transform refuses until Running, takes a read lease, and rejects reentry and concurrent transforms. If the owner faults during a transform, the result is refused and the mutated service state stays retained. Early engine loading and the earlier plugin and requirement callback scenarios are unsupported.

`Tick` takes a full-operation read lease and refuses reentry. The fresh-admission boolean is an observation, not a substitute for that lease. An owner fault or loss of admission closes runtime admission. Work already admitted can finish, and later queued callbacks stay retained.

`Stop` closes admission and tries to drain all cooperative reads and owned workers, including their thread-local destruction. It returns `Failed` and retains the app, registry, provider code, image and dependent backing, even after a successful drain. Existing cleanup can call arbitrary code or destroy state irreversibly, so there is no proved failure-atomic cleanup commit. Restart is refused, and the host must keep retained resources valid. Timed waits give no hard wall-clock deadline for mutex scheduling or thread creation. No fail-stop process termination is supplied.

## Process-retained composition

This composition is a contract that the product and shell keep, not something Core proves. It makes three promises.

**A. Retention.** Module code and every dependency (image, resolver provider, signature registry, admission inspector, plugin modules, callbacks, contexts) stay mapped and alive until process exit. Core's host and stop paths never unload or free them. One deliberate exception: managed plugin reload (`sco.reload`, `app::Reload`) stays available while Running and unloads and reloads plugin modules under the loader's own admission rules. It is a plugin-level operation, not host retirement, and has no retained-mode test.

**B. Install quiescence.** `platform.gameReadersQuiesced` must be non-null and truthful: engine execution is quiesced for as long as `Prepare` and `Start` install native hooks.

- Core calls it immediately before the first native mutation of `Prepare` (registry install, quit hook). Each internal entry calls it again immediately before its own patch: the DataCore pak enable and the game-service start.
- Null, `false` or a throw refuses with `Unavailable`, and that step installs nothing. Before the registry install the host is back at Idle. Later it is `Failed`.
- `imageOwner` and `resolverOwner` are required. Calls arrive on the documented threads (`Start`, `Tick` and `Stop` on the game thread).
- The attestation covers the installs made synchronously inside `Prepare` or `Start`: the quit detour, the pak loader detour, and every hook the game services install while starting (entities stream hooks, game events, quantum, and creative's magazine hook, which is installed in the creative start and never lazily).
- It does not cover installs that cannot be made at start. The pak loader's per-load CryPak slot swap runs on the engine's own loader thread inside the engine's call, so it is late by design and Core cannot quiesce it.
- No reachable code patches later. The ASOP host's installers sit behind `asop_host::Start`, which still refuses, and the DataCore live-read and mining hooks stay off.

**C. Stop is closure.** There is no reader or worker drain proof, no unload and no restart.

### Lifecycle

- `Prepare` refuses `Unknown`, `Present` and online evidence before any registration. It builds, registers, freezes and resolves a signature registry against the image with the supplied owners, and installs it as the global host registry (the app pins it at `Start`, and it is never cleared). It installs the quit hook when `quit == Required` and prepares early DataCore like the legacy host. A failure after the registry or a hook is installed leaves the phase `Failed`, retained. A refused attempt leaves the host Idle with nothing installed.
- `Activate` exists for API symmetry.
- `Start` (game thread) starts the normal runtime through an internal entry. That entry refuses unless the reader callback is present, no owner is bound and the runtime is not owned. Public `app::StartDetailed` keeps refusing `gameServices` for everyone else.
- `Tick` re-checks admission on every pass. Lost admission runs the same closure as `Stop`.
- `Stop` and `BeforeQuit` (game thread, not inside a callout) close admission and nothing else, in this order:
  1. Loader and managed-service admission close. Live pins or loader entries do not fail the stop. They stay retained and later acquisition refuses.
  2. The diagnostics worker is signalled to end itself, with no join.
  3. The private session stops accepting requests. A running session is asked to leave without joining its network thread. The broker, table and sockets stay allocated.
  4. `game.exit` is dispatched once.
  5. The host menu client is stopped and its owner released, which withdraws its subscriptions, commands, tasks and UI surfaces. No retained-mode test covers a configured host menu.
  6. Runtime admission closes and capabilities are revoked.

  Nothing else is unloaded, withdrawn, reset or freed: plugins, game and host services, the diagnostics publisher and snapshot store (its entries refuse through the runtime-stopping flag and the snapshot is no longer served), queued work and the registry all stay. `Ok` means closure, not retirement. The phase becomes `Stopped`. A second call is `Ok`, and `Prepare` and `Start` afterwards refuse.
- The quit detour runs the closure, then forwards the original quit exactly once on the same stack. A quit issued from inside that closure is absorbed by the outer detour. If closure cannot run there (a tick holds the control gate, the call is inside a callout, or it is off the game thread), the detour issues the non-waiting stop request instead. That closes the owner and the fresh admission, and the next `Tick` runs the closure. `game.exit` is then not guaranteed before the original quit runs.
- An optional `Options::shutdownOwner` is not bound to the runtime. The host drives only its admission: readers and workers are refused until `Running`, and `request_stop()` closes it at `Stop` or on admission loss. Core never calls `drain_until` or `fault()` for it. The product drains it from its own thread. In this composition `RequestStopDetailed` closes the owner from any thread or callout and reports `Ok`, `ProcessRetained`, a nonzero session ID and drain `Requested`. The next `Tick` runs the game-thread closure. `StatusDetailed` reports the retained phase with `ProcessRetained`.

### Native entries

Two private entries replace the blanket refusals of the public ones. Both are used only after `gameReadersQuiesced` returned true, and no other caller can reach them.

- The app start reaches the real game-service start. `game::services::Start()` and `app::StartDetailed` with `gameServices` still refuse everyone else.
- `dataCoreHook == Required` enables the DataCore pak hook (`pak::Enable` stays gated). The enable is latched: afterwards `pak::Disable()` is a refused no-op, and the detour, mounts and `vfs.pak` stay in place until exit.

The game services tick on the normal runtime while Running. `Stop` never stops, withdraws or finalizes them. Providers stay published, and lookups, publication and events refuse through the runtime-stopping flag. Tables a caller already holds keep working, which is the retention contract.

### Process exit

Once the retained runtime starts, no destructor reachable at static destruction joins a thread, blocks or terminates. That holds whether or not closure ran, including a game that calls `ExitProcess` without the quit hook. The objects that own a thread, or that a running thread reads, live in static storage that is never destroyed in this composition: the diagnostics worker with its snapshot store and capture, and the network service's thread and shared state (the thread of a hosted or joined private session lives there). The host's own copies of an optional `shutdownOwner`, its Options and the registry are immortal too, so an owner's worker join never runs from Core. Other compositions destroy all of these at exit.

At `ExitProcess` the OS has already ended the other threads. A normal `exit` with threads still running is also safe, because their state outlives them. A product-held owner, or any thread the product starts itself, remains the product's responsibility.

### Not claimed

Reader or worker drain, unload, restart, engine-object lifetime inside hooks, and any in-game behavior. Opening the native entries does not enable every service. Many stay off or unavailable on their own terms: per-service opt-ins such as `SCO_CVARS_NATIVE`, `SCO_LOADOUT_ASYNC` and `SCO_AUDIO_NATIVE`, native mining and DataCore live reads, and backends that no production code installs. `Options::nativeDataCoreReads` is still refused.

## Detailed reports

`HostCapsule` and the entries `PrepareDetailed`, `ActivateDetailed`, `StartDetailed`, `StopDetailed`, `RequestStopDetailed` and `StatusDetailed` report whether dependencies were exposed and how far cooperative drain got. They start the strict runtime through the private `app::detail::StartOwned`, not public `app::StartDetailed`. The older `Result`-returning entries still exist but cannot tell a product whether a failed startup exposed dependencies.

`HostReport` is a C++ value returned by value. Its layout and the `app::Platform` layout differ from the SDK 2.4.0 source, so rebuild all host consumers and Core together. The plugin C ABI and `game.build` v1 are unchanged.

### Capsule

Build the capsule privately with `HostCapsule(options, backingOwner)` before calling `PrepareDetailed`. The capsule owns a copy of the Options, the Image descriptor and the host-version string.

- `backingOwner` must actually own every referenced image byte, mapping and accepted context. A dummy shared pointer, a no-op deleter or a borrowed pointer is not ownership, and a non-null token cannot prove it.
- Resolver and inspector code must be strongly pinned or linked into the audited, process-resident executable.
- Construction can throw without exposing anything in Core. The original input pointers must stay valid through construction.

The detailed path is restricted to external quit and data hooks. It allows no native game services, native DataCore, DataCore or storage root, plugins, built-ins, scripts, private sessions or host adapters. It enrolls no app or global raw service tables, mapped views or foreign workers. A separately audited capsule-owned copy facade must hold the owner's Read through its last output copy and unwind. Rooting dependencies is not a native execution barrier.

`PrepareDetailed` first allocates retention storage, then roots the capsule, then runs the existing `Prepare` steps (reserve the registry, bind the runtime, copy Options, call the inspector). The root also pins the private registry and is not destroyed during module-static teardown. Adoption issues one nonzero process-scoped session ID. There is no second session or root replacement in a process. A failed adopted `Prepare` closes or faults its owner and keeps the capsule, including after a failure partway through Options copying.

### Reading a report

Each `HostReport` holds the operation result, the observed phase, the session ID and a dependency disposition: `NeverExposed` or `ProcessRetained`.

- A fresh invalid or unsupported request, or an allocation failure before adoption, can be `NeverExposed`.
- Every post-adoption result is `ProcessRetained`, including refusal, exception, wrong-thread calls and stop failure. No result authorizes destruction, unload or restart.
- The existing activation refusal may retry the same prepared transition. It cannot replace the capsule or start a new session.
- Legacy control use before adoption reports `ProcessRetained` with ID zero and prevents `PrepareDetailed`, because its caller-owned backing is not adopted retroactively. A refused overlapping control before adoption latches the same floor. A later legacy idle `Stop` returning `Ok` cannot clear it. ID zero does not assert that a new input was rooted.
- `NeverExposed` does not release aliases retained by another invocation. Only the completed `PrepareDetailed` report classifies adoption of that invocation's input.
- `StatusDetailed` is an observation, not an admission permit or cleanup certificate. Busy means the first gate observation found the gate held: it returns `Unavailable` with conservative retained data and is not a completed control or adoption decision. Pure status observation does not latch exposure or poison a private prepare that later fails. Overlapping status calls can return older observations after newer ones, so there is no ordering guarantee.

The `drain` field is a separate `DrainDisposition`: `NotAdopted`, `Open`, `Requested`, `InProgress`, `Drained`, `Retained` or `Unknown`. The reported phase is only the observed host phase and can still be `Running` after a request closes admission. Do not use phase or `drain` to admit work or to show cleanup as complete. `Drained` records the successful completion of the owner's one drain attempt. It stays a historical cooperative reader and worker observation even if the host later faults the owner and latches failure.

One nonblocking control gate covers the old and detailed game-host transitions. The separate close request uses only the already-rooted owner mutex. No mutex spans the inspector or other callouts, and synchronous reentry refuses. Internal transitions preserve Start-to-Activate and Tick-to-Stop behavior without giving an external callback ambient startup authority.

### Request closure before control-thread drainage

```cpp
// include/sco/owned_shutdown.h
void sco::OwnedShutdown::request_stop();

// include/sco/game/host.h (link sco_game_host)
sco::game::host::HostReport sco::game::host::RequestStopDetailed() noexcept;
```

`request_stop()` closes new read, worker-start and transition admission under the existing owner mutex and sets the cooperative worker stop predicate. It returns without waiting for admitted readers, reserved worker starts, worker bodies, callable destruction or TLS. A reader, worker, callable constructor or startup callback can request stop and then unwind normally. The request does not fault the owner, use up its one drain attempt, set readiness, run user code or release dependencies. Repeating it keeps an existing fault or drain result. A worker start that was already reserved may still enter its body with the stop predicate set. That start stays counted until its handle is published or cancelled.

It does not wait, but it is not lock-free. Mutex scheduling has no hard deadline, and acquiring the mutex can throw. Do not call it from `DllMain`, under the loader lock or from signal handlers. If it throws, closure is unproven and the caller must retain the whole owner and backing closure. Every ordinary use must already hold the same owner's Read through its final output copy.

`RequestStopDetailed()` takes the owner from the immutable, process-retained capsule root. It does not take the host control gate or read mutable Options. An admitted callback or the `Prepare` inspector can therefore request closure without waiting for its own frame or inheriting publication authority. Its `drain` field snapshots cooperative owner progress after the request, and later reports can show later progress. It runs no inspector, OS policy action, app `Stop`, cleanup callback or phase transition. The capsule and registry stay rooted on every outcome.

| Request result | Meaning |
| --- | --- |
| `Ok`, adopted session ID, `ProcessRetained` | The rooted owner's admission is closed and enrolled workers see the stop request. No drain or cleanup is claimed. |
| `Unavailable` | No root was observed, so the request was not delivered or queued for a later capsule. A retained report seen concurrently does not turn the refusal into closure proof. |
| `Failed`, adopted session ID, `ProcessRetained` | The close call threw. Closure is unproven; retain everything. |

If closure is observed under the owner lock before the `Prepare` transition publishes success, that publication is refused and partial state stays retained. A request that overlaps `Prepare` in wall-clock time but linearizes after a successful publication does not cancel it.

The offline consumer sequence is split across owners of work:

1. Before exposure, build the real dependency closure and retain the same `std::shared_ptr<OwnedShutdown>` in the `HostCapsule` and in all admitted product readers and workers. Use `PrepareDetailed` to root an accepted capsule. A dummy backing token cannot enroll foreign threads or pin native code.
2. An admitted frame calls `RequestStopDetailed()` (or its own owner's `request_stop()` for a private, not-yet-adopted composition). It releases its read only after its final output copy and callback return obligations.
3. A separate control thread, outside all that owner's reads, workers, worker constructors and destructors, and startup transitions, calls `owner->drain_until(deadline)`. Serialize this once-only attempt in the product. Never run it inside the worker being joined or while the controller holds a read. Retain the capsule, owner and transitive backing through closure and drainage.
4. The host reports `Requested` before the owner drain, `InProgress` while it runs, and `Drained` only when the owner's drain attempt actually completed. A later `StopDetailed` reuses that completed proof and does not call the one-shot drain again. Its `result` still reports retained failure, and `dependencies` stays `ProcessRetained`. `drain: Drained` never authorizes app cleanup. If a drain is `InProgress`, failed or uncertain when `StopDetailed` arrives, the host retains failure and refuses a second attempt. Finish the product drain before calling game-thread `Stop`.
5. A true drain result proves that the cooperative census completed and that worker, callable, TLS and helper joins actually happened. It does not prove application cleanup, foreign callback return, native instruction exclusion, safe original-quit continuation or module unload. A false or exceptional drain is a retained failure. Use `reap_retained_until` only for the later handle maintenance described below. The adopted game host stays `ProcessRetained` on either outcome. `StopDetailed` is a game-thread operation that returns retained failure. It is not a control-thread cleanup method.

An owned close or drain does not make the separately preserved startup snapshot writer eligible to run on this restricted path.

### Worker drain and retained maintenance

A successful worker drain joins both worker threads (including callable and TLS destruction) and its internal helper before it publishes readiness. The process root still retains backing. If the timed wait expires before the helper's worker-join completion marker is seen, the helper and owner stay retained, and a later exit cannot upgrade the terminal failure. That path never detaches a thread or performs an unbounded join. After the marker is seen, the final helper join can still be scheduler-delayed, so the deadline is rechecked.

`OwnedShutdown::reap_retained_until(deadline)` is explicit post-failure handle maintenance.

| Result | Meaning |
|---|---|
| `ReapedRetained` | Worker, callable, TLS and helper joins actually happened. |
| `PendingRetained` | A later maintenance retry is allowed. |
| `Refused` | Wrong phase, self-obligation or overlapping join control. |

It reuses an existing helper instead of starting another. All three results keep the original failed retirement, closed admission, backing root and restart refusal. None permits destruction, unmapping, provider cleanup or native activation. A completed call cannot upgrade `FailedRetained` to `QuiescedRetained` or make `ready()` true. The caller must retain the owner and all backing through every maintenance call, because nothing is collected automatically after a timeout.

The legacy `retireConsumer` and native barrier callbacks are not accepted as ownership proof. A successful retirement would need a complete reader and worker census, owner-controlled cleanup, failure injection and Windows native evidence. A callback that merely returns success provides none of these. Native evidence is needed for native consumers, not for a bounded non-native fixture.

## Direct engine quit and dependent providers

In the strict composition, the direct detour requests non-draining closure of the process-retained legacy owner, then calls the original engine quit synchronously with the original arguments.

- The owner is published before legacy startup transitions. An actual detour call before owner publication marks the attempt terminal, and a later publication refuses startup and attempts to close the supplied owner. The detour cannot close an owner it has not yet been given.
- A failed owner request leaves closure unproven. If the notification returns, a non-null original runs once.
- The request may block on the mutex, and structured exceptions remain unproved.
- The public `BeforeQuit` keeps its synchronous `Stop` meaning.
- Neither operation proves that engine-owned objects survive outstanding product reads.
- The detour does not call `app::Stop`, dispatch `game.exit`, unload plugins, or run `game::services::Stop` or `WithdrawGameServices`.
- Current activation policy rejects the required-hook configuration before installation.
- The terminal marker affects only legacy required-hook preparation with an owner. It is not a process-wide barrier for external or detailed compositions. Published legacy owners stay retained after a failed preparation, and a later owner cannot replace them.
- If quit closes an owner while an inspector is already running, preparation may still do registry or image work before its transition fails. No check proves that work is safe after the engine original has run.

The order matters to dependent services. `app::Stop` dispatches `game.exit` while services are still published, then stops plugins and game services. A direct `BeforeQuit` does not run that sequence, so an ASOP `game.exit` subscriber cannot withdraw its provider on this path. The owned `HostCapsule` refuses game services, plugins and product host-service adapters, so it cannot own or withdraw the offline product's ASOP composition.

No safe ASOP completion API exists on this path. Calling `app::Stop` inside the quit detour would run synchronous event, plugin and provider callbacks on the engine hook. The detour's `OwnedShutdown::request_stop()` closes only this owner's admissions. It does not notify the product controller or drain ASOP callbacks, and the original quit could end the process before another safe game-thread turn. The request takes the owner mutex and is not a hard-time-bounded or lock-free hook primitive.

A product needs a shell-owned pre-quit notification or an engine-specific deferral mechanism, plus a completion receipt that the detour can observe. On a safe game-thread path the product must:

1. Keep `game.exit` available until its callbacks return.
2. Close dependent-provider admission.
3. Establish the provider's own reader, callback and native-hook quiescence.
4. Withdraw the provider only after that proof.

The detour may forward the original quit exactly once, and only after it observes that completed sequence. It must not repeat teardown or wait for completion on the hook. The current offline product has neither the pre-quit notification nor the completion receipt. If it cannot arrange the sequence before the engine may terminate, the dependent provider and its backing stay retained, and ASOP withdrawal on direct quit is unresolved.

A native ASOP hook adapter also needs its own hook-entry census and return barrier. `OwnedShutdown` and service-table withdrawal do not supply them. Before native ASOP activation, a product has to show on Windows that:

- `game.exit` callbacks can still query the published ASOP provider.
- No provider withdrawal happens until the callbacks and the provider-owned readers have returned.
- The detour sees one completion receipt and forwards the original quit once.
- Missing or uncertain completion retains the provider and backing.
- Hook admission closes and all entered hooks return before any hook target, table or backing is withdrawn.
