// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/reserved_session.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
namespace rs = sco::reserved;
template<class T> static T Function(HMODULE module, const char* name) {
    FARPROC raw = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(raw));
    std::memcpy(&result, &raw, sizeof result);
    return result;
}
int main(int argc, char** argv) {
    CHECK(argc == 3);
    HMODULE first = LoadLibraryA(argv[1]), second = LoadLibraryA(argv[2]);
    CHECK(first && second);
    rs::CoreBinding binding; rs::Session session; rs::StartupControl control;
    const auto* bootstrap = rs::CreateExecutableAuthority(); CHECK(bootstrap);
    CHECK(rs::BindCoreInstance(bootstrap, binding) == sco::Result::Ok);
    using ReserveGraph = bool (*)(rs::CoreBinding*, rs::Session*);
    auto reserveGraph = Function<ReserveGraph>(first, "module_reserve_graph");
    CHECK(reserveGraph && reserveGraph(&binding, &session));
    int hostOwner = 0, hostTable = 0;
    CHECK(sco::ProvideService(&hostOwner, nullptr, "fixture.host", 1, &hostTable) == sco::Result::Unavailable);
    CHECK(rs::BeginStartup(session, control) == sco::Result::Ok);
    CHECK(rs::PublishTypedProvider(control) == sco::Result::Ok);
    CHECK(rs::FinishStartup(control) == sco::Result::Ok);
    const auto* table = rs::QueryMock(session); CHECK(table);
    using Call = bool (*)(rs::Session*, const rs::MockTable*, int64_t);
    using Cached = bool (*)(const rs::MockTable*);
    using Move = bool (*)(rs::Session*, const rs::MockTable*, uint64_t, double);
    auto call1 = Function<Call>(first, "module_call");
    auto call2 = Function<Call>(second, "module_call");
    auto cached1 = Function<Cached>(first, "module_cached_refuses");
    auto cached2 = Function<Cached>(second, "module_cached_refuses");
    auto move1 = Function<Move>(first, "module_move_entity");
    auto move2 = Function<Move>(second, "module_move_entity");
    using Identity = decltype(&rs::TryEnter) (*)();
    auto identity1 = Function<Identity>(first, "module_identity");
    auto identity2 = Function<Identity>(second, "module_identity");
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
    // Kept loaded to satisfy this controlled composition's process lifetime.
}
