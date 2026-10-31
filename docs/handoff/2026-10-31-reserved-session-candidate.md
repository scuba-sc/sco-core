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

## Bounded engine consumer qualification

The first engine consumer should use a dedicated executable whose reviewed
source/link graph contains the authority once. A module-free executable is the
initial integration to qualify. If a cooperating module is included, it receives
copied mock operations through the executable's accessor; it must not embed core
or obtain startup/retirement controls. The existing fixture audits are examples,
not certification of the engine target. Check the actual engine target's sources,
dependencies, exports/imports and module identity on each supported platform.

Reserve before changing shared logging, mock configuration, registry state,
profile staging or external callbacks. Keep the complete dependency root and
session alive through process exit after either retained terminal result. Reject
restart and preserve native-unavailable reporting. `app::Stop` success and reserved
retained retirement are distinct contracts; the latter never permits freeing
staged files, providers, code or contexts.

The host must hold an outer `TryEnter` pin before invoking a consumer, through
provider execution, output construction/copy and unwinding. Pin each admitted
request/tick/action even when every table slot also pins itself. After Close,
refuse before entering the consumer. Copy all JSON/diagnostic outputs; do not
return borrowed graph or table views to callers. Multi-call read/update sequences
are not atomic transactions and must handle refusal or intervening updates.

Before qualification, pause an actual consumer request after its table read but
before its final output copy. Prove retirement reaches the reader-drain path,
remains Closing while that request holds its pin, and finishes only after the
output copy and pin unwind. Also test no-entry-after-Close, stale cached table
refusal, partial startup/fault retention, and terminal restart refusal using the
actual consumer executable. A sleep or a future that has not completed alone
does not prove retirement entered its drain path.

For that deterministic test only, compile both the authority and test consumer
with `SCO_RESERVED_DRAIN_TESTING=1`. The header then declares
`sco::reserved::testing::ReaderDrainObserved()`. Its monotonic lock-free signal
becomes true after retirement observes Closing with admitted readers under the
authority lock. Hold the consumer's output-copy barrier until this observation,
then verify retirement is pending before releasing the barrier. Make the paused
request the only held reader, and verify that removing its outer pin makes the
test fail; an unrelated held pin must not mask missing request admission. The
observer belongs in the authority executable/test host, not an independently
embedded DLL. Use a fresh process for every case: there is deliberately no reset
operation. The observer does not establish quiescence and is absent from ordinary
production builds.

The sealed queue is the only worker covered by this core table. Extra event-loop
threads, tasks, callbacks, listener code or module initializers require their own
complete host-owned admission and lifetime census before inclusion. Keeping core
backing rooted does not count those obligations or establish their quiescence.
Until source review and these product tests pass, successful fixture tests and
consumer-requirements reports are evidence of narrower contracts only.

Native/PAK activation stays refused pending a distinct product proof of
continuous reader and instruction exclusion. Original-quit continuation also
needs proof that worker code and argument objects remain valid after the game
starts destroying its own objects; retaining our code and capsule cannot supply
that proof.
