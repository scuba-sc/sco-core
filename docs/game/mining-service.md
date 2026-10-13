<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- SPDX-FileCopyrightText: 2026 the sco-core contributors -->
# Natural mining public service

`game.mining` v1 (`sc_mining.h`) belongs to the game pack, not the neutral kernel.
The table is size-prefixed; adapters require complete callback extents. All public
calls are game-thread-only and require a live plugin owner. `enabled` is exactly
0 or 1. At most 64 owners can request mining independently. Unload removes only
that owner's request through spawn's existing release dispatcher.

`request` returns `SCO_OK` for accepted intent, including unavailable engines;
read `state` for acceptance. Disabling always removes that owner's intent.
`state` requires the full output extent, returns `SCO_OK` for a copied snapshot,
and reports owner-scoped requested/active, a refusal block and process counters.
OFF=0 means disabled, or no block when active; UNAVAILABLE=1 means native adapter,
hook or cleanup unavailable; ADMISSION=2 means fresh host evidence refuses;
RESTART=3 means a latched read/adapter fault. Counters are atomic observations,
not a transactional native engine snapshot. Promoted counts forwarded bits, not
spawned entities. No addresses or game object references cross the public ABI.

Production remains **UNAVAILABLE**: the native adapter has not been verified on
Windows/in game. No inference from row matches, teleport/spawner readiness or
launcher flags activates it. G1 `FreshAdmissionAllowed()` is a host-only predicate
that re-inspects offline session, anti-cheat absence, positively blocked official
network and correct target. Unknown/absent inspection and stopping hosts refuse.
The inspector and backing state must be thread-safe and process-retained, never
plugin-owned. Worker SEH guards native reads and admission; no worker calls plugins.

The retained detour forwards the original function once, unchanged unless flags
are exactly 6, LOD is 0/1, builder type is 0, spawn gate is 0 and harvest LOD is
2..100. Only then may flags become 14, after fresh admission. Read faults latch
restart-required. Stop/unload leaves detour, trampoline and callbacks retained for
process lifetime in pass-through mode; no worker hook removal is attempted.

Guard mechanics originate in sc-offline's `src/builtins/mining_plugin.cpp`, itself
derived from GPL-3.0 junikka/sc-offline-mining `src/mining.cpp`. The unsafe legacy
context predicate and product configuration/commands were not migrated. Portable
synthetic provider tests are not Windows SEH, native detour or gameplay evidence.
