// SPDX-License-Identifier: GPL-3.0-only
// K-41 native entity class-name seam: refuses without a grounded registry receiver lifetime, and
// answers only exact names — never a snapshot. Synthetic routes only; no game memory is read.
#include <sc_entities.h>
#include "../src/game/services/entity_lookup_engine.h"
#include <cstdio>
#include <cstring>
using sco::Result;
namespace G = sco::game::services;

static int pass = 0, fail = 0;
#define CHECK(x) do { if (x) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

namespace {
struct Fixture {
    int registry = 0;
    uint64_t epoch = 7;
    uint64_t id = 42;
    sco_result answer = SCO_OK;
    int calls = 0;
};
sco_result ByName(void* ctx, const char* name, uint64_t* out) {
    auto* f = static_cast<Fixture*>(ctx);
    if (!f || !name || !out) return SCO_BAD_ARG;
    ++f->calls;
    if (f->answer != SCO_OK) return f->answer;
    if (std::strcmp(name, "AEGS_Gladius") != 0) return SCO_NOT_FOUND;
    *out = f->id;
    return SCO_OK;
}
uint64_t Epoch(void* ctx) {
    auto* f = static_cast<Fixture*>(ctx);
    return f ? f->epoch : 0;
}
}  // namespace

int main() {
    CHECK(G::NativeEntityLookupBackend().name == nullptr);
    CHECK(std::strcmp(G::NativeEntityLookupRefusal(), "") == 0);

    CHECK(G::InstallNativeEntityLookup({}) == Result::Unavailable);
    CHECK(std::strstr(G::NativeEntityLookupRefusal(), "class registry") != nullptr);

    Fixture f;
    G::EntityLookupNativeRoute route;
    route.registry = &f.registry;
    CHECK(G::InstallNativeEntityLookup(route) == Result::Unavailable);
    CHECK(std::strstr(G::NativeEntityLookupRefusal(), "registry receiver lifetime") != nullptr);

    route.class_by_name = &ByName;
    CHECK(G::InstallNativeEntityLookup(route) == Result::Unavailable);
    CHECK(std::strstr(G::NativeEntityLookupRefusal(), "teardown generation") != nullptr);

    route.registry_epoch = &Epoch;
    route.ctx = &f;
    f.epoch = 0;
    CHECK(G::InstallNativeEntityLookup(route) == Result::Unavailable);
    CHECK(std::strstr(G::NativeEntityLookupRefusal(), "no live generation") != nullptr);
    CHECK(G::NativeEntityLookupBackend().name == nullptr);

    f.epoch = 7;
    CHECK(G::InstallNativeEntityLookup(route) == Result::Ok);
    CHECK(std::strcmp(G::NativeEntityLookupRefusal(), "") == 0);
    auto backend = G::NativeEntityLookupBackend();
    CHECK(backend.name != nullptr);
    // A point lookup only: this seam never claims a snapshot or component answers.
    CHECK(backend.classes == nullptr && backend.component == nullptr);

    CHECK(G::InstallNativeEntityLookup(route) == Result::Failed);
    CHECK(std::strstr(G::NativeEntityLookupRefusal(), "already installed") != nullptr);

    uint64_t id = 0;
    CHECK(backend.name(backend.ctx, "AEGS_Gladius", &id) == SCO_OK && id == 42);
    CHECK(backend.name(backend.ctx, "ANVL_Arrow", &id) == SCO_NOT_FOUND);
    CHECK(backend.name(backend.ctx, nullptr, &id) == SCO_BAD_ARG);
    CHECK(backend.name(backend.ctx, "AEGS_Gladius", nullptr) == SCO_BAD_ARG);

    // A host id of zero is not a class, whatever the provider says.
    f.id = 0;
    CHECK(backend.name(backend.ctx, "AEGS_Gladius", &id) == SCO_FAILED);
    f.id = 42;

    // The registry was torn down: every id this seam would answer is stale, so it refuses.
    f.epoch = 8;
    CHECK(backend.name(backend.ctx, "AEGS_Gladius", &id) == SCO_UNAVAILABLE);

    std::printf("entity lookup engine: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
