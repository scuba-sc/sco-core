// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "datacore_live_engine.h"

#include "sco/datacore.h"
#include "sco/hook.h"
#include "sco/signatures.h"
#include "sco/game/pak.h"
#include "sco/game/host.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <vector>
#if defined(_WIN32) && defined(_MSC_VER)
#define NOMINMAX
#include <windows.h>
#endif

namespace sco::game::services::datacore_live {
namespace {

constexpr uint64_t kRecordBytes = 0x24;
constexpr uint32_t kMaxDefinitions = 1u << 20;
constexpr uint32_t kMaxRecords = 1u << 20;
constexpr uint32_t kMaxParentDepth = 1u << 20;

struct Binding {
    EngineRows rows{};
    bool configured = false;
};
Binding g_binding;
std::atomic<bool> g_faulted{false};
std::atomic<bool> g_retired{false};
using LifecycleFn=void(*)(uintptr_t);
LifecycleFn g_resetOriginal=nullptr,g_destructorOriginal=nullptr;
using DeletingFn=uintptr_t(*)(uintptr_t,uint32_t);
DeletingFn g_deletingOriginal=nullptr;
bool g_prepared=false;
void ResetHook(uintptr_t owner) {
    LifecycleGate::ExclusiveLease lease;
    sco::game::datacore_live::ProcessLifecycleGate().EnterLifecycleOrTerminate(lease);
    g_retired.store(true,std::memory_order_release);
    g_resetOriginal(owner);
}
void DestructorHook(uintptr_t owner) {
    LifecycleGate::ExclusiveLease lease;
    sco::game::datacore_live::ProcessLifecycleGate().EnterLifecycleOrTerminate(lease);
    g_retired.store(true,std::memory_order_release);
    g_destructorOriginal(owner);
}
uintptr_t DeletingHook(uintptr_t owner,uint32_t flags) {
    LifecycleGate::ExclusiveLease lease;
    sco::game::datacore_live::ProcessLifecycleGate().EnterLifecycleOrTerminate(lease);
    g_retired.store(true,std::memory_order_release);
    return g_deletingOriginal(owner,flags);
}
#if defined(_WIN32) && defined(_MSC_VER)
int NativeFaultFilter() {
    g_faulted.store(true,std::memory_order_release);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

struct Pool {
    uintptr_t data;
    uint64_t count;
    uint32_t width;
};

struct Snapshot {
    uintptr_t owner;
    uintptr_t header;
    uintptr_t structs;
    uintptr_t properties;
    uintptr_t records;
    uint32_t structCount;
    uint32_t propertyCount;
    uint32_t recordCount;
    Pool pools[13];
};

bool AddFits(uint64_t a, uint64_t b, uint64_t limit) noexcept {
    return a <= limit && b <= limit - a;
}

bool MulFits(uint64_t a, uint64_t b, uint64_t limit) noexcept {
    return !b || a <= limit / b;
}

bool Plausible(uintptr_t p) noexcept {
    return p >= 0x10000 && p < 0x0000800000000000ull && !(p & 7);
}

template<class T>
T Load(uintptr_t p) {
    return *reinterpret_cast<const volatile T*>(p);
}

#if defined(_MSC_VER)
__declspec(noinline)
#endif
bool ReadSnapshotUnsafe(const EngineRows* rows, Snapshot* out) {
    if (!rows || !out || !rows->ownerSlot || !Plausible(rows->ownerSlot) ||
        !Plausible(rows->ownerVtable) || !Plausible(rows->reset) ||
        !Plausible(rows->deletingDestructor) || !rows->gate || !rows->admitted)
        return false;

    const uintptr_t owner = Load<uintptr_t>(rows->ownerSlot);
    if (!Plausible(owner) || Load<uintptr_t>(owner) != rows->ownerVtable)
        return false;
    const uintptr_t vt = Load<uintptr_t>(owner);
    if (Load<uintptr_t>(vt + 8) != rows->reset ||
        Load<uintptr_t>(vt + 0x20) != rows->deletingDestructor)
        return false;

    if (Load<uint32_t>(owner + 0x1c8) != 2) return false;
    const uintptr_t header = Load<uintptr_t>(owner + 0x1d0);
    const uintptr_t structs = Load<uintptr_t>(owner + 0x1d8);
    const uintptr_t properties = Load<uintptr_t>(owner + 0x1e0);
    const uintptr_t records = Load<uintptr_t>(owner + 0x1f0);
    if (!Plausible(header))
        return false;

    Snapshot s{};
    s.owner = owner;
    s.header = header;
    s.structs = structs;
    s.properties = properties;
    s.records = records;
    s.structCount = Load<uint32_t>(header + 0x10);
    s.propertyCount = Load<uint32_t>(header + 0x14);
    s.recordCount = Load<uint32_t>(header + 0x20);
    if ((s.structCount && !Plausible(structs)) ||
        (s.propertyCount && !Plausible(properties)) ||
        (s.recordCount && !Plausible(records)) ||
        s.structCount > kMaxDefinitions || s.propertyCount > kMaxDefinitions ||
        s.recordCount > kMaxRecords ||
        !MulFits(s.structCount, 16, std::numeric_limits<uintptr_t>::max() - structs) ||
        !MulFits(s.propertyCount, 12, std::numeric_limits<uintptr_t>::max() - properties) ||
        !MulFits(s.recordCount, kRecordBytes, std::numeric_limits<uintptr_t>::max() - records))
        return false;

    // Native serialized type codes and their owner buffers:
    // 1 bool, 2..5 signed integers, 6..9 unsigned integers,
    // 11 float, 12 double, 14 GUID. Type 10 is the owner text blob.
    const uintptr_t data[13] = {
        Load<uintptr_t>(owner + 0x200), Load<uintptr_t>(owner + 0x208),
        Load<uintptr_t>(owner + 0x210), Load<uintptr_t>(owner + 0x218),
        Load<uintptr_t>(owner + 0x220), Load<uintptr_t>(owner + 0x228),
        Load<uintptr_t>(owner + 0x230), Load<uintptr_t>(owner + 0x238),
        Load<uintptr_t>(owner + 0x240), Load<uintptr_t>(owner + 0x248),
        Load<uintptr_t>(owner + 0x250), Load<uintptr_t>(owner + 0x258),
        Load<uintptr_t>(owner + 0x278),
    };
    const uint32_t headerOffset[13] = {
        0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c,
        0x40, 0x44, 0x48, 0x4c, 0x50, 0x70,
    };
    const uint32_t width[13] = {1, 1, 2, 4, 8, 1, 2, 4, 8, 4, 8, 16, 1};
    for (uint32_t i = 0; i < 13; ++i) {
        const uint32_t count = Load<uint32_t>(header + headerOffset[i]);
        s.pools[i] = {data[i], count, width[i]};
        if (count && !Plausible(data[i])) return false;
        if (!MulFits(count, width[i], std::numeric_limits<uintptr_t>::max() - data[i]))
            return false;
    }

    *out = s;
    return true;
}

bool SnapshotFor(const EngineRows* rows, LifecycleGate::ReadLease* lease,
                 Snapshot* out) noexcept {
    if (!rows || !rows->gate || !lease || !*lease || g_faulted.load(std::memory_order_acquire))
        return false;
#if defined(_WIN32) && defined(_MSC_VER)
    volatile bool valid=false;
    __try {
        valid=ReadSnapshotUnsafe(rows, out);
    } __except (NativeFaultFilter()) {
        g_faulted.store(true, std::memory_order_release);
        valid=false;
    }
    return valid;
#else
    return ReadSnapshotUnsafe(rows, out);
#endif
}

bool CurrentSnapshot(Snapshot* out, LifecycleGate::ReadLease* lease) noexcept {
    if (!g_binding.configured || !g_binding.rows.admitted() ||
        !g_binding.rows.gate->EnterRead(*lease)) return false;
    if(g_prepared && !sco::game::pak::Enabled())return false;
    if(g_retired.load(std::memory_order_acquire))return false;
    return SnapshotFor(&g_binding.rows, lease, out);
}

sco_result ReadStats(uint32_t type, sc_datacore_pool_stats* out) noexcept {
    if (!out || out->size < sizeof(*out) || type >= SC_DC_POOL_COUNT) return SCO_BAD_ARG;
    LifecycleGate::ReadLease lease;
    Snapshot s{};
    if (!CurrentSnapshot(&s, &lease)) return g_faulted.load() ? SCO_FAILED : SCO_UNAVAILABLE;

    const uint32_t i = type;
    sc_datacore_pool_stats staged{};
    staged.size = sizeof(staged);
    staged.pool_type = type;
    staged.element_count = s.pools[i].count;
    // No growth-capacity field is exposed by the native owner. Report only
    // the validated current extent; do not infer spare allocation capacity.
    staged.capacity = s.pools[i].count;
    staged.element_size = s.pools[i].width;
    if (!MulFits(staged.capacity, staged.element_size, UINT64_MAX)) return SCO_FAILED;
    staged.total_bytes = staged.capacity * staged.element_size;
    *out = staged;
    return SCO_OK;
}

bool CopyPoolBytes(uintptr_t source, void* out, size_t bytes) noexcept {
#if defined(_WIN32) && defined(_MSC_VER)
    volatile bool copied=false;
    __try {
        const volatile uint8_t* input=reinterpret_cast<const volatile uint8_t*>(source);
        auto* target=static_cast<uint8_t*>(out);
        for (size_t i=0;i<bytes;++i) target[i]=input[i];
        copied=true;
    }
    __except (NativeFaultFilter()) {
        g_faulted.store(true, std::memory_order_release);
        copied=false;
    }
    return copied;
#else
    std::memcpy(out, reinterpret_cast<const void*>(source), bytes);
    return true;
#endif
}

sco_result ReadPool(uint32_t type, uint64_t offset, void* output, size_t bytes,
                    size_t* count) noexcept {
    if (!output || !bytes || !count || type >= SC_DC_POOL_COUNT) return SCO_BAD_ARG;
    *count = 0;
    LifecycleGate::ReadLease lease;
    Snapshot s{};
    if (!CurrentSnapshot(&s, &lease)) return g_faulted.load() ? SCO_FAILED : SCO_UNAVAILABLE;

    const Pool& pool = s.pools[type];
    uint64_t extent = 0;
    if (!MulFits(pool.count, pool.width, UINT64_MAX)) return SCO_FAILED;
    extent = pool.count * pool.width;
    if (offset > extent || bytes > extent - offset) return SCO_BAD_ARG;

    std::vector<uint8_t> staged;
    try { staged.resize(bytes); } catch (...) { return SCO_FAILED; }
    if (!CopyPoolBytes(pool.data + offset, staged.data(), bytes)) return SCO_FAILED;
    std::memcpy(output, staged.data(), bytes);
    *count = bytes;
    return SCO_OK;
}

bool FieldCount(const Snapshot& s, uint32_t structIndex, uint32_t* result) {
    if (!result || structIndex >= s.structCount) return false;
    uint64_t total = 0;
    uint32_t current = structIndex;
    for (uint32_t depth = 0; depth <= s.structCount && depth < kMaxParentDepth; ++depth) {
        const uintptr_t def = s.structs + static_cast<uint64_t>(current) * 16;
        const uint32_t rawParent = Load<uint32_t>(def + 4);
        const int32_t parent = static_cast<int32_t>(rawParent);
        const uint16_t ownCount = Load<uint16_t>(def + 8);
        const uint16_t first = Load<uint16_t>(def + 0xa);
        if (static_cast<uint64_t>(first) + ownCount > s.propertyCount ||
            !AddFits(total, ownCount, UINT32_MAX))
            return false;
        total += ownCount;
        if (parent == -1) {
            *result = static_cast<uint32_t>(total);
            return true;
        }
        if (parent < 0 || static_cast<uint32_t>(parent) >= s.structCount)
            return false;
        current = static_cast<uint32_t>(parent);
    }
    return false;
}

#if defined(_MSC_VER)
__declspec(noinline)
#endif
sco_result FindRecordUnsafe(const Snapshot& s, const uint8_t guid[16], sc_datacore_live_record_info* out) {

    uint32_t hit = UINT32_MAX;
    for (uint32_t i = 0; i < s.recordCount; ++i) {
        const uintptr_t record = s.records + static_cast<uint64_t>(i) * kRecordBytes;
        bool matches=true;
        for (size_t b=0;b<16;++b) if (Load<uint8_t>(record+0x10+b)!=guid[b]) matches=false;
        if (matches) {
            if (hit != UINT32_MAX) return SCO_FAILED;
            hit = i;
        }
    }
    if (hit == UINT32_MAX) return SCO_NOT_FOUND;

    const uintptr_t record = s.records + static_cast<uint64_t>(hit) * kRecordBytes;
    const uint32_t structIndex = Load<uint32_t>(record + 0x0c);
    uint32_t fields = 0;
    if (!FieldCount(s, structIndex, &fields)) return SCO_FAILED;

    sc_datacore_live_record_info staged{};
    staged.size = sizeof(staged);
    std::memcpy(staged.guid, guid, sizeof(staged.guid));
    staged.struct_type = structIndex;
    staged.field_count = fields;
    staged.pool_offset = static_cast<uint64_t>(hit) * kRecordBytes;
    *out = staged;
    return SCO_OK;
}

sco_result FindRecordGuarded(const Snapshot& s, const uint8_t guid[16], sc_datacore_live_record_info* out) noexcept {
#if defined(_WIN32) && defined(_MSC_VER)
    volatile sco_result result=SCO_FAILED;
    __try { result=FindRecordUnsafe(s, guid, out); }
    __except (NativeFaultFilter()) {
        g_faulted.store(true, std::memory_order_release);
        result=SCO_FAILED;
    }
    return result;
#else
    return FindRecordUnsafe(s, guid, out);
#endif
}

sco_result FindRecord(const uint8_t guid[16], sc_datacore_live_record_info* out) noexcept {
    if (!guid || !out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    LifecycleGate::ReadLease lease;
    Snapshot s{};
    if (!CurrentSnapshot(&s, &lease)) return g_faulted.load() ? SCO_FAILED : SCO_UNAVAILABLE;
    return FindRecordGuarded(s, guid, out);
}

bool Offline() noexcept { return g_binding.configured && g_binding.rows.admitted(); }
bool Ready() noexcept {
    if (g_faulted.load(std::memory_order_acquire)) return false;
    LifecycleGate::ReadLease lease;
    Snapshot snapshot{};
    const bool ready=CurrentSnapshot(&snapshot, &lease);
    return ready && !g_faulted.load(std::memory_order_acquire);
}
sco_result StatsCallback(uint32_t type, sc_datacore_pool_stats* out) { return ReadStats(type, out); }
sco_result FindCallback(const uint8_t guid[16], sc_datacore_live_record_info* out) {
    return FindRecord(guid, out);
}
sco_result ReadCallback(uint32_t type, uint64_t offset, void* out, size_t bytes, size_t* count) {
    return ReadPool(type, offset, out, bytes, count);
}
sco_result WriteCallback(uint32_t, uint64_t, const void*, size_t, size_t* count) {
    if (count) *count = 0;
    return SCO_UNAVAILABLE;
}

} // namespace

bool ConfigureEngine(const EngineRows& rows) noexcept {
    if (g_binding.configured || g_faulted.load() || !rows.gate || !rows.admitted || !Plausible(rows.ownerSlot) ||
        !Plausible(rows.ownerVtable) || !Plausible(rows.reset) ||
        !Plausible(rows.deletingDestructor))
        return false;
    g_binding.rows = rows;
    g_binding.configured = true;
    return true;
}

void ClearEngine() noexcept {
    g_binding = {};
}

Backend EngineBackend() noexcept {
    if (!g_binding.configured) return {};
    return {true, Offline, Ready, StatsCallback, FindCallback, ReadCallback, WriteCallback};
}

bool PrepareEngine(const EngineRows& rows,const LifecycleTargets& targets,bool quiesced) noexcept {
    if(!quiesced || g_prepared || g_binding.configured || g_faulted.load() ||
       !targets.reset || !targets.destructor || !targets.deleting || targets.reset==targets.destructor ||
       targets.deleting==targets.reset || targets.deleting==targets.destructor ||
       !rows.gate || rows.gate!=&sco::game::datacore_live::ProcessLifecycleGate() ||
       !rows.admitted || !sco::game::pak::Enabled()) return false;
    const size_t resetBytes=sco::hook::StolenLength(targets.reset,14);
    const size_t destructorBytes=sco::hook::StolenLength(targets.destructor,14);
    const size_t deletingBytes=sco::hook::StolenLength(targets.deleting,14);
    if(!resetBytes || !destructorBytes || !deletingBytes || sco::hook::IsHooked(targets.reset) || sco::hook::IsHooked(targets.destructor) || sco::hook::IsHooked(targets.deleting)) return false;
    sco::hook::Transaction hooks;
    hooks.Add(targets.reset,resetBytes,reinterpret_cast<void*>(&ResetHook),reinterpret_cast<void**>(&g_resetOriginal));
    hooks.Add(targets.destructor,destructorBytes,reinterpret_cast<void*>(&DestructorHook),reinterpret_cast<void**>(&g_destructorOriginal));
    hooks.Add(targets.deleting,deletingBytes,reinterpret_cast<void*>(&DeletingHook),reinterpret_cast<void**>(&g_deletingOriginal));
    if(hooks.Commit()!=sco::hook::Error::None) {g_faulted.store(true);return false;}
    if(!ConfigureEngine(rows)) {
        if(hooks.Rollback()!=sco::hook::Error::None) std::terminate();
        g_faulted.store(true);return false;
    }
    g_prepared=true;
    return true;
}
bool EnginePrepared() noexcept {return g_prepared && !g_faulted.load();}
bool PrepareProductionEngine(bool quiesced) noexcept {
    if(!quiesced) return false;
    const char* names[]={"datacore_live.owner_slot","datacore_live.owner_vtable","datacore_live.reset","datacore_live.deleting","datacore_live.destructor","pak.datacore_loader"};
    for(const auto* name:names)if(!SigReady(name))return false;
    EngineRows rows{reinterpret_cast<uintptr_t>(Sig(names[0])),reinterpret_cast<uintptr_t>(Sig(names[1])),
        reinterpret_cast<uintptr_t>(Sig(names[2])),reinterpret_cast<uintptr_t>(Sig(names[3])),
        &sco::game::datacore_live::ProcessLifecycleGate(),sco::game::host::FreshAdmissionAllowed};
    return PrepareEngine(rows,{Sig(names[2]),Sig(names[4]),Sig(names[3])},true);
}

} // namespace sco::game::services::datacore_live
