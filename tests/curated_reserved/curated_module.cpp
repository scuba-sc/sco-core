// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/reserved_session.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
namespace rs = sco::reserved;
#if defined(_WIN32)
#define MODULE_EXPORT extern "C" __declspec(dllexport)
extern "C" __declspec(dllimport)
const rs::MockTable* reserved_host_mock_table() noexcept;
#else
#define MODULE_EXPORT extern "C" __attribute__((visibility("default")))
extern "C" __attribute__((visibility("default")))
const rs::MockTable* reserved_host_mock_table() noexcept;
#endif

namespace {
struct MoveGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool enabled = false;
    bool entered = false;
    bool released = false;
} g_moveGate; // fixed test fixture state; module remains loaded through process exit

const rs::MockTable* GetHostTable() noexcept { return reserved_host_mock_table(); }
void HoldMoveAfterRead() noexcept {
    std::unique_lock<std::mutex> lock(g_moveGate.mutex);
    if (!g_moveGate.enabled) return;
    g_moveGate.entered = true;
    g_moveGate.changed.notify_all();
    g_moveGate.changed.wait(lock, [] { return g_moveGate.released; });
    g_moveGate.enabled = false;
}
} // namespace

MODULE_EXPORT const char* curated_module_identity() noexcept {
    return "sco305-curated-module-v1";
}
MODULE_EXPORT bool curated_module_host_accessor_found() noexcept {
    return GetHostTable() != nullptr;
}
MODULE_EXPORT const rs::MockTable* curated_module_table() noexcept {
    return GetHostTable();
}
MODULE_EXPORT sco::Result curated_module_read(uint64_t id, rs::MockEntity* out) noexcept {
    const auto* table = GetHostTable();
    return table ? table->get_entity(table->context, id, out) : sco::Result::Unavailable;
}
MODULE_EXPORT sco::Result curated_module_move(uint64_t id, double x) noexcept {
    const auto* table = GetHostTable();
    if (!table) return sco::Result::Unavailable;
    rs::MockEntity entity{};
    const auto read = table->get_entity(table->context, id, &entity);
    if (read != sco::Result::Ok) return read;
    HoldMoveAfterRead(); // deterministic test-only pause between the two pinned table slots
    entity.position[0] = x;
    return table->put_entity(table->context, &entity);
}
MODULE_EXPORT sco::Result curated_module_enqueue_set(uint64_t value, uint64_t* ticket) noexcept {
    const auto* table = GetHostTable();
    return table ? table->enqueue_set_tracked(table->context, value, ticket) : sco::Result::Unavailable;
}
MODULE_EXPORT sco::Result curated_module_enqueue_add(uint64_t value, uint64_t* ticket) noexcept {
    const auto* table = GetHostTable();
    return table ? table->enqueue_add_tracked(table->context, value, ticket) : sco::Result::Unavailable;
}
MODULE_EXPORT void curated_module_arm_move_gate() noexcept {
    std::lock_guard<std::mutex> lock(g_moveGate.mutex);
    g_moveGate.enabled = true;
    g_moveGate.entered = false;
    g_moveGate.released = false;
}
MODULE_EXPORT bool curated_module_wait_move_gate(uint64_t timeoutMilliseconds) noexcept {
    std::unique_lock<std::mutex> lock(g_moveGate.mutex);
    return g_moveGate.changed.wait_for(lock, std::chrono::milliseconds(timeoutMilliseconds), [] { return g_moveGate.entered; });
}
MODULE_EXPORT void curated_module_release_move_gate() noexcept {
    std::lock_guard<std::mutex> lock(g_moveGate.mutex);
    g_moveGate.released = true;
    g_moveGate.changed.notify_all();
}
