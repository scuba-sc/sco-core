#include "inventory_walk.h"
#include <cstring>

namespace sco::game::inventory {

namespace {

struct State {
    const Access* access;
    uint32_t capacity;
    sc_inventory_port* out;
    WalkResult* r;
    uintptr_t visited[kMaxContainers];
    uint32_t visitedCount;
};

bool Visited(const State& s, uintptr_t container) {
    for (uint32_t i = 0; i < s.visitedCount; ++i)
        if (s.visited[i] == container) return true;
    return false;
}

// false: the container (or one of its ports) failed a guard. At depth 0 the caller treats that as
// fatal; deeper, the subtree is skipped and the result is flagged LIMITED. A port that was already
// counted stays counted.
bool WalkContainer(State& s, uintptr_t container, uint32_t depth) {
    uintptr_t ports = 0;
    uint32_t count = 0;
    if (!container || !s.access->containerPorts(s.access->ctx, container, &ports, &count)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (s.r->totalSeen >= kMaxPortsSeen) { s.r->flags |= SC_INVENTORY_FLAG_LIMITED; return true; }
        PortInfo p;
        if (!s.access->readPort(s.access->ctx, ports, i, &p)) return false;
        if (s.r->totalSeen < s.capacity) {
            sc_inventory_port& o = s.out[s.r->totalSeen];
            memset(&o, 0, sizeof(o));
            o.size = sizeof(o);
            if (p.valid) { o.valid = 1; o.item_class_id = p.itemType; o.entity_id = p.entityId; }
            ++s.r->count;
        }
        ++s.r->totalSeen;
        if (!p.inner) continue;
        if (depth >= kMaxDepth || s.visitedCount >= kMaxContainers || Visited(s, p.inner)) {
            s.r->flags |= SC_INVENTORY_FLAG_LIMITED;
            continue;
        }
        s.visited[s.visitedCount++] = p.inner;
        if (!WalkContainer(s, p.inner, depth + 1)) s.r->flags |= SC_INVENTORY_FLAG_LIMITED;
    }
    return true;
}

constexpr uint64_t kFnvBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t Mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) { h ^= (v >> (8 * i)) & 0xFF; h *= kFnvPrime; }
    return h;
}

}  // namespace

bool Walk(const Access& access, uintptr_t root, uint32_t capacity, sc_inventory_port* out, WalkResult& r) {
    r = {};
    if (!access.containerPorts || !access.readPort || !root || (capacity && !out)) return false;
    State s{ &access, capacity > SC_INVENTORY_MAX_PORTS ? SC_INVENTORY_MAX_PORTS : capacity, out, &r, {}, 0 };
    s.visited[s.visitedCount++] = root;
    if (!WalkContainer(s, root, 0)) { r = {}; return false; }
    if (r.totalSeen > r.count) r.flags |= SC_INVENTORY_FLAG_TRUNCATED;
    return true;
}

uint64_t Generation::Update(const sc_inventory_port* ports, uint32_t count, uint32_t totalSeen) {
    uint64_t h = Mix(Mix(kFnvBasis, count), totalSeen);
    for (uint32_t i = 0; i < count; ++i) {
        h = Mix(h, ports[i].valid);
        h = Mix(h, ports[i].item_class_id);
        h = Mix(h, ports[i].entity_id);
    }
    if (!generation_) generation_ = 1;
    else if (h != digest_) ++generation_;
    digest_ = h;
    return generation_;
}

}  // namespace sco::game::inventory
