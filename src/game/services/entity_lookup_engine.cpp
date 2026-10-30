// SPDX-License-Identifier: GPL-3.0-only
#include "entity_lookup_engine.h"
#include "sco/host.h"
namespace sco::game::services {
namespace {
EntityLookupNativeRoute g_route;
EntityLookupBackend g_backend;
const char* g_refusal = "";
// The registry generation this seam was installed against. A different value means the registry
// was torn down since, so the ids it would answer are stale and the call is refused.
uint64_t g_epoch = 0;

bool OnGame() { return OnGameThread(); }

sco_result Name(void*, const char* name, uint64_t* out) {
    if (!name || !out) return SCO_BAD_ARG;
    if (!g_route.class_by_name || !g_route.registry_epoch) return SCO_UNAVAILABLE;
    if (g_route.registry_epoch(g_route.ctx) != g_epoch) return SCO_UNAVAILABLE;
    uint64_t class_id = 0;
    const sco_result r = g_route.class_by_name(g_route.ctx, name, &class_id);
    if (r != SCO_OK) return r;
    // Host-issued ids are nonzero by contract; zero is not a class and is never reported as one.
    if (!class_id) return SCO_FAILED;
    *out = class_id;
    return SCO_OK;
}
}  // namespace

const char* NativeEntityLookupRefusal() { return g_refusal; }

Result InstallNativeEntityLookup(const EntityLookupNativeRoute& route) {
    if (g_backend.name) {
        g_refusal = "a native entity lookup backend is already installed";
        return Result::Failed;
    }
    if (!route.registry) {
        g_refusal = "the class registry instance was not resolved";
        return Result::Unavailable;
    }
    if (!route.class_by_name) {
        g_refusal = "no exact class-name lookup: the registry receiver lifetime is not grounded";
        return Result::Unavailable;
    }
    if (!route.registry_epoch) {
        g_refusal = "no registry teardown generation";
        return Result::Unavailable;
    }
    const uint64_t epoch = route.registry_epoch(route.ctx);
    if (!epoch) {
        g_refusal = "the registry reports no live generation";
        return Result::Unavailable;
    }
    g_route = route;
    g_epoch = epoch;
    g_backend = {};
    g_backend.ctx = nullptr;
    g_backend.on_thread = &OnGame;
    g_backend.name = &Name;
    g_refusal = "";
    return Result::Ok;
}

EntityLookupBackend NativeEntityLookupBackend() { return g_backend; }

}  // namespace sco::game::services
