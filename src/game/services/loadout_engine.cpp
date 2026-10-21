// SPDX-License-Identifier: GPL-3.0-only
// Native local-player async loadout adapter. Static build grounding does not
// certify gameplay: explicit SCO_LOADOUT_ASYNC=1 opt-in and fresh G1 admission.
// Item-port slots (port by name, attached container, handle, AttachEntity), the port vector and
// the detach-behavior and persistence-reason values: F-4.10.197.2878-053, with
// F-4.10.197.2878-012/-013. The 14 port names below are the consumer-side slot table of
// docs/game/loadout.md, not verified engine port names: they are data-driven and absent from the
// executable (F-4.10.197.2878-053).
// game.loadout.async defaults OFF. Not run in game.
#include "loadout.h"
#include "spawn.h"
#include "actors.h"
#include "sco/game/actors.h"
#include "sco/game/entities.h"
#include "sco/game/host.h"
#include "sco/game/reads.h"
#include "sco/signatures.h"
#include <cstring>
#include <initializer_list>
#include <windows.h>

namespace sco::game::services {
namespace {
constexpr uint64_t kPointerMask = 0xffffffffffffull;
using PortByName = uintptr_t(__fastcall*)(uintptr_t, const char*);
using PortHandle = uint64_t*(__fastcall*)(uintptr_t, uint64_t*);
using PortContainer = uintptr_t(__fastcall*)(uintptr_t);
using AttachEntity = bool(__fastcall*)(uintptr_t, uint64_t, uint64_t, uint32_t, uint64_t);
PortByName findPort = nullptr;
PortHandle portHandle = nullptr;
PortContainer attachedContainer = nullptr;
AttachEntity attach = nullptr;
uint64_t sequence = 0;
struct Pending {
    uint64_t token = 0, player = 0, parent = 0, entity = 0;
    uintptr_t port = 0;
    uint32_t slot = 0;
    bool attached = false, confirmed = false;
};
Pending pending[SC_LOADOUT_MAX_JOBS]{};

template <typename T> T Read(uintptr_t at) { return *reinterpret_cast<const T*>(at); }
template <typename R, typename... A> R Virtual(uintptr_t object, size_t slot, A... args) {
    using Fn = R(__fastcall*)(uintptr_t, A...);
    return reinterpret_cast<Fn>(Read<uintptr_t>(Read<uintptr_t>(object) + slot))(object, args...);
}
bool SlotFunction(uintptr_t object, size_t slot, const void* expected) {
    return object && Read<uintptr_t>(Read<uintptr_t>(object) + slot) == reinterpret_cast<uintptr_t>(expected);
}
uintptr_t Component(uintptr_t entity, const char* name) {
    const uint8_t* system = Sig("teleport.entity_system");
    const uintptr_t resolver = system ? Read<uintptr_t>(reinterpret_cast<uintptr_t>(system) + 8) : 0;
    if (!resolver || !entity) return 0;
    uint64_t temp[2]{};
    const uint16_t* id = Virtual<const uint16_t*>(resolver, actors::kComponentsTypeId, temp, name);
    if (!id || *id == 0xffff) return 0;
    const uint16_t type = *id;
    uint64_t handle[2]{};
    const uint64_t* found = Virtual<const uint64_t*>(entity, actors::kEntityComponent, handle, &type);
    return found ? (*found & kPointerMask) : 0;
}
uint64_t EntityId(uintptr_t entity) {
    uint64_t temp[2]{};
    const uint64_t* id = entity ? Virtual<const uint64_t*>(entity, entities::kEntityId, temp) : nullptr;
    return id ? *id : 0;
}
bool IsClass(uintptr_t entity, const char* name) {
    if (!entity) return false;
    const uintptr_t cls = Virtual<uintptr_t>(entity, entities::kEntityClass);
    const char* actual = cls ? Virtual<const char*>(cls, entities::kClassName) : nullptr;
    return actual && std::strcmp(actual, name) == 0;
}
bool Admitted(uint64_t* session) {
    if (!sco::game::host::FreshAdmissionAllowed()) return false;
    uintptr_t actor = 0, entity = 0;
    if (!reads::LocalPlayer(actor, entity)) return false;
    *session = EntityId(entity);
    return *session != 0;
}
uintptr_t ChildContainer(uintptr_t port) {
    if (!SlotFunction(port, 0x48, reinterpret_cast<const void*>(attachedContainer))) return 0;
    return attachedContainer(port);
}
// Only descend from the current local player's component. Bound the walk and
// refuse duplicate names: a modified loadout must not select an arbitrary port.
uintptr_t FindOwnedPort(uintptr_t root, const char* name) {
    uintptr_t queue[256]{};
    uint32_t head = 0, count = 1;
    queue[0] = root;
    uintptr_t found = 0;
    while (head < count) {
        const uintptr_t container = queue[head++];
        if (!SlotFunction(container, 0x6f8, reinterpret_cast<const void*>(findPort))) return 0;
        const uintptr_t match = findPort(container, name);
        if (match) { if (found && found != match) return 0; found = match; }
        const uintptr_t begin = Read<uintptr_t>(container + 0x88);
        const uintptr_t end = Read<uintptr_t>(container + 0x90);
        if (end < begin || (end - begin) % sizeof(uintptr_t) || end - begin > 256 * sizeof(uintptr_t)) return 0;
        for (uintptr_t at = begin; at < end; at += sizeof(uintptr_t)) {
            const uintptr_t child = ChildContainer(Read<uintptr_t>(at));
            if (!child) continue;
            bool seen = false;
            for (uint32_t i = 0; i < count; ++i) if (queue[i] == child) { seen = true; break; }
            if (seen) continue;
            if (count == 256) return 0;
            queue[count++] = child;
        }
    }
    return found;
}
uintptr_t TargetPort(uintptr_t player, uint32_t slot) {
    const uintptr_t root = Component(player, "IItemPortContainer");
    if (!root) return 0;
    const char* name = nullptr;
    switch (slot) {
    case SC_LOADOUT_SLOT_HEAD: name = "Armor_Helmet"; break;
    case SC_LOADOUT_SLOT_TORSO: name = "Armor_Torso"; break;
    case SC_LOADOUT_SLOT_ARMS: name = "Armor_Arms"; break;
    case SC_LOADOUT_SLOT_LEGS: name = "Armor_Legs"; break;
    case SC_LOADOUT_SLOT_UNDERSUIT: name = "Armor_Undersuit"; break;
    case SC_LOADOUT_SLOT_BACKPACK: name = "backpack"; break;
    case SC_LOADOUT_SLOT_PRIMARY: name = "wep_stocked_3"; break;
    case SC_LOADOUT_SLOT_SIDEARM: name = "wep_sidearm"; break;
    case SC_LOADOUT_SLOT_VISOR: return 0; // no independent current-build mapping
    default: break;
    }
    if (name) return FindOwnedPort(root, name);
    const bool primary = slot == 10 || slot == 11 || slot == 12 || (slot >= 16 && slot <= 18);
    const uintptr_t parent = FindOwnedPort(root, primary ? "wep_stocked_3" : "wep_sidearm");
    const uintptr_t nested = parent ? ChildContainer(parent) : 0;
    switch (slot) {
    case 10: case 13: name = "optics_attach"; break;
    case 11: case 14: name = "barrel_attach"; break;
    case 12: case 15: name = "underbarrel_attach"; break;
    case 16: case 19: name = "module_attach"; break;
    case 17: case 20: name = "canister_attach"; break;
    case 18: case 21: name = "magazine_attach"; break;
    default: return 0;
    }
    return nested ? FindOwnedPort(nested, name) : 0;
}
uint64_t CurrentEntity(uintptr_t port) {
    if (!SlotFunction(port, 0x68, reinterpret_cast<const void*>(portHandle))) return UINT64_MAX;
    uint64_t handle = 0;
    portHandle(port, &handle);
    const uintptr_t entity = handle & kPointerMask;
    const uint64_t id = EntityId(entity);
    return id && reads::EntityFromId(id) == entity ? id : 0;
}
bool Begin(uint32_t slot, const char* name, uint64_t* token, sc_loadout_status* terminal) {
    uintptr_t actor = 0, player = 0;
    if (!reads::LocalPlayer(actor, player)) { *terminal = SC_LOADOUT_UNAVAILABLE; return true; }
    const uintptr_t port = TargetPort(player, slot);
    if (!port) { *terminal = SC_LOADOUT_UNSUPPORTED; return true; }
    const uint64_t current = CurrentEntity(port);
    if (current == UINT64_MAX) return false;
    if (current && IsClass(reads::EntityFromId(current), name)) { *terminal = SC_LOADOUT_APPLIED; return true; }
    Pending* free = nullptr;
    for (auto& item : pending) if (!item.token) { free = &item; break; }
    if (!free || sequence == UINT64_MAX) return false;
    const uintptr_t zone = reads::EntityZone(player);
    const uint64_t zoneId = zone ? reads::ZoneId(zone) : 0;
    if (!zoneId || !EntityRemovalReady()) { *terminal = SC_LOADOUT_UNAVAILABLE; return true; }
    double position[3]{};
    reads::EntityLocalPos(player, position);
    free->token = ++sequence; free->slot = slot; free->player = EntityId(player);
    free->port = port;
    free->parent = CurrentEntity(port);
    *token = free->token; // retain ownership even if the native spawn partially faults
    if (!sco::game::host::FreshAdmissionAllowed()) {
        *free = {}; *token = 0; *terminal = SC_LOADOUT_UNAVAILABLE; return true;
    }
    const char* error = SpawnEntityInZone(name, zoneId, position, free->entity);
    if (error || !free->entity) {
        *terminal = error && std::strcmp(error, "unknown entity class") == 0 ? SC_LOADOUT_ITEM_MISSING : SC_LOADOUT_FAILED;
        if (free->entity) RemoveEntityLater(free->entity);
        *free = {}; *token = 0;
    }
    return true;
}
bool Poll(uint64_t token, bool* complete, sc_loadout_status* result) {
    Pending* item = nullptr;
    for (auto& candidate : pending) if (candidate.token == token) { item = &candidate; break; }
    if (!item) return false;
    uintptr_t actor = 0, player = 0;
    if (!reads::LocalPlayer(actor, player) || EntityId(player) != item->player) return false;
    const uintptr_t port = TargetPort(player, item->slot);
    if (!port) { *complete = true; *result = SC_LOADOUT_UNSUPPORTED; return true; }
    if (port != item->port) { *complete = true; *result = SC_LOADOUT_FAILED; return true; }
    const uint64_t current = CurrentEntity(port);
    if (current == UINT64_MAX) return false;
    if (current == item->entity) {
        item->confirmed = true; *complete = true; *result = SC_LOADOUT_APPLIED; return true;
    }
    if (item->attached) { *complete = true; *result = SC_LOADOUT_FAILED; return true; }
    if (current != item->parent) { *complete = true; *result = SC_LOADOUT_FAILED; return true; }
    const uintptr_t entity = reads::EntityFromId(item->entity);
    if (!entity) return true; // exact native spawn id is not live yet; tick owns the deadline
    if (!Component(entity, "IAttachableComponent")) {
        *complete = true; *result = SC_LOADOUT_FAILED; return true;
    }
    if (!SlotFunction(port, 0x90, reinterpret_cast<const void*>(attach))) return false;
    // SDetachBehavior { kLinger=1, bHide=false }; reason 0 = NoPersistence.
    // Recheck admission at the native mutation boundary; no network/persistence route.
    if (!sco::game::host::FreshAdmissionAllowed()) { *complete = true; *result = SC_LOADOUT_UNAVAILABLE; return true; }
    item->attached = true;
    if (!attach(port, item->entity, 1, 0, 0)) {
        *complete = true; *result = SC_LOADOUT_FAILED;
    }
    return true;
}
void Discard(uint64_t token) {
    for (auto& item : pending) if (item.token == token) {
        // Never delete equipment after a successful attachment, even if admission
        // changed before its confirmation tick. An uncertain attached item stays.
        if (item.entity && !item.attached && !item.confirmed) RemoveEntityLater(item.entity);
        item = {}; return;
    }
}
const LoadoutAsyncBackend backend{true, Admitted, Begin, Poll, Discard};
}
const LoadoutAsyncBackend* ProductionLoadoutBackend() {
    char enabled[2]{};
    if (GetEnvironmentVariableA("SCO_LOADOUT_ASYNC", enabled, sizeof(enabled)) != 1 || enabled[0] != '1') return nullptr;
    for (const char* row : {"loadout.attach_entity", "loadout.port_by_name", "loadout.port_handle",
                            "loadout.port_container", "spawn.landing_helper", "entities.class_site"})
        if (!SigReady(row)) return nullptr;
    attach = reinterpret_cast<AttachEntity>(Sig("loadout.attach_entity"));
    findPort = reinterpret_cast<PortByName>(Sig("loadout.port_by_name"));
    portHandle = reinterpret_cast<PortHandle>(Sig("loadout.port_handle"));
    attachedContainer = reinterpret_cast<PortContainer>(Sig("loadout.port_container"));
    return &backend;
}
}
