# Offline fleet host composition

The ASOP host composition carries sc-offline's offline ship terminal (fleet list, delivery, store and retrieve), hangar and ATC logic for a host product. It publishes no service table and has no table version or capabilities. Native startup is refused in every process, so nothing here reaches the engine. See [game capability status](status.md). Not run in game.

Layer: Star Citizen game pack, host-source only. Link `sco_asop_host` from a tagged source dependency and include `sco/game/asop_host.h`. This header is not a plugin header. It and `src/game/asop_host/` are selected only for `host-source-sc`, not for the public plugin or neutral core and core-source profiles. No plugin service, raw pointer or patch right is added, and the frozen ABI is unchanged. The signature rows it uses are in [ASOP rows](asop.md).

## Startup and configuration

`Config` is disabled by default. `Start` validates the configuration, the enabled flag, offline preparation and EAC absence, then returns `Unavailable` without publishing admission or installing hooks.

Offline and EAC admission and resolved signatures do not show that engine readers are quiescent while code is patched. The host has no proven safe-phase and reader or worker lifetime protocol for installation or later native mutation. A game-thread tick, an atomic slot store or a success-only callback is not such a protocol. Native ASOP startup stays refused until one exists.

`Pump` does nothing while admission is closed. `Stop` is terminal and performs no provider close operations when startup was refused.

`Ships` validation accepts at most 2048 host-supplied class names of at most 63 bytes, and rejects invalid or duplicate names. No game list or GUID table is bundled. The host owns list-file parsing and `asop` configuration policy. The selection picker stays file-only (see [ASOP selection](asop-selection.md)).

`Reconfigure(config)` always returns `Unavailable`, before validation, engine calls or publication, even for a valid admitted game-thread request. It keeps the previous configuration and catalog. No engine lifecycle contract establishes safe mid-session rebuilding or an overlap of rebuild and `Stop`. Game-thread admission, a publication lock and retained buffers do not give engine quiescence, so native reconfiguration stays an open boundary.

## Fleet catalog

The internal transactional fleet helper and its append-only slot table are kept. A ship URN uses a stable slot index, removed slots are not recycled, and a re-added class reuses its slot.

- On initial publication the helper can skip entries that a resolver positively establishes as absent, and it reports the skipped count.
- The native adapter has no proven readiness or authoritative absence signal. A null name lookup, a zero GUID or a completed traversal miss can reflect partial initialization, so each aborts the whole plan. So do faults and contradictory GUID round trips. A native list that holds both valid and missing entries therefore cannot yet be published under the initial skip policy.
- Capacity, invalid input and allocation failure leave the prior table and buffer intact. Later transactional replacements also refuse unresolved entries and keep the prior publication. Retired published buffers stay for the life of the process.

A nonblocking guard refuses concurrent or reentrant initial builds. Resolution and allocation happen outside the ships lock. Publication takes that lock and checks admission and the still-unpublished state. `Stop` closes admission under the same lock. An attempt sees closed admission before its next resolver call and discards its private plan, but this does not cancel or join an engine call already in progress. A failed deferred attempt does not repair the original entitlement response, and a later request may try a fresh build. Native registry lifetime, fault behavior and lock reentry still need Windows and engine validation.

## Status and stop

`Snapshot` reports composition status and a copied installed-group bitmask, not gameplay readiness. `installed` records catalog-hook installation only. Check `ready` for the independently unavailable terminal, fleet, lift, hangar and ATC groups.

The class-based ATC request fallback stays in place when the stored-ship retrieve path is unavailable. It also needs all its signature rows. Never install the old product fleet or services hooks alongside this composition: the engine has one owner.

A failed installation needs a process restart. Never unload or restart the image after hook installation. `Stop` closes custom admission and keeps backing state for engine callbacks. Windows hooks are compiled only under `SCO_GAME_SC AND WIN32`. Portable configuration and catalog fixtures run on development systems.

## Delivery, store and retrieve

The terminal open, caller and list hooks, the delivery settle and store loop, claim refresh, unstow takeover, retrieved-id matching and ATC cancellation are in `fleet_win.cpp`. Raw offsets and continuation mechanics stay game-pack-owned.

- Guarded reads, spawn and mover registration replace the product's teleport and spawner symbols.
- Only the internal delivery setup cvar is writable. No console route is exposed.
- Stored ships are session-only.
- Lift-dependent retrieval needs successful row checks and installation of the sole lift close-handler owner.
- The pad-spawn lower, wait and timeout fallback, platform spawn (a capacity-zero borrowed engine string), spawned-id matching and the store-down, store and raise loops are kept.

Synthetic fixtures check the shared `lift_watch.inc` states (ClosedIdle, OpenIdle, unavailable and unknown state, the exact timeout boundary, one-shot raising) and the `delivery.inc` loop (settle timing, duplicate refusal, unavailable spawn or store, timeout). They do not show physical lift movement, and they are not engine or gameplay evidence.

## Hangar, synchronous continuation and ATC

`hangar_win.cpp` keeps the scoped stand-in hub and futures, personal hangar permissions, bubble correction, the delayed gateway queue, the keep-until-entered policy, pad-token owner attribution and the landing marker callbacks.

`Pump` must run on every window message, before the original 100 ms fleet tick cadence. A main-thread, visible or reentrant continuation runs inline. Any other continuation waits synchronously for at most 3000 ms to be **taken**. An untaken call runs on its original caller (the no-deadlock fallback). A taken call must wait until its callback completes, because its state belongs to the caller's stack, and no next-tick job may retain it. This is the original contract, expressed by the shared portable `Handoff` helper.

Shutdown closes admission and wakes untaken waiters, which run their original continuation. Taken frames stay held until they complete. Hooks, original function pointers, stand-in objects and session records live for the process lifetime. The host must keep the image loaded and join engine workers before final process teardown. `Stop` is terminal, not a hot reload or a destruction, and is called on the game thread.

Windows engine loading is not covered by any check.
