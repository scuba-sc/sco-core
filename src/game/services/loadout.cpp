// SPDX-License-Identifier: GPL-3.0-only
#include "loadout.h"
#include "sc_loadout.h"
#include "sc_game_events.h"
#include <atomic>
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
#if defined(_WIN32) && defined(_MSC_VER)
#include <windows.h>
#endif
namespace sco::game::services {
namespace {
bool started = false;
LoadoutBackend engine{};
bool faulted = false;
bool applying = false;
constexpr const char* kCapRespawn = "game.events.loadout_respawn_completed";
// One state word makes recording atomic with Stop/restart. Low bits: enabled/pending;
// the generation changes whenever admission is configured, so old producers cannot replay.
std::atomic<uint64_t> respawnState{0};
bool respawnSubscribed = false;
void SetRespawn(bool on, const char* why) {
    const uint64_t generation = (respawnState.load() & ~uint64_t{3}) + 4;
    respawnState.store(generation | (on ? 1u : 0u));
    caps::Set(kCapRespawn, on, why);
}
struct ApplyScope {
    ApplyScope() { applying = true; }
    ~ApplyScope() { applying = false; }
};
int OfflineCpp() {
    try { return engine.offline() ? 1 : 0; } catch (...) { return -1; }
}
sc_loadout_status EquipCpp(uint32_t slot, const char* name) {
    try { return engine.equip(slot, name); } catch (...) { return SC_LOADOUT_STATUS_FORCE32; }
}
int SafeOffline() {
#if defined(_WIN32) && defined(_MSC_VER)
    __try { return OfflineCpp(); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
#else
    return OfflineCpp();
#endif
}
sc_loadout_status SafeEquip(uint32_t slot, const char* name) {
#if defined(_WIN32) && defined(_MSC_VER)
    __try { return EquipCpp(slot, name); } __except (EXCEPTION_EXECUTE_HANDLER) { return SC_LOADOUT_STATUS_FORCE32; }
#else
    return EquipCpp(slot, name);
#endif
}
void Fault() {
    faulted = true;
    caps::Set("game.loadout.apply", false, "loadout provider fault; process restart required");
    caps::Set("game.loadout.weapons", false, "loadout provider fault; process restart required");
    SetRespawn(false, "loadout provider fault; process restart required");
}
sco_result Apply(sco_plugin* self, const sc_loadout_request* request, sc_loadout_report* report) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!host::PluginId(self) || detail::Released(self) || !request || !report ||
        request->size < sizeof(*request) || report->size < sizeof(*report) ||
        !request->count || !request->items || request->item_stride != sizeof(sc_loadout_item) || request->_pad)
        return SCO_BAD_ARG;
    if (request->count > SC_LOADOUT_MAX_ITEMS) return SCO_TOO_MANY;
    const uint32_t count = request->count;
    sc_loadout_item items[SC_LOADOUT_MAX_ITEMS]{};
    for (uint32_t i = 0; i < count; ++i) {
        items[i] = request->items[i];
        const auto& item = items[i];
        if (item._pad || !item.item_class[0]) return SCO_BAD_ARG;
        uint32_t n = 0;
        for (; n < SC_LOADOUT_ITEM_NAME_BYTES && item.item_class[n]; ++n) {
            const char c = item.item_class[n];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_')) return SCO_BAD_ARG;
        }
        if (n == SC_LOADOUT_ITEM_NAME_BYTES) return SCO_BAD_ARG;
        for (uint32_t j = 0; j < i; ++j)
            if (items[j].slot == item.slot) return SCO_BAD_ARG;
    }
    *report = {};
    report->size = sizeof(*report); report->count = count;
    for (uint32_t i = 0; i < count; ++i)
        report->slots[i] = {items[i].slot, SC_LOADOUT_UNAVAILABLE};
    if (!started || faulted || applying || !caps::Has("game.loadout.apply") || !engine.verified || !engine.offline || !engine.equip)
        return SCO_UNAVAILABLE;
    const ApplyScope scope;
    const int admitted = SafeOffline();
    if (admitted < 0) { Fault(); return SCO_FAILED; }
    if (!admitted) return SCO_UNAVAILABLE;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& item = items[i];
        auto& result = report->slots[i];
        if (item.slot < SC_LOADOUT_SLOT_HEAD || item.slot > SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY) {
            result.status = SC_LOADOUT_UNSUPPORTED;
            continue;
        }
        if (item.slot >= SC_LOADOUT_SLOT_PRIMARY) {
            if (!engine.weapons) { result.status = SC_LOADOUT_UNSUPPORTED; continue; }
            if (!caps::Has("game.loadout.weapons")) continue;  // stays UNAVAILABLE, no engine call
        }
        if (i) {
            const int stillAdmitted = SafeOffline();
            if (stillAdmitted < 0) { result.status = SC_LOADOUT_FAILED; Fault(); break; }
            if (!stillAdmitted || !caps::Has("game.loadout.apply")) break;
        }
        result.status = SafeEquip(item.slot, item.item_class);
        if (result.status < SC_LOADOUT_APPLIED || result.status > SC_LOADOUT_UNAVAILABLE) {
            result.status = SC_LOADOUT_FAILED;
            Fault(); break;
        }
        if (result.status == SC_LOADOUT_APPLIED) ++report->applied;
    }
    return report->applied == report->count ? SCO_OK : SCO_FAILED;
}
const sc_loadout_v1 table = {sizeof(sc_loadout_v1), 0, Apply};
// Game-thread tick: turn a recorded occurrence into one bus event, after a live admission check.
void OnRespawnTick(const char*, const void*, void*) {
    const uint64_t state = respawnState.fetch_and(~uint64_t{2});
    if ((state & 3u) != 3u) return;
    if (!started || faulted || !caps::Has(kCapRespawn) || !engine.offline) return;
    const int admitted = SafeOffline();
    if (admitted < 0) { Fault(); return; }
    if (!admitted) return;
    const sc_game_loadout_respawn_completed event = {sizeof(event), 0};
    Dispatch(SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED, &event);
}
}
Result StartLoadout(const LoadoutBackend* backend) {
    if (!OnGameThread()) return Result::WrongThread;
    if (applying) return Result::Unavailable;
    if (started) return Result::Ok;
    engine = backend ? *backend : LoadoutBackend{};
    const Result r = host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_1, &table);
    started = r == Result::Ok;
    const bool applyOn = started && !faulted && engine.verified && engine.equip && engine.offline;
    caps::Set("game.loadout.apply", applyOn,
              faulted ? "loadout provider fault; process restart required" :
                        "typed engine equip methods not verified; no provider installed");
    caps::Set("game.loadout.weapons", applyOn && engine.weapons,
              faulted ? "loadout provider fault; process restart required" :
                        "typed weapon and multitool slot methods not verified; default off");
    const bool respawnWanted = started && !faulted && engine.verified && engine.respawnCompleted && engine.offline;
    respawnSubscribed = respawnWanted && Subscribe(host::GameOwner(), "tick", OnRespawnTick, nullptr) == Result::Ok;
    SetRespawn(respawnSubscribed,
               faulted ? "loadout provider fault; process restart required" :
                         "native respawn-loadout-completed occurrence not verified (F-INTAKE-004, T-3); default off");
    if (!started) engine = {};
    return r;
}
bool RecordRespawnLoadoutCompleted() {
    uint64_t state = respawnState.load();
    if (!(state & 1u) || !caps::Has(kCapRespawn)) return false;
    const uint64_t generation = state & ~uint64_t{3};
    do {
        if (!(state & 1u) || (state & ~uint64_t{3}) != generation) return false;
    } while (!respawnState.compare_exchange_weak(state, state | 2u));
    return true;
}
void StopLoadout() {
    if (!OnGameThread() || !started || applying) return;
    host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    caps::Set("game.loadout.apply", false, "game loadout stopped");
    caps::Set("game.loadout.weapons", false, "game loadout stopped");
    if (respawnSubscribed) Unsubscribe(host::GameOwner(), "tick", OnRespawnTick);
    respawnSubscribed = false;
    SetRespawn(false, "game loadout stopped");
    started = false;
    engine = {};
}
}
