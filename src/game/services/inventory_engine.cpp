#include "inventory_engine.h"
#include "../inventory_map.h"
#include <windows.h>

namespace sco::game::inventory {

namespace {

namespace map = sco::game::inventory_map;

constexpr uintptr_t kPtrMask = 0xFFFFFFFFFFFFull;   // handle -> pointer, as the game masks it
constexpr uintptr_t kMinPtr = 0x10000;              // below this is never a game object

template <typename T> T Rd(uintptr_t p) { return *reinterpret_cast<const T*>(p); }

template <typename R, typename... A> R VCall(uintptr_t obj, size_t off, A... a) {
    using Fn = R(__fastcall*)(uintptr_t, A...);
    return reinterpret_cast<Fn>(Rd<uintptr_t>(Rd<uintptr_t>(obj) + off))(obj, a...);
}

bool PlausiblePtr(uintptr_t p) { return p >= kMinPtr && p <= kPtrMask && (p & 7) == 0; }

// The engine's view while one snapshot runs. The Access callbacks reach it through ctx.
struct Ctx {
    const EngineRows* rows;
};

using HandleValidFn = bool(__fastcall*)(const uint64_t* handle);

bool HandleLive(const Ctx& c, uint64_t handle) {
    return reinterpret_cast<HandleValidFn>(c.rows->handleValid)(&handle);
}

// A container component pointer from a handle, or 0. The container's vtable must hold the row's
// GetItemPorts where the game keeps it: the receiver check that this is an item-port container.
uintptr_t CheckedContainer(const Ctx& c, uintptr_t container) {
    if (!PlausiblePtr(container)) return 0;
    const uintptr_t vt = Rd<uintptr_t>(container);
    if (!PlausiblePtr(vt)) return 0;
    return Rd<uintptr_t>(vt + map::kContainerGetPorts) == c.rows->getItemPorts ? container : 0;
}

// The port range of a container: begin/end of the game's own vector, read in place. No call, no
// allocation. Bounds: ordered, 8-aligned, at most kMaxPortsPerContainer, first and last readable.
bool ContainerPorts(void* ctx, uintptr_t container, uintptr_t* ports, uint32_t* count) {
    const Ctx& c = *static_cast<const Ctx*>(ctx);
    __try {
        if (!CheckedContainer(c, container)) return false;
        const uintptr_t begin = Rd<uintptr_t>(container + map::kContainerPortsBegin);
        const uintptr_t end = Rd<uintptr_t>(container + map::kContainerPortsEnd);
        if (begin == 0 && end == 0) { *ports = 0; *count = 0; return true; }
        if (!PlausiblePtr(begin) || !PlausiblePtr(end) || begin > end) return false;
        const uintptr_t n = (end - begin) >> 3;
        if (n > map::kMaxPortsPerContainer || (end - begin) != (n << 3)) return false;
        if (n) { (void)Rd<uintptr_t>(begin); (void)Rd<uintptr_t>(begin + 8 * (n - 1)); }
        *ports = begin;
        *count = static_cast<uint32_t>(n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// One port: its vtable must hold the rows' accessors where the adapter calls them; then valid?,
// the item handle, the item's entity id and item type id, and the container attached to it.
bool ReadPort(void* ctx, uintptr_t ports, uint32_t index, PortInfo* out) {
    const Ctx& c = *static_cast<const Ctx*>(ctx);
    const EngineRows& rows = *c.rows;
    *out = {};
    __try {
        const uintptr_t port = Rd<uintptr_t>(ports + 8 * static_cast<uintptr_t>(index));
        if (!PlausiblePtr(port)) return false;
        const uintptr_t vt = Rd<uintptr_t>(port);
        if (!PlausiblePtr(vt)) return false;
        if (Rd<uintptr_t>(vt + map::kPortGetItem) != rows.portGetItem || Rd<uintptr_t>(vt + map::kPortGetContainer) != rows.portGetContainer ||
            Rd<uintptr_t>(vt + map::kPortIsValid) != rows.portIsValid || Rd<uintptr_t>(vt + map::kPortItemHandle) != rows.portItemHandle)
            return false;
        if (!VCall<bool>(port, map::kPortIsValid)) return true;   // an empty port: valid = 0

        uint64_t tmp = 0;
        const uint64_t* hp = VCall<const uint64_t*>(port, map::kPortItemHandle, &tmp);
        if (!hp) return false;
        const uint64_t handle = *hp;
        if (!HandleLive(c, handle)) return false;
        const uintptr_t entity = handle & kPtrMask;
        if (!PlausiblePtr(entity)) return false;
        uint8_t idTmp[16] = {};
        const uint64_t* id = VCall<const uint64_t*>(entity, map::kEntityId, idTmp);
        if (!id || !*id) return false;
        const uintptr_t item = VCall<uintptr_t>(port, map::kPortGetItem);
        if (!PlausiblePtr(item)) return false;
        const uintptr_t desc = VCall<uintptr_t>(item, map::kItemDescriptor);
        if (!PlausiblePtr(desc)) return false;
        out->itemType = Rd<uint32_t>(desc + map::kDescItemType);
        out->entityId = *id;
        out->valid = true;
        const uintptr_t inner = VCall<uintptr_t>(port, map::kPortGetContainer) & kPtrMask;
        out->inner = inner;   // 0 when the item has no container; checked when the walk enters it
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = {};
        return false;
    }
}

// The player's container: type id cache -> component by type -> live handle -> checked container.
// Plain POD locals so the SEH frame stays legal.
ReadStatus FindContainer(const Ctx& c, uintptr_t entity, uintptr_t* container, const char** why) {
    const EngineRows& rows = *c.rows;
    __try {
        if (!PlausiblePtr(entity)) { *why = "no player entity"; return ReadStatus::Guard; }
        uint16_t type = Rd<uint16_t>(rows.typeIdCache);
        if (type == 0xFFFF) { *why = "the item-port container type isn't registered yet"; return ReadStatus::NoContainer; }
        uint64_t out = 0;
        const uint64_t* h = VCall<const uint64_t*>(entity, map::kEntityComponentByType, &out, &type);
        if (!h || !*h) { *why = "the entity has no item-port container"; return ReadStatus::NoContainer; }
        const uint64_t handle = *h;
        if (!HandleLive(c, handle)) { *why = "the container handle is not live"; return ReadStatus::NoContainer; }
        const uintptr_t comp = handle & kPtrMask;
        if (!CheckedContainer(c, comp)) { *why = "the component isn't the expected item-port container"; return ReadStatus::Guard; }
        *container = comp;
        return ReadStatus::Ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *why = "fault while finding the item-port container";
        return ReadStatus::Fault;
    }
}

}  // namespace

ReadStatus ReadSnapshot(const EngineRows& rows, uintptr_t entity, Generation& generation,
                        sc_inventory_snapshot* io, const char** why) {
    static const char* sink;
    if (!why) why = &sink;
    if (!io || !rows.getItemPorts || !rows.portIsValid || !rows.portItemHandle || !rows.portGetItem ||
        !rows.portGetContainer || !rows.handleValid || !rows.typeIdCache) {
        *why = "rows not resolved";
        return ReadStatus::Guard;
    }
    Ctx c{ &rows };
    uintptr_t container = 0;
    const ReadStatus found = FindContainer(c, entity, &container, why);
    if (found != ReadStatus::Ok) return found;
    Access access;
    access.ctx = &c;
    access.containerPorts = ContainerPorts;
    access.readPort = ReadPort;
    WalkResult r;
    const uint32_t capacity = io->count < SC_INVENTORY_MAX_PORTS ? io->count : SC_INVENTORY_MAX_PORTS;
    if (!Walk(access, container, capacity, io->ports, r)) {
        *why = "the item-port container failed a guard while it was read";
        return ReadStatus::Guard;
    }
    io->count = r.count;
    io->total_seen = r.totalSeen;
    io->flags = r.flags;
    io->generation = generation.Update(io->ports, r.count, r.totalSeen);
    return ReadStatus::Ok;
}

}  // namespace sco::game::inventory
