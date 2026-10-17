// game.inventory 1.1 production binding. Nothing here has been run in game: the capability stays
// OFF until the owner's in-game run (docs/game/inventory.md, F-4.10.197.2878-011..-016).
#include "inventory_host.h"
#include "inventory.h"
#include "inventory_engine.h"
#include "sco/caps.h"
#include "sco/game/reads.h"
#include "sco/log.h"
#include "sco/signatures.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <windows.h>

namespace sco::game::services {

namespace {

namespace inv = sco::game::inventory;

constexpr const char* kCap = "game.inventory.read";
constexpr const char* kRows[] = {
    "inventory.get_item_ports", "inventory.port_is_valid", "inventory.port_item_handle",
    "inventory.handle_valid", "inventory.unequip_item", "inventory.port_get_item",
    "inventory.port_get_container", "inventory.container_type_id", "inventory.item_type_names",
};

bool                g_started = false;
bool                g_rowsOk = false;
inv::EngineRows     g_rows;
inv::Generation     g_generation;
char                g_why[128] = "";
std::atomic<bool>   g_enabled{false};
bool                g_loggedFail = false;

bool RowOk(const char* id) {
    const SigResult* s = SigLookup(id);
    return s && s->state == SigState::Ok;
}

void UpdateCap() {
    if (!g_started) return;
    const char* why = !g_rowsOk ? (g_why[0] ? g_why : "rows not ready")
                    : !g_enabled.load() ? "OFF pending in-game confirmation (SCO_INVENTORY_READ=1 or the product setting turns it on)"
                    : nullptr;
    caps::Set(kCap, why == nullptr, why);
}

// The player's actor entity pointer, under SEH. false: no local player yet.
bool LocalEntity(uintptr_t& entity) {
    __try {
        uintptr_t actor = 0;
        return reads::LocalPlayer(actor, entity);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Snapshot(void*, sc_inventory_snapshot* io) {
    if (!OnGameThread() || !g_started) return false;
    UpdateCap();   // picks up a switch flipped off the game thread
    if (!caps::Has(kCap)) return false;
    uintptr_t entity = 0;
    if (!reads::Ready() || !LocalEntity(entity)) return false;
    const char* why = nullptr;
    const inv::ReadStatus st = inv::ReadSnapshot(g_rows, entity, g_generation, io, &why);
    if (st == inv::ReadStatus::Ok) { g_loggedFail = false; return true; }
    // No container is an ordinary answer; a guard or a fault is logged once per run of failures.
    if (st != inv::ReadStatus::NoContainer && !g_loggedFail) {
        g_loggedFail = true;
        Log("[game] game.inventory: %s; answering SCO_UNAVAILABLE", why ? why : "read failed");
    }
    return false;
}

void ResolveRows() {
    g_why[0] = 0;
    for (const char* id : kRows)
        if (!RowOk(id)) { snprintf(g_why, sizeof(g_why), "needs %s", id); break; }
    g_rowsOk = !g_why[0];
    g_rows = {};
    if (!g_rowsOk) return;
    g_rows.getItemPorts = reinterpret_cast<uintptr_t>(Sig("inventory.get_item_ports"));
    g_rows.portIsValid = reinterpret_cast<uintptr_t>(Sig("inventory.port_is_valid"));
    g_rows.portItemHandle = reinterpret_cast<uintptr_t>(Sig("inventory.port_item_handle"));
    g_rows.portGetItem = reinterpret_cast<uintptr_t>(Sig("inventory.port_get_item"));
    g_rows.portGetContainer = reinterpret_cast<uintptr_t>(Sig("inventory.port_get_container"));
    g_rows.handleValid = reinterpret_cast<uintptr_t>(Sig("inventory.handle_valid"));
    g_rows.typeIdCache = reinterpret_cast<uintptr_t>(Sig("inventory.container_type_id"));
}

}  // namespace

void SetInventoryReadEnabled(bool enabled) {
    g_enabled.store(enabled);
    if (OnGameThread()) UpdateCap();   // otherwise the next snapshot sees it
}

bool InventoryReadEnabled() { return g_enabled.load(); }

Result StartInventory() {
    if (g_started) return Result::Ok;
    ResolveRows();
    char env[8] = "";
    GetEnvironmentVariableA("SCO_INVENTORY_READ", env, sizeof(env));
    if (env[0] == '1') g_enabled.store(true);
    g_generation.Reset();
    g_loggedFail = false;
    caps::SetFromSignatures(kCap, kRows, std::size(kRows));
    InventoryBackend backend;
    backend.snapshot = Snapshot;
    const Result r = StartGameInventory(backend);
    if (r != Result::Ok) {
        caps::Set(kCap, false, "game.inventory not published");
        return r;
    }
    g_started = true;
    UpdateCap();
    Log("[game] %s is %s", kCap, caps::Has(kCap) ? "ON" : "off");
    return Result::Ok;
}

void StopInventory() {
    if (!g_started) return;
    StopGameInventory();
    caps::Set(kCap, false, "stopped");
    g_started = false;
    g_rowsOk = false;
    g_rows = {};
}

}  // namespace sco::game::services
