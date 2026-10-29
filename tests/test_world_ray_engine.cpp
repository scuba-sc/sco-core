// SPDX-License-Identifier: GPL-3.0-only
// K-42 native filtered ray seam: refuses without the resolved zone/world/grid references and a
// skip-request lifetime, publishes no hit when a call is invalidated or faults, and latches
// unavailable for the rest of the process after either. Synthetic routes only.
#include <sc_world_control.h>
#include "../src/game/services/world_ray_engine.h"
#include <cstdio>
#include <cstring>
using sco::Result;
namespace G = sco::game::services;

static int pass = 0, fail = 0;
#define CHECK(x) do { if (x) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

namespace {
struct Fixture {
    int zone = 0, world = 0, grid = 0;
    uint64_t skip_generation = 3;
    sco_result answer = SCO_OK;
    bool move_generation = false;
    int calls = 0;
    uint32_t skip_seen = 0;
};
sco_result SingleHit(void* ctx, uint64_t, const double[3], const double[3], double,
                     const uint64_t*, uint32_t skip_count, sc_world_hit* out) {
    auto* f = static_cast<Fixture*>(ctx);
    if (!f || !out) return SCO_FAILED;
    ++f->calls;
    f->skip_seen = skip_count;
    if (f->move_generation) f->skip_generation += 1;
    if (f->answer != SCO_OK) return f->answer;
    out->flags = SC_WORLD_HIT_ENTITY;
    out->entity_id = 77;
    out->pos[0] = 1.0;
    out->distance = 12.5;
    return SCO_OK;
}
uint64_t Skips(void* ctx) {
    auto* f = static_cast<Fixture*>(ctx);
    return f ? f->skip_generation : 0;
}
sc_world_hit Hit() {
    sc_world_hit h{};
    h.size = sizeof(sc_world_hit);
    return h;
}
}  // namespace

int main() {
    CHECK(G::NativeWorldRayFiltered() == nullptr);
    CHECK(std::strcmp(G::NativeWorldRayRefusal(), "") == 0);

    CHECK(G::InstallNativeWorldRay({}) == Result::Unavailable);
    CHECK(std::strstr(G::NativeWorldRayRefusal(), "zone, world or grid") != nullptr);

    Fixture f;
    G::WorldRayNativeRoute route;
    route.zone = &f.zone;
    route.world = &f.world;
    CHECK(G::InstallNativeWorldRay(route) == Result::Unavailable);
    CHECK(std::strstr(G::NativeWorldRayRefusal(), "zone, world or grid") != nullptr);

    route.grid = &f.grid;
    CHECK(G::InstallNativeWorldRay(route) == Result::Unavailable);
    CHECK(std::strstr(G::NativeWorldRayRefusal(), "single-hit query") != nullptr);

    route.single_hit = &SingleHit;
    CHECK(G::InstallNativeWorldRay(route) == Result::Unavailable);
    CHECK(std::strstr(G::NativeWorldRayRefusal(), "skip-request lifetime") != nullptr);
    CHECK(G::NativeWorldRayFiltered() == nullptr);

    route.skip_generation = &Skips;
    route.ctx = &f;
    CHECK(G::InstallNativeWorldRay(route) == Result::Ok);
    CHECK(std::strcmp(G::NativeWorldRayRefusal(), "") == 0);
    auto filtered = G::NativeWorldRayFiltered();
    CHECK(filtered != nullptr);

    CHECK(G::InstallNativeWorldRay(route) == Result::Failed);
    CHECK(std::strstr(G::NativeWorldRayRefusal(), "already installed") != nullptr);

    // A hit is copied through, with the skip array only borrowed for the call.
    const double from[3]{0, 0, 0}, dir[3]{1, 0, 0};
    const uint64_t skips[1]{9};
    auto h = Hit();
    CHECK(filtered(nullptr, 5, from, dir, 100.0, skips, 1, &h) == SCO_OK);
    CHECK(f.calls == 1 && f.skip_seen == 1);
    CHECK(h.entity_id == 77 && h.distance == 12.5 && h.size == sizeof(sc_world_hit));

    // A skip count with no array is refused before the provider is called.
    CHECK(filtered(nullptr, 5, from, dir, 100.0, nullptr, 1, &h) == SCO_BAD_ARG);
    CHECK(f.calls == 1);

    // No hit is an answer, not a fault, and publishes no hit fields.
    f.answer = SCO_NOT_FOUND;
    auto miss = Hit();
    CHECK(filtered(nullptr, 5, from, dir, 100.0, nullptr, 0, &miss) == SCO_NOT_FOUND);
    CHECK(miss.entity_id == 0 && miss.flags == 0 && miss.size == sizeof(sc_world_hit));

    // A skip array invalidated during the call publishes nothing and latches the seam.
    f.answer = SCO_OK;
    f.move_generation = true;
    auto raced = Hit();
    CHECK(filtered(nullptr, 5, from, dir, 100.0, skips, 1, &raced) == SCO_UNAVAILABLE);
    CHECK(raced.entity_id == 0 && raced.size == sizeof(sc_world_hit));
    auto after = Hit();
    CHECK(filtered(nullptr, 5, from, dir, 100.0, nullptr, 0, &after) == SCO_UNAVAILABLE);
    const int calls_after_latch = f.calls;
    CHECK(filtered(nullptr, 5, from, dir, 100.0, nullptr, 0, &after) == SCO_UNAVAILABLE);
    CHECK(f.calls == calls_after_latch);

    std::printf("world ray engine: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
