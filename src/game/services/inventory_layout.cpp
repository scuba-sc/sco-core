// SPDX-License-Identifier: GPL-3.0-only
#include "inventory.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
#include <algorithm>
#include <array>
#include <cstring>
#if defined(_WIN32) && defined(_MSC_VER)
#include <windows.h>
#endif

namespace sco::game::services::inventory_layout {
namespace {
struct Frame {
    sc_inventory_layout_snapshot info{};
    std::array<sc_inventory_layout_entry, SC_INVENTORY_LAYOUT_MAX_PORTS> entries{};
};
InventoryLayoutBackend g_backend{};
bool g_started = false, g_busy = false, g_faulted = false, g_haveFrame = false;
uint64_t g_lifetime = 0, g_epochSequence = 0, g_publicEpoch = 0;
Frame g_previous{};
struct Scope {
    bool previous = g_busy;
    Scope() { g_busy = true; }
    ~Scope() { g_busy = previous; }
};
using Thunk = sco_result (*)(void*);
sco_result Cpp(Thunk thunk, void* ctx) {
    try { return thunk(ctx); } catch (...) { return SCO_CRASHED; }
}
sco_result Guard(Thunk thunk, void* ctx) {
#if defined(_WIN32) && defined(_MSC_VER)
    __try { return Cpp(thunk, ctx); } __except (EXCEPTION_EXECUTE_HANDLER) { return SCO_CRASHED; }
#else
    return Cpp(thunk, ctx);
#endif
}
template<class F> sco_result Call(F call) {
    Scope scope;
    const auto result = Guard([](void* ptr) { return (*static_cast<F*>(ptr))(); }, &call);
    if (result == SCO_CRASHED) {
        g_faulted = true;
        caps::Set(SC_INVENTORY_LAYOUT_CAPABILITY, false, "inventory layout fault; process restart required");
    }
    return result;
}
bool Installed() { return g_backend.enabled && g_backend.admitted && g_backend.snapshot && !g_faulted; }
bool Current(sco_plugin* owner, uint64_t lifetime) {
    return g_started && g_lifetime == lifetime && !detail::Released(owner);
}
sco_result Enter(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    return g_started && !g_busy && Installed() ? SCO_OK : SCO_UNAVAILABLE;
}
bool Range(uintptr_t start, size_t bytes, uintptr_t& end) {
    if (bytes > UINTPTR_MAX - start) return false;
    end = start + bytes;
    return true;
}
void Normalize(Frame& frame) {
    auto clearTail = [](char* text, size_t bytes) {
        const size_t length = std::strlen(text);
        std::memset(text + length, 0, bytes - length);
    };
    clearTail(frame.info.identity_domain, sizeof(frame.info.identity_domain));
    clearTail(frame.info.layout_key, sizeof(frame.info.layout_key));
    for (uint32_t i = 0; i < frame.info.count; ++i)
        clearTail(frame.entries[i].port_key, sizeof(frame.entries[i].port_key));
    frame.info.size = sizeof(frame.info);
    for (uint32_t i = 0; i < frame.info.count; ++i) frame.entries[i].size = sizeof(frame.entries[i]);
    std::fill(frame.entries.begin() + frame.info.count, frame.entries.end(), sc_inventory_layout_entry{});
}
}

Result Begin(const InventoryLayoutBackend& backend) {
    if (!OnGameThread()) return Result::WrongThread;
    if (g_lifetime == UINT64_MAX) return Result::TooMany;
    ++g_lifetime; g_started = true; g_backend = backend; g_haveFrame = false;
    // This capability records a configured host producer; each read still needs
    // a fresh admitted observation. The production backend never installs one.
    caps::Set(SC_INVENTORY_LAYOUT_CAPABILITY, Installed(),
        Installed() ? nullptr : "no enabled inventory layout producer");
    return Result::Ok;
}
void Stop() {
    g_started = false; g_backend = {}; g_haveFrame = false; g_previous = {};
    caps::Set(SC_INVENTORY_LAYOUT_CAPABILITY, false, "inventory layout stopped");
}
sco_result Snapshot(sco_plugin* self, sc_inventory_layout_snapshot* out,
                    sc_inventory_layout_entry* entries, uint32_t stride, uint32_t capacity) {
    const auto admitted = Enter(self);
    if (admitted != SCO_OK) return admitted;
    if (!out || (reinterpret_cast<uintptr_t>(out) & 7u) || out->size < sizeof(*out) ||
        !entries || !capacity || stride < sizeof(*entries) || (stride & 7u) ||
        (reinterpret_cast<uintptr_t>(entries) & 7u)) return SCO_BAD_ARG;
    if (capacity > SC_INVENTORY_LAYOUT_MAX_PORTS || stride > SC_INVENTORY_LAYOUT_MAX_STRIDE) return SCO_TOO_MANY;
    const uint32_t outSize = out->size;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(entries), info = reinterpret_cast<uintptr_t>(out);
    uintptr_t end = 0, infoEnd = 0;
    if (!Range(begin, static_cast<size_t>(capacity) * stride, end) || !Range(info, outSize, infoEnd) ||
        (begin < infoEnd && info < end)) return SCO_BAD_ARG;
    for (uint32_t i = 0; i < capacity; ++i) {
        const auto* row = reinterpret_cast<const sc_inventory_layout_entry*>(begin + static_cast<size_t>(i) * stride);
        if (row->size < sizeof(*row) || row->size > stride) return SCO_BAD_ARG;
    }
    const uint64_t lifetime = g_lifetime;
    auto backend = g_backend;
    auto result = Call([&] { return backend.admitted(backend.ctx) ? SCO_OK : SCO_UNAVAILABLE; });
    if (!Current(self, lifetime)) return detail::Released(self) ? SCO_BAD_ARG : SCO_UNAVAILABLE;
    if (result != SCO_OK) { g_haveFrame = false; return result; }
    Frame frame{}; frame.info.size = sizeof(frame.info);
    for (auto& row : frame.entries) row.size = sizeof(row);
    result = Call([&] { return backend.snapshot(backend.ctx, &frame.info, frame.entries.data(), SC_INVENTORY_LAYOUT_MAX_PORTS); });
    if (!Current(self, lifetime)) return detail::Released(self) ? SCO_BAD_ARG : SCO_UNAVAILABLE;
    if (result != SCO_OK) {
        g_haveFrame = false;
        return result == SCO_CRASHED || result == SCO_TOO_MANY ? result : SCO_UNAVAILABLE;
    }
    if (frame.info.size != sizeof(frame.info) ||
        !sc_inventory_layout_valid(&frame.info, frame.entries.data(), sizeof(frame.entries[0]), SC_INVENTORY_LAYOUT_MAX_PORTS)) {
        g_haveFrame = false; return SCO_UNAVAILABLE;
    }
    Normalize(frame);
    Frame canonical = frame;
    std::sort(canonical.entries.begin(), canonical.entries.begin() + canonical.info.count,
        [](const auto& a, const auto& b) { return std::strcmp(a.port_key, b.port_key) < 0; });
    const bool sameSource = g_haveFrame && canonical.info.source_epoch == g_previous.info.source_epoch;
    if (sameSource && (canonical.info.observation_generation < g_previous.info.observation_generation ||
        (canonical.info.observation_generation == g_previous.info.observation_generation &&
         std::memcmp(&canonical, &g_previous, sizeof(canonical))))) {
        g_haveFrame = false; return SCO_UNAVAILABLE;
    }
    if (!sameSource) {
        if (g_epochSequence == UINT64_MAX) return SCO_TOO_MANY;
        g_publicEpoch = ++g_epochSequence;
    }
    g_previous = canonical; g_haveFrame = true;
    frame.info.source_epoch = g_publicEpoch;
    const uint32_t count = std::min(frame.info.count, capacity);
    if (count < frame.info.count) {
        frame.info.flags = (frame.info.flags & ~SC_INVENTORY_LAYOUT_COMPLETE) | SC_INVENTORY_LAYOUT_TRUNCATED;
        frame.info.count = count;
    }
    // No backend call or allocation follows the first output write. Sizes and
    // unknown caller tails remain untouched for valid, disjoint caller buffers.
    std::memcpy(reinterpret_cast<unsigned char*>(out) + sizeof(out->size),
        reinterpret_cast<const unsigned char*>(&frame.info) + sizeof(frame.info.size), sizeof(frame.info) - sizeof(frame.info.size));
    for (uint32_t i = 0; i < count; ++i)
        std::memcpy(reinterpret_cast<unsigned char*>(entries) + static_cast<size_t>(i) * stride + sizeof(entries->size),
            reinterpret_cast<const unsigned char*>(&frame.entries[i]) + sizeof(frame.entries[i].size), sizeof(frame.entries[i]) - sizeof(frame.entries[i].size));
    return SCO_OK;
}
}
