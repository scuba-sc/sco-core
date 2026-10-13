# Offline fleet host composition

Layer: Star Citizen game pack, host-source-only. Link `sco_asop_host` from a tagged
source dependency and include `sco/game/asop_host.h`. This header is not a plugin
header and is excluded, with `src/game/asop_host/`, from every SDK profile by the
existing game implementation exclusions. No plugin service, raw pointer or patch
right is added. The frozen ABI is unchanged.

`Config` is disabled by default. The host must positively establish offline
preparation and EAC absence; false/unknown refuses. Configure before engine calls,
after signature resolution and guarded game services startup. `CatalogMode::Game`
keeps the game's list (an unavailable offline entitlement query can be empty).
`Ships` copies at most 2048 host-supplied class names of at most 63 bytes, rejects
invalid/duplicate names, and resolves class GUIDs at runtime. No game list or GUID
table is bundled. The host owns list-file parsing and `asop` configuration policy.
The entitlements callback retains the existing bounded offline catalog mechanism.

`Snapshot` reports composition status and a copied installed-group bitmask, not
proof of gameplay readiness. `installed` records catalog-hook installation only;
consult `ready` for independently unavailable terminal/fleet/lift/hangar/ATC groups.
The existing class-based ATC request fallback remains when the stored-ship retrieve
path is unavailable; it also requires all its signature rows. Never install the
old product fleet/services hooks alongside this composition (one engine owner). A failed
installation requires process restart; never unload/restart the image after hook
installation. `Stop` closes custom admission, retaining backing state for engine
callbacks. Windows hooks are compiled only under `SCO_GAME_SC AND WIN32`; portable
configuration/catalog fixtures run on development systems.

## Delivery, store and retrieve port

The existing terminal open/caller/list hooks, delivery settle/store loop, claim
refresh, unstow takeover, retrieved-id matching and ATC cancellation are now in
`fleet_win.cpp`. Raw offsets/continuation mechanics remain game-pack-owned.
Existing guarded reads/spawn/mover registration replace product teleport/spawner
symbols. Only the internal delivery setup cvar is writable; no console route is
exposed. Stored ships remain session-only. Lift-dependent retrieval requires successful row checks and installation of the
sole lift close-handler owner. Existing pad-spawn lower/wait/timeout fallback,
platform spawn (capacity-zero borrowed engine string), spawned-id matching and
store-down/store/raise loops are preserved. The shared `lift_watch.inc` fixture
checks ClosedIdle/OpenIdle, unavailable/unknown state, exact timeout boundary and
one-shot raising. It does not prove physical lift movement. The fixture compiles the same `delivery.inc`
loop, checking settle timing, duplicate refusal, unavailable spawn/store and timeout.
These synthetic primitive outcomes are not engine or gameplay evidence.

## Hangar, synchronous continuation and ATC

`hangar_win.cpp` retains the existing scoped stand-in hub/futures, personal hangar
permissions, bubble correction, delayed gateway queue, keep-until-entered policy,
pad-token owner attribution and landing marker callbacks. `Pump` must run on every
window message, before the original 100 ms fleet tick cadence. Main-thread/visible/reentrant continuations run
inline. Others synchronously wait at most 3000 ms to be **taken**; an untaken call
runs on its original caller (no-deadlock fallback). Once taken, it must wait until
callback completion because its state belongs to the caller's stack. No next-tick
job may retain it. This is the original contract, expressed by the shared portable
`Handoff` helper, not a substitute simulation state machine.

Shutdown closes admission and wakes untaken waiters to execute their original
continuation; taken frames remain held until completion. Hooks, original function
pointers, stand-in objects and session records remain process-lifetime. The host
must keep the image loaded and join engine workers before final process teardown.
`Stop` is terminal, not hot reload or destruction, and is called on the game thread.
The fixture covers taken-frame lifetime past the deadline, reentrant callback,
untaken timeout, capacity overflow, shutdown before/after take and subsequent calls.

Source: GPL-3.0-only sc-offline `1ac5308a65ac0ba7955e4108bd1dc800449cfa69`,
`src/hooks.cpp` catalog mechanisms; new host composition and fixtures are
project-authored. Engine offsets remain interoperability facts, not game extracts.

Evidence: the focused `asop_host` CTest builds under warnings-as-errors and
ASan/UBSan. It covers bounded catalog/config/admission plus shared original delivery
and lift-watch loops and synchronous handoff. Raw engine translation units also
receive a development-only syntax probe using Windows/SEH declaration stand-ins;
that probe is **not** Windows ABI, SEH, linker or engine evidence. All new source
and fixture files are GPL-3.0-only and match the explicit provenance directory row;
the manifest's existing `src/game/` and `include/sco/game/` exclusions cover every
new implementation/host header, with no plugin-header or source-kit grant.

**Windows engine loading and gameplay NOT VERIFIED.**
