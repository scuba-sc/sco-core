# Reserved mock session: candidate host-source contract

**HOLD — implementation and integration review in progress.** This is a bounded
mock-graph candidate, not app cleanup, plugin compatibility, native readiness, or
a gameplay claim. The executable must link one core/runtime and authority, keep
all their code mapped until process exit, and export the reserved operations to
cooperating modules. Modules import those operations; they must not embed a
second runtime. An unmanaged copy that never binds can bypass this protocol.

The declarations are in `include/sco/reserved_session.h`. The C++20 bootstrap
and table are host-source interfaces, not the frozen C plugin ABI. Participating
modules need the same compiler, C++ standard library, CRT and steady-clock ABI.
The current candidate bootstrap version is 3 and the mock table version is 5.
Any future layout change requires an independently versioned interface.
Wrappers embed bootstrap storage: rebuild every participant together; binaries
built against earlier unpublished candidate layouts cannot be mixed with this one.

The trusted executable creates a process-rooted authority, calls
`BindCoreInstance`, and calls `ReserveSession(binding, graph, session)` before
shared logging, registry installation, callback publication, runtime startup,
profile staging or other consumer-visible mutation. The seed is a complete
by-value copy: it contains bounded zones and entities with no pointers, views,
callbacks, or borrowed backing. An invalid seed is rejected before reservation;
the caller must keep it stable throughout the call. Names must be nonempty and
NUL-terminated, coordinates finite, identifiers unique within each kind, and
zone references acyclic and valid. A retained reservation is terminal: neither
success nor failure permits another session in this process.

`BeginStartup` issues a noncopyable, thread-bound `StartupControl`. Only the
holder can call `PublishTypedProvider` and `FinishStartup`; code invoked on the
same thread does not inherit this capability. `QueryMock` supplies a rooted,
executable-resident table. Its enumerated operations provide copied scalar,
zone and entity reads, entity upsert/removal, a scalar tick counter, and a
separate unsigned sealed task value. Enumeration across calls is not an atomic
snapshot, and returned values are owned copies. Queue operations execute in
ticket order. Tracked enqueue returns a nonzero ticket; copied queue progress
reports the last completed ticket, so completion of a particular operation is
unambiguous even when successive values are equal. The queue is not an engine
Tick or cvar implementation.
No raw returned view, arbitrary provider, plugin, script, native hook, foreign
callback or extra worker is supported by this table.

Every table entry takes admission for the entire invocation, including output
copying. Cached table calls refuse after closure. `start_worker` starts at most
one sealed executable-owned queue worker. Queued payloads are by value; no
foreign callback or context is retained. `Close` rejects further admission and
closes that queue. `RetireUntil` waits for admitted calls, queued work, the
worker, callable destruction and worker TLS destruction; then it performs a
state-only terminal commit. `QuiescedRetained` describes only that complete
bounded census. `FailedRetained`, timeout, uncertain failures and self-retire
retain the capsule and refuse restart. Neither terminal result authorizes
resource destruction, module unload or `app::Stop == Ok`. Deadline checks do
not strictly bound scheduler delay, lock acquisition or the final join.

Legacy `StartDetailed` remains its prior API and can admit callback reentry when
used outside a reserved session. It is not a reserved mock-session start. The
reserved gate refuses legacy entrypoints that participate in it, but this does
not make arbitrary OS calls, foreign static cores, raw SDK tables or unclassified
legacy APIs safe. Engine adoption must use and test the bounded table rather than
infer safety from a boolean startup or lookup result. Existing copied graph
fields do not implement the full engine scene, inventory, services or editor.

The cooperating executable must restrict its reachable operations to the
reserved interface and explicitly reviewed facades. In particular, low-level
`sco::hook` patch and allocation operations, scanner APIs returning raw image
addresses, plugin `ModuleOps`, generic `OwnedShutdown` workers,
`surfaces::detail` and game-pack mutators are not enrolled in this session.
Some remain callable in a general core build and do
not all pass through the reservation gate. In particular, prior hook installation
or direct module loading does not dirty that gate, so a successful reservation
does not prove those operations never occurred. A broader composition needs its
own complete admission and lifetime coverage.

The test-only `reserved_curated_composition` fixture builds a separate authority
executable and a cooperating value-only module. The host retains the module,
holds an outer call pin through each admitted module invocation, and exercises
copied entity operations and the sealed worker queue. A controlled pause between
two table calls verifies retirement waits for the outer pin. Build-graph and
binary checks examine the named test artifacts, with injected negative cases;
they are not certification of another executable or protection against an
unmanaged module or untrusted build directory. Fixture barriers, direct post-close
probes and the reader-drain observation exist only for tests. A product still
needs an audited composition and its own consumer integration.

Native/PAK activation stays refused pending a distinct product proof of
continuous reader and instruction exclusion. Original-quit continuation also
needs proof that worker code and argument objects remain valid after the game
starts destroying its own objects; retaining our code and capsule cannot supply
that proof.
