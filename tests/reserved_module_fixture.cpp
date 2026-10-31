// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/reserved_session.h"
#include <cstdint>
#include <memory>
namespace rs = sco::reserved;
#ifdef _WIN32
#define SCO_TEST_EXPORT __declspec(dllexport)
#else
#define SCO_TEST_EXPORT
#endif
extern "C" {
#ifdef SCO_DUPLICATE_CORE_FIXTURE
SCO_TEST_EXPORT const void* duplicate_core_address() { return reinterpret_cast<const void*>(&rs::CreateExecutableAuthority); }
SCO_TEST_EXPORT const rs::Bootstrap* duplicate_authority() { return rs::CreateExecutableAuthority(); }
SCO_TEST_EXPORT bool duplicate_bind(const rs::Bootstrap* bootstrap) {
    rs::CoreBinding binding;
    return rs::BindCoreInstance(bootstrap, binding) == sco::Result::Ok;
}
#else
SCO_TEST_EXPORT decltype(&rs::TryEnter) module_identity() { return &rs::TryEnter; }
SCO_TEST_EXPORT bool module_reserve_graph(rs::CoreBinding* binding, rs::Session* session) {
    try {
        auto seed = std::make_unique<rs::MockGraph>();
        seed->zoneCount = 1; seed->zones[0].id = 1; seed->zones[0].name[0] = 'z';
        seed->entityCount = 1; seed->entities[0].id = 1; seed->entities[0].zone = 1;
        seed->entities[0].name[0] = 'e'; seed->entities[0].position[0] = 5;
        if (rs::ReserveSession(*binding, *seed, *session) != sco::Result::Ok) return false;
        seed->entities[0].position[0] = 999; // the retained graph is a copy
        return true;
    } catch (...) { return false; }
}
SCO_TEST_EXPORT bool module_call(rs::Session* session, const rs::MockTable* table, int64_t value) {
    rs::CallPin pin;
    if (rs::TryEnter(*session, pin) != sco::Result::Ok) return false;
    if (table->set(table->context, value) != sco::Result::Ok) return false;
    int64_t out = 0;
    return table->get(table->context, &out) == sco::Result::Ok && out == value;
}
SCO_TEST_EXPORT bool module_cached_refuses(const rs::MockTable* table) {
    int64_t out = -17;
    return table->get(table->context, &out) == sco::Result::Unavailable && out == -17;
}
SCO_TEST_EXPORT bool module_move_entity(rs::Session* session, const rs::MockTable* table, uint64_t id, double x) {
    rs::CallPin pin;
    if (rs::TryEnter(*session, pin) != sco::Result::Ok) return false;
    rs::MockEntity value{};
    if (table->get_entity(table->context, id, &value) != sco::Result::Ok) return false;
    value.position[0] = x;
    return table->put_entity(table->context, &value) == sco::Result::Ok;
}
#endif
}
