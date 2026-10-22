#pragma once
// game.inventory 1.1: the portable part of the item-port adapter (docs/game/inventory.md). No game
// memory and no Windows here: the walk's order, caps and limits, and the adapter-side generation,
// run against an Access the engine (inventory_engine.cpp) fills in for the game and the tests fill
// in with synthetic objects. Nothing here is in-game evidence.
#include <sc_inventory.h>
#include <cstddef>
#include <cstdint>

namespace sco::game::inventory {

// What the walk learns about one port.
struct PortInfo {
    bool      valid = false;      // the port holds an item the adapter could read
    uint64_t  itemType = 0;       // the item's type id (valid only)
    uint64_t  entityId = 0;       // the item's entity id (valid only)
    uintptr_t inner = 0;          // the container attached to the item, or 0 (a nested walk)
};

// All three answer false when memory is unreadable or a guard fails: the walk fails closed on the
// player's own container and skips (and flags) a nested one.
struct Access {
    void* ctx = nullptr;
    // container -> its port pointers: *ports points at `count` port references (the engine keeps
    // them in the game's own vector). false: bad pointer, bad bounds or too many ports.
    bool (*containerPorts)(void* ctx, uintptr_t container, uintptr_t* ports, uint32_t* count) = nullptr;
    // The index-th port reference of a container's ports, or false when it isn't a readable port.
    bool (*readPort)(void* ctx, uintptr_t ports, uint32_t index, PortInfo* out) = nullptr;
};

// The walk's own limits, besides the caller's capacity. Past any of them the walk stops going
// deeper or counting and says so with SC_INVENTORY_FLAG_LIMITED.
constexpr uint32_t kMaxDepth      = 8;      // containers below the player's own
constexpr uint32_t kMaxContainers = 256;    // distinct containers visited
constexpr uint32_t kMaxPortsSeen  = 4096;   // ports counted

struct WalkResult {
    uint32_t count = 0;        // ports written to out
    uint32_t totalSeen = 0;    // ports found, written or not
    uint32_t flags = 0;        // SC_INVENTORY_FLAG_*
};

// Walks the tree under root, depth first: each port in the container's order, immediately
// followed by the ports of the container attached to it (if any). The first `capacity` ports are
// written to out (capacity <= SC_INVENTORY_MAX_PORTS); the walk goes on counting after that.
// A container already visited (a cycle, or one attached twice) is not entered again. false: the
// player's own container failed a guard, so nothing can be said (r and out are not meaningful).
bool Walk(const Access& access, uintptr_t root, uint32_t capacity, sc_inventory_port* out, WalkResult& r);

// generation, defined by the adapter because the game has no change counter for the port set
// (docs/game/inventory.md): a digest of the ordered (valid, item type, entity id) list and the
// total, compared with the last snapshot. The first snapshot is generation 1; it advances by one
// whenever the digest differs.
class Generation {
public:
    uint64_t Update(const sc_inventory_port* ports, uint32_t count, uint32_t totalSeen);
    void Reset() { generation_ = 0; digest_ = 0; }
private:
    uint64_t generation_ = 0;
    uint64_t digest_ = 0;
};

}  // namespace sco::game::inventory
