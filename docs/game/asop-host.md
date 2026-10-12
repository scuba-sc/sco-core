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

`Snapshot` reports composition status, not proof of gameplay readiness. A failed
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
exposed. Stored ships remain session-only. Lift-dependent retrieval stays unavailable
until lift composition is installed. The fixture compiles the same `delivery.inc`
loop, checking settle timing, duplicate refusal, unavailable spawn/store and timeout.
These synthetic primitive outcomes are not engine or gameplay evidence.

Source: GPL-3.0-only sc-offline `1ac5308a65ac0ba7955e4108bd1dc800449cfa69`,
`src/hooks.cpp` catalog mechanisms; new host composition and fixtures are
project-authored. Engine offsets remain interoperability facts, not game extracts.

Evidence: portable bounded catalog, invalid configuration and fail-closed admission
fixture. **Windows engine loading and gameplay NOT VERIFIED.**
