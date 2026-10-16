#pragma once
// game.inventory 1.1: the engine side of the item-port adapter (F-4.10.197.2878-011..-016):
// game memory read under SEH, every vtable entry checked against its signature row before it is
// called, fail closed on anything unexpected. Windows only. Static analysis only: not run in game.
#include "inventory_walk.h"
#include <sc_inventory.h>
#include <cstdint>

namespace sco::game::inventory {

// The resolved rows the adapter needs, as addresses. The host fills it from sco::Sig(); the tests
// fill it with synthetic functions.
struct EngineRows {
    uintptr_t getItemPorts = 0;      // inventory.get_item_ports (compared with, never called)
    uintptr_t portIsValid = 0;       // inventory.port_is_valid
    uintptr_t portItemHandle = 0;    // inventory.port_item_handle
    uintptr_t portGetItem = 0;       // inventory.port_get_item
    uintptr_t portGetContainer = 0;  // inventory.port_get_container
    uintptr_t handleValid = 0;       // inventory.handle_valid: bool(const uint64_t* handle)
    uintptr_t typeIdCache = 0;       // inventory.container_type_id: the u16 cache (0xFFFF = unregistered)
};

enum class ReadStatus : uint8_t {
    Ok,
    NoContainer,   // the entity has no item-port container (or the type isn't registered yet)
    Guard,         // a guard failed: unexpected vtable entry, bad bounds or a bad pointer
    Fault,         // a memory fault while reading
};

// Reads the item ports of `entity` (the player's actor entity pointer) into io: container
// component by type, then the whole attached tree through the Walk. io->count is the capacity in
// and the ports written out; ports, count, total_seen, flags and generation are set, size isn't
// touched. `why` gets a static reason when the status isn't Ok. `generation` advances when the
// snapshot differs from the last one it saw. Game thread.
ReadStatus ReadSnapshot(const EngineRows& rows, uintptr_t entity, Generation& generation,
                        sc_inventory_snapshot* io, const char** why);

}  // namespace sco::game::inventory
