// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/reserved_session.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#define CHECK(x) do { if (!(x)) { const char* error = dlerror(); std::fprintf(stderr, "CHECK failed: %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error ? error : "no loader error"); std::abort(); } } while (0)
namespace rs = sco::reserved;
int main(int argc, char** argv) {
    CHECK(argc == 7);
    void* modules[6];
    for (int i = 0; i < 6; ++i) { modules[i] = dlopen(argv[i+1], RTLD_NOW | RTLD_LOCAL); CHECK(modules[i]); }
    using Address = const void* (*)();
    using Authority = const rs::Bootstrap* (*)();
    using Bind = bool (*)(const rs::Bootstrap*);
    auto address1 = reinterpret_cast<Address>(dlsym(modules[2], "duplicate_core_address"));
    auto address2 = reinterpret_cast<Address>(dlsym(modules[3], "duplicate_core_address"));
    auto authority1 = reinterpret_cast<Authority>(dlsym(modules[2], "duplicate_authority"));
    auto authority2 = reinterpret_cast<Authority>(dlsym(modules[3], "duplicate_authority"));
    auto bind1 = reinterpret_cast<Bind>(dlsym(modules[2], "duplicate_bind"));
    auto bind2 = reinterpret_cast<Bind>(dlsym(modules[3], "duplicate_bind"));
    CHECK(address1 && address2 && authority1 && authority2 && bind1 && bind2);
    CHECK(address1() != address2()); // distinct broker code copies, not a runtime-state census
    CHECK(!authority1() && !authority2()); // neither is executable-owned
    using RuntimeCall = int (*)();
    auto provide1 = reinterpret_cast<RuntimeCall>(dlsym(modules[4], "duplicate_runtime_provide"));
    auto provide2 = reinterpret_cast<RuntimeCall>(dlsym(modules[5], "duplicate_runtime_provide"));
    auto query1 = reinterpret_cast<RuntimeCall>(dlsym(modules[4], "duplicate_runtime_query"));
    auto query2 = reinterpret_cast<RuntimeCall>(dlsym(modules[5], "duplicate_runtime_query"));
    CHECK(provide1 && provide2 && query1 && query2);
    CHECK(query1() == static_cast<int>(sco::Result::NotFound));
    CHECK(provide1() == static_cast<int>(sco::Result::Ok));
    CHECK(query1() == static_cast<int>(sco::Result::Ok));
    CHECK(query2() == static_cast<int>(sco::Result::NotFound));
    rs::CoreBinding binding; rs::Session session; rs::StartupControl control;
    const auto* bootstrap = rs::CreateExecutableAuthority(); CHECK(bootstrap);
    CHECK(!bind1(bootstrap) && !bind2(bootstrap));
    CHECK(rs::BindCoreInstance(bootstrap, binding) == sco::Result::Ok);
    using ReserveGraph = bool (*)(rs::CoreBinding*, rs::Session*);
    auto reserveGraph = reinterpret_cast<ReserveGraph>(dlsym(modules[0], "module_reserve_graph"));
    CHECK(reserveGraph && reserveGraph(&binding, &session));
    int hostOwner = 0, hostTable = 0;
    CHECK(sco::ProvideService(&hostOwner, nullptr, "fixture.host", 1, &hostTable) == sco::Result::Unavailable);
    // An unmanaged embedded runtime is outside the supported composition and
    // does not participate in the EXE's gate; show the bypass explicitly.
    CHECK(provide2() == static_cast<int>(sco::Result::Ok));
    CHECK(query2() == static_cast<int>(sco::Result::Ok));
    CHECK(rs::BeginStartup(session, control) == sco::Result::Ok);
    CHECK(rs::PublishTypedProvider(control) == sco::Result::Ok);
    CHECK(rs::FinishStartup(control) == sco::Result::Ok);
    const auto* table = rs::QueryMock(session); CHECK(table);
    using Call = bool (*)(rs::Session*, const rs::MockTable*, int64_t);
    using Cached = bool (*)(const rs::MockTable*);
    using Move = bool (*)(rs::Session*, const rs::MockTable*, uint64_t, double);
    auto call1 = reinterpret_cast<Call>(dlsym(modules[0], "module_call"));
    auto call2 = reinterpret_cast<Call>(dlsym(modules[1], "module_call"));
    auto cached1 = reinterpret_cast<Cached>(dlsym(modules[0], "module_cached_refuses"));
    auto cached2 = reinterpret_cast<Cached>(dlsym(modules[1], "module_cached_refuses"));
    auto move1 = reinterpret_cast<Move>(dlsym(modules[0], "module_move_entity"));
    auto move2 = reinterpret_cast<Move>(dlsym(modules[1], "module_move_entity"));
    using Identity = decltype(&rs::TryEnter) (*)();
    auto identity1 = reinterpret_cast<Identity>(dlsym(modules[0], "module_identity"));
    auto identity2 = reinterpret_cast<Identity>(dlsym(modules[1], "module_identity"));
    int64_t value = 0;
    CHECK(call1 && call2 && cached1 && cached2 && move1 && move2 && identity1 && identity2);
    CHECK(identity1() == &rs::TryEnter && identity2() == &rs::TryEnter);
    CHECK(call1(&session, table, 37));
    CHECK(table->get(table->context, &value) == sco::Result::Ok && value == 37);
    CHECK(call2(&session, table, 42));
    CHECK(table->get(table->context, &value) == sco::Result::Ok && value == 42);
    rs::MockEntity entity{};
    CHECK(table->get_entity(table->context, 1, &entity) == sco::Result::Ok && entity.position[0] == 5);
    CHECK(move1(&session, table, 1, 37.0) && move2(&session, table, 1, 42.0));
    CHECK(table->get_entity(table->context, 1, &entity) == sco::Result::Ok && entity.position[0] == 42.0);
    CHECK(rs::Close(session) == sco::Result::Ok);
    CHECK(!call1(&session, table, 99) && !call2(&session, table, 99));
    CHECK(!move1(&session, table, 1, 99.0) && !move2(&session, table, 1, 99.0));
    CHECK(cached1(table) && cached2(table));
    CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + std::chrono::seconds(2)) == rs::RetireResult::QuiescedRetained);
    // Modules are process-pinned by this fixture, matching its supported composition.
}
