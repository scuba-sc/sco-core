// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Native entity class-name lookup seam (K-41). The exact-name route needs the class registry the
// host resolved plus an owner-safe lifetime: the registry can be torn down and the class records
// it returned are only valid through the copy. Neither the receiver lifetime nor the class-free
// routes are grounded, so production installs nothing and refuses. The catalogue snapshot is a
// different seam and stays the only entity path that answers today.
//
// This seam is a point lookup only: it wires name, never classes or component, so it cannot be
// mistaken for a snapshot provider. Not run in game.
#include "entity_lookup.h"
#include "sco/runtime.h"
#include <cstdint>

namespace sco::game::services {

struct EntityLookupNativeRoute {
    // The class registry instance the host resolved for this build.
    void* registry = nullptr;
    // Exact class-name -> host-issued class id. Distinct from the catalogue snapshot: one name,
    // one id, no enumeration.
    sco_result (*class_by_name)(void* ctx, const char* name, uint64_t* class_id) = nullptr;
    // Owner-safe lifetime: nonzero while the registry behind the receiver is alive. A changed
    // value means it was torn down, so every id this seam handed out is stale.
    uint64_t (*registry_epoch)(void* ctx) = nullptr;
    void* ctx = nullptr;
    bool Complete() const { return registry && class_by_name && registry_epoch; }
};

// Why the last install refused; empty after a successful one. Static storage, never caller-owned.
const char* NativeEntityLookupRefusal();

// Installs the point-lookup backend. Refuses, naming the first missing piece, unless the route is
// complete; a refusal leaves no backend and every capability exactly as it was.
Result InstallNativeEntityLookup(const EntityLookupNativeRoute& route = {});

// The installed backend, or an empty one. classes, name and component are null until an install
// succeeds, and classes/component stay null after one: this seam answers exact names only.
EntityLookupBackend NativeEntityLookupBackend();

}  // namespace sco::game::services
