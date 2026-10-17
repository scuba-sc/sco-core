#pragma once
// game.inventory inside the game pack: the layout the adapter reads (F-4.10.197.2878-011..-016).
// Private to src/game/. Static analysis only, not run in game. inventory_sigs.cpp's rows pin the
// functions these offsets belong to; the adapter (services/inventory_engine.cpp) checks every
// vtable entry it calls against those rows before it calls it, and fails closed on a mismatch.
#include <cstddef>
#include <cstdint>

namespace sco::game::inventory_map {

// Entity: its virtual "component by type" lookup, the same call game.hangar's IIM lookup uses
// (handle = *fn(entity, &out, &typeId)), and its id accessor (the same call game.entities uses).
constexpr size_t kEntityComponentByType = 0x390;
constexpr size_t kEntityId              = 0x08;

// The item-port container component (what the game's own port walk is a method of).
constexpr size_t kContainerPortsBegin = 0x88;    // CItemPort** first
constexpr size_t kContainerPortsEnd   = 0x90;    // CItemPort** one past the last
constexpr size_t kContainerGetPorts   = 0x898;   // vtable entry: inventory.get_item_ports

// One item port: vtable entries the adapter calls, each checked against its row.
constexpr size_t kPortGetItem      = 0x40;       // inventory.port_get_item
constexpr size_t kPortGetContainer = 0x48;       // inventory.port_get_container
constexpr size_t kPortIsValid      = 0x60;       // inventory.port_is_valid
constexpr size_t kPortItemHandle   = 0x68;       // inventory.port_item_handle

// The attached item (what port_get_item returns): descriptor accessor, then the item type.
constexpr size_t kItemDescriptor = 0x6E8;        // vtable entry, called with the item
constexpr size_t kDescItemType   = 0x08;         // u32 in the descriptor: the item type id

// Sanity bounds. A container with more ports than this is a bad read, not an inventory.
constexpr uint32_t kMaxPortsPerContainer = 4096;

// The item type enumeration's id <-> name pairs inventory.item_type_names checks on every build.
struct TypeName { uint32_t id; const char* name; };
constexpr TypeName kTypeNames[] = {
    { 22,  "Char_Armor_Helmet" },
    { 24,  "Char_Armor_Torso" },
    { 26,  "Char_Armor_Backpack" },
    { 30,  "Char_Clothing_Hat" },
    { 95,  "FPS_WeaponSidearm" },
    { 110, "InventoryContainer" },
    { 132, "MobiGlas" },
    { 167, "Ship" },
};

}  // namespace sco::game::inventory_map
