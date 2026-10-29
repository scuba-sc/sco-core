// SPDX-License-Identifier: GPL-3.0-only
// K-40 native GameRules read seam: the install refuses without a grounded read-only accessor and a
// host-owned change counter, and an installed route copies only when the counter moved. Synthetic
// routes only; no game memory is read and nothing here runs in game.
#include <sc_gamerules.h>
#include "../src/game/services/gamerules_engine.h"
#include <cstdio>
#include <cstring>
using sco::Result;
namespace G = sco::game::services;

static int pass = 0, fail = 0;
#define CHECK(x) do { if (x) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

namespace {
// A synthetic route: no game memory, just counters the test owns.
struct Fixture {
    int module = 0;
    int calls = 0;
    uint64_t generation = 1;
    bool accessor_ok = true;
    uint32_t qt_allowed = 1;
};
bool Accessor(void* ctx, sc_gamerules_state* out) {
    auto* f = static_cast<Fixture*>(ctx);
    if (!f || !f->accessor_ok || !out) return false;
    ++f->calls;
    out->valid = 1;
    out->generation = f->generation;
    out->quantum_travel_allowed = f->qt_allowed;
    out->travel_target_threshold_angle = 42.5f;
    return true;
}
uint64_t Generation(void* ctx) {
    auto* f = static_cast<Fixture*>(ctx);
    return f ? f->generation : 0;
}
}  // namespace

int main() {
    // Nothing installed: no backend, no refusal to report yet.
    CHECK(G::NativeGameRulesBackend().read == nullptr);
    CHECK(std::strcmp(G::NativeGameRulesRefusal(), "") == 0);

    // Each missing piece refuses with its own reason, and leaves no backend behind.
    CHECK(G::InstallNativeGameRules({}) == Result::Unavailable);
    CHECK(std::strstr(G::NativeGameRulesRefusal(), "IGameRulesParamsModule") != nullptr);
    CHECK(G::NativeGameRulesBackend().read == nullptr);

    Fixture f;
    G::GameRulesNativeRoute route;
    route.params_module = &f.module;
    CHECK(G::InstallNativeGameRules(route) == Result::Unavailable);
    CHECK(std::strstr(G::NativeGameRulesRefusal(), "read-only accessor") != nullptr);

    route.read_only_accessor = &Accessor;
    CHECK(G::InstallNativeGameRules(route) == Result::Unavailable);
    CHECK(std::strstr(G::NativeGameRulesRefusal(), "change counter") != nullptr);
    CHECK(G::NativeGameRulesBackend().read == nullptr);

    // Complete route installs, and the refusal reason is cleared.
    route.generation = &Generation;
    route.ctx = &f;
    CHECK(G::InstallNativeGameRules(route) == Result::Ok);
    CHECK(std::strcmp(G::NativeGameRulesRefusal(), "") == 0);
    auto backend = G::NativeGameRulesBackend();
    CHECK(backend.read != nullptr);

    // A second install is refused rather than replacing the first.
    CHECK(G::InstallNativeGameRules(route) == Result::Failed);
    CHECK(std::strstr(G::NativeGameRulesRefusal(), "already installed") != nullptr);

    // First read copies; the second read at the same generation reuses the copy.
    sc_gamerules_state a{};
    a.size = sizeof(sc_gamerules_state);
    CHECK(backend.read(backend.ctx, &a));
    CHECK(f.calls == 1);
    CHECK(a.valid == 1 && a.quantum_travel_allowed == 1 && a.travel_target_threshold_angle == 42.5f);
    CHECK(a.generation == 1);
    // The seam owns size: a caller cannot make it report a layout this build does not have.
    CHECK(a.size == sizeof(sc_gamerules_state));

    sc_gamerules_state b{};
    b.size = sizeof(sc_gamerules_state);
    CHECK(backend.read(backend.ctx, &b));
    CHECK(f.calls == 1);  // no second accessor call: the counter did not move
    CHECK(std::memcmp(&a, &b, sizeof(a)) == 0);

    // A moved counter re-reads and reports the new value.
    f.generation = 2;
    f.qt_allowed = 0;
    sc_gamerules_state c{};
    c.size = sizeof(sc_gamerules_state);
    CHECK(backend.read(backend.ctx, &c));
    CHECK(f.calls == 2);
    CHECK(c.quantum_travel_allowed == 0 && c.generation == 2);

    // A zero counter means the host has no valid count: refuse instead of reusing a stale copy.
    f.generation = 0;
    sc_gamerules_state d{};
    d.size = sizeof(sc_gamerules_state);
    CHECK(!backend.read(backend.ctx, &d));
    CHECK(f.calls == 2);

    // A failing accessor reports failure and does not poison the cache: the next read succeeds.
    f.generation = 3;
    f.accessor_ok = false;
    sc_gamerules_state e{};
    e.size = sizeof(sc_gamerules_state);
    CHECK(!backend.read(backend.ctx, &e));
    f.accessor_ok = true;
    sc_gamerules_state g{};
    g.size = sizeof(sc_gamerules_state);
    CHECK(backend.read(backend.ctx, &g));
    CHECK(f.calls == 3 && g.generation == 3);

    // A null output is refused, never written through.
    CHECK(!backend.read(backend.ctx, nullptr));

    std::printf("gamerules engine: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
