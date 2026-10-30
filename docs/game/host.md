# Star Citizen offline host composition (host-source)

`include/sco/game/host.h`, target `sco_game_host`, composes the game signature pack
with `sco::app`. This is a C++ source contract for trusted host builders, **not**
a plugin service, frozen wire ABI, or permission to patch the game. Released plugin
builds use `core` + `game-sc` SDK archives; kernel-only hosts do not compile or link
this adapter. During development, test against an exact source candidate and matching
artifacts/checksums across consumers; a later stable SDK and pin transition remain a
release decision. Windows and in-game behavior of this change have not been verified.

## Admission, resolve, start

1. The shell collects fresh OS evidence: correct target process, anti-cheat absent,
   official-network access blocked, and positively known offline session. All
   fields default to unknown; failed process/service/module inspections remain
   unknown. A launcher flag or a plugin capability is not sufficient evidence.
2. `Prepare(options)` refuses online/unknown/present evidence before registering
   any row or resolving the original image. It copies options, registers the game
   signature tables atomically, freezes and resolves them, and installs the sealed
   registry. Missing rows stay unavailable. Signature setup or ownership failure
   refuses preparation.
   The original image must be readable, with nonempty text/rdata; the adapter is
   not a general arbitrary-pointer validator. Prepare runs outside loader lock.
3. On the actual game thread, `Start()` re-inspects admission and calls `app::Start`
   exactly once. Platform image/registerSignatures are rejected: resolution has a
   single owner. Other platform inputs follow `sco/app.h` lifetime rules.
4. `Stop()` runs on the game thread, outside callouts/reload. Before destructive
   teardown, the host calls `retireConsumer(context)` at most once, including on
   failed preparation paths with dependent resources. `ConsumerRetirement::Quiesced`
   asserts a durable product-consumer gate through cleanup; null, `Retained` or an
   exception refuses teardown and retains the registry and code through process exit.
   The host also requires `platform.gameReadersQuiesced(context)` to confirm an
   externally held native-reader barrier before game-service start and before any
   applicable teardown. The barrier excludes in-flight and new native readers and
   target execution throughout the phase, permits safe synchronous cleanup reentry,
   and excludes the current quit control frame; a point-in-time sample is insufficient.
   A failed cleanup also retains dependencies. Process-retained quit or native
   DataCore detours keep the registry, image and provider code through process exit
   and make `Stop` return `Failed` even after services stop. Wrong-thread stop cannot
   claim completion. The product must independently prove hook-installation safety;
   this change has not established it.

Admission and input refusals before composition remain retryable. A failure that
sets the host phase to `Failed`, including signature setup failure, does not
restart composition in the same process. Host entries must be serialized; scalar `Status()`
is safe to observe from other threads, not an authorization grant.

```cpp
sco::game::host::Options opts;
opts.inspect = InspectOfflineEnvironment; // OS shell; unknown on every failed probe
opts.image = &originalImage;
opts.platform.hostVersion = "synthetic-host 1.0";
opts.platform.dataRoot = "data";
opts.platform.pluginRoot = "data/plugins";
opts.platform.pluginsEnabled = true;
opts.platform.gameServices = true;
opts.retireConsumer = RetireProductConsumers; // Quiesced only for a held, durable gate
opts.consumerContext = productState;
opts.platform.gameReadersQuiesced = NativeBarrierHeld; // verifies barrier held for the full phase
opts.platform.gameReadersContext = readerState;
// No image/registerSignatures/beforeDependentStop in opts.platform; host owns those.
auto prepared = sco::game::host::Prepare(opts);
// Later, from the game thread, only when prepared == Result::Ok:
auto started = sco::game::host::Start();
```

No network socket, anti-cheat modification, process launch, proxy DLL, Windows
message hook or renderer is created by this adapter. The shell keeps OS/render
policy. Admission must not silently treat an inspection failure as absence.

The optional `earlyDataRequirement` callback currently receives the requirement
name, a reference to the installed immutable snapshot, and the product's context;
it runs synchronously during `Prepare`, possibly before the game thread, and must
be side-effect-free. It must not retain the name or snapshot references. A null
callback or exception means unavailable; callback code and context must remain
valid through `Prepare`. The existing `platform.setCapabilities` callback instead
runs on the app game thread after the registry owner is pinned.

## Tick adapter

The shell keeps its Windows message hook, renderer frames and input polling. On
each game-thread pass call `Tick(nowMs)`; do not throttle the OS shell itself.
The adapter performs the first plugin tick immediately, then uses a configurable
unsigned millisecond cadence (default 100, zero means every pass), including clock
wrap. It drains app tasks and dispatches only the app tick; there are no product
feature fallbacks. A contained synthetic plugin fault stays disabled.

Admission is rechecked on every pass, before cadence filtering. Lost/unknown
admission stops the app and cancels queued tasks without running a tick. Nested
runtime-callout and wrong-thread entries refuse. The `game_host_loss` fixture
proves loss during a throttled pass does not execute a queued task.

## Quit adapter

By default Prepare requires the validated `system.quit` target and installs its
entry detour through the shared hook owner. Failure/missing row refuses startup,
not a silent WM_QUIT-only fallback. The adapter calls `BeforeQuit()` synchronously
before the engine original on **both** normal and fast shutdown. It reads no
CSystem/ICVar fast-shutdown fields. The engine receives the original system/info
pointers and exit code exactly once. OS WM_QUIT is a secondary Stop trigger, not
the sole shutdown guarantee; never call from DLL_PROCESS_DETACH/loader lock.

`QuitPolicy::External` is an explicit trusted-host integration route only when
the shell already guarantees synchronous `BeforeQuit()` on the game thread
before engine shutdown; it grants no plugin hook/patch service. It is used in
synthetic fixtures. Wrong-thread/callout quits cannot safely marshal a caller
stack to a future tick: teardown refuses/logs but the original engine call still
runs. This limitation is not represented as successful cleanup. A game-thread
quit is required for the lifecycle guarantee. The entry hook remains installed
for process lifetime, including after Stop; no in-flight detour is removed.

The private ForwardQuit seam tests actual adapter ordering/argument passthrough
without writing executable memory. ARM/macOS cannot prove a real x86-64 detour;
Windows hook/game proof remains owed.

## DataCore timing

With Platform dataCore enabled, Prepare starts the service and indexes folder
manifests/data packs without running plugin code; builtin inputs are refused on
this early route. Required CryPak transport is installed before engine load,
using the validated pak rows and a bounded Data/Game2.dcb transform. Start does
not reset an already LOADED service. After app startup, the frozen data inputs
are refreshed from the actual app list/content. The loader checks fresh offline
evidence, reads/parses a bounded base, then invokes the existing service pack/
saved-patch pipeline. Inspect must be thread-safe and non-reentrant on any loader
thread. Stop disables transport and drains active producers before plugin
teardown; service tables stay alive through unload.

External DataCore transport is an explicit synthetic/host integration route, not
a plugin service. The synthetic game_host_data fixture proves an early pack
load survives Start and late producers refuse after Stop. It does not prove
Windows CryPak hooks. Early discovery can only enforce manifest/readiness known
at that time, not predict a future plugin-load fault. Hosts must not enable
engine loading before Prepare completes.

## Decision pending owner / migration limits

This first composition slice deliberately does **not** copy the product's
`ApplyOfflinePatches`, fake service implementations, environment variables, or
feature fallback ticks. It accepts an already positively admitted offline
execution environment; it never turns online/unknown into offline. Engine
mutation policy and staged migration of those legacy mechanics remain separate
work. The product must not delete its remaining engine setup just because this
host target exists. G1 is not fully closed until those mechanics and the product
call sites migrate, and Windows/game acceptance is supplied.

Evidence: synthetic `game_host` CTest only, no game image or bytes. Not run in
game; Windows/MSVC, loader hooks, anti-cheat evidence collection, actual network
blocking and renderer behavior are not verified by this fixture.
