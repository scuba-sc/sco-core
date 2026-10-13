// SPDX-License-Identifier: GPL-3.0-only
#include "loadout.h"
#include "sc_loadout.h"
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
        if (item.slot < SC_LOADOUT_SLOT_HEAD || item.slot > SC_LOADOUT_SLOT_VISOR) {
            result.status = SC_LOADOUT_UNSUPPORTED;
            continue;
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
}
Result StartLoadout(const LoadoutBackend* backend) {
    if (!OnGameThread()) return Result::WrongThread;
    if (applying) return Result::Unavailable;
    if (started) return Result::Ok;
    engine = backend ? *backend : LoadoutBackend{};
    const Result r = host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &table);
    started = r == Result::Ok;
    caps::Set("game.loadout.apply", started && !faulted && engine.verified && engine.equip && engine.offline,
              faulted ? "loadout provider fault; process restart required" :
                        "typed engine equip methods not verified; no provider installed");
    if (!started) engine = {};
    return r;
}
void StopLoadout() {
    if (!OnGameThread() || !started || applying) return;
    host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    caps::Set("game.loadout.apply", false, "game loadout stopped");
    started = false;
    engine = {};
}
}
