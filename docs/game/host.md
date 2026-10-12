# Star Citizen offline host composition (host-source)

`include/sco/game/host.h`, target `sco_game_host`, composes the game signature pack
with `sco::app`. This is a C++ source contract for trusted host builders, **not**
a plugin service, frozen wire ABI, or permission to patch the game. Plugins still
build from `core` + `game-sc` public SDK archives. Kernel-only hosts do not compile
or link this adapter.

## Admission, resolve, start

1. The shell collects fresh OS evidence: correct target process, anti-cheat absent,
   official-network access blocked, and positively known offline session. All
   fields default to unknown; failed process/service/module inspections remain
   unknown. A launcher flag or a plugin capability is not sufficient evidence.
2. `Prepare(options)` refuses online/unknown/present evidence before registering
   any row or resolving the original image. It copies options, registers the game
   signature tables, then resolves every row. Missing rows stay unavailable.
   The original image must be readable, with nonempty text/rdata; the adapter is
   not a general arbitrary-pointer validator. Prepare runs outside loader lock.
3. On the actual game thread, `Start()` re-inspects admission and calls `app::Start`
   exactly once. Platform image/registerSignatures are rejected: resolution has a
   single owner. Other platform inputs follow `sco/app.h` lifetime rules.
4. `Stop()` runs on the game thread, outside callouts/reload. Runtime teardown
   retains the existing app ordering and cancellation contract. Repeated stop is
   harmless, and wrong-thread stop cannot claim completion.

A refusal remains retryable before startup; completed/failed composition does not
restart in the same process. Host entries must be serialized; scalar `Status()`
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
// No image/registerSignatures in opts.platform.
auto prepared = sco::game::host::Prepare(opts);
// Later, from the game thread, only when prepared == Result::Ok:
auto started = sco::game::host::Start();
```

No network socket, anti-cheat modification, process launch, proxy DLL, Windows
message hook or renderer is created by this adapter. The shell keeps OS/render
policy. Admission must not silently treat an inspection failure as absence.

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
