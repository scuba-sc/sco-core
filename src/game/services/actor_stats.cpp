// game.actor_stats 1.0 (sc_actor_stats.h) from the game pack: an actor's status stats by name
// (F-4.10.197.2878-006). Started and stopped by game.actors (actors.cpp), whose actor lookup it
// uses (ActorStatusOf); the stat itself is read with the game's GetStat (actor.get_stat) after the
// record array (actor.stat_array) is checked for a record, so "no such stat" is never a silent 0.
// actor.stat_names keeps every sc_actor_stat on the game index that carries its name
// (actor_stats_map.h).
//
// game.actor_stats_write 1.0 (sc_actor_stats_write.h, F-4.10.197.2878-010) is a second table here:
// your own player's stats through the game's AdjustStatValue (actor.adjust_stat). Its capability
// is off unless the product switch (SetActorStatWritesEnabled, default off) is on, and every write
// asks for fresh offline admission first. Nothing is restored on unload: these stats are gameplay
// state the game changes itself every tick. Nothing here has been run in game.
#include "actors.h"
#include "../actor_stats_map.h"
#include "../../api/internal.h"   // detail::Released
#include "sco/caps.h"
#include "sco/game/host.h"
#include "sco/game/services.h"
#include "sco/host.h"
#include "sco/log.h"
#include "sco/signatures.h"
#include <sc_actor_stats.h>
#include <sc_actor_stats_write.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <windows.h>

namespace sco::game::services {

namespace {

using GetStatFn = float(__fastcall*)(uintptr_t status, int stat);
using AdjustFn  = void(__fastcall*)(uintptr_t status, int stat, float delta, const char* source, uint16_t hitId);

constexpr const char* kCapRead  = "game.actor_stats.read";
constexpr const char* kCapWrite = "game.actor_stats.write";
constexpr const char* kRows[] = { "actor.stat_names", "actor.stat_array", "actor.get_stat" };
constexpr const char* kWriteRows[] = { "actor.adjust_stat" };
constexpr uint16_t kNoHit = 0xFFFF;   // the hit id the game's own non-hit callers pass

bool      g_started = false;
bool      g_canRead = false;
bool      g_writeRows = false;   // actor.adjust_stat OK
GetStatFn g_getStat = nullptr;
AdjustFn  g_adjust = nullptr;
int32_t   g_arrayOff = 0;   // the record array's offset in CSCActorStatus, from actor.stat_array
char      g_why[128] = "";
std::atomic<bool> g_writesEnabled{false};

bool RowOk(const char* id) {
    const SigResult* s = SigLookup(id);
    return s && s->state == SigState::Ok;
}

// Game memory, under SEH. *hasRecord: the actor has a record of index.
bool ReadStat(uintptr_t status, int index, bool& hasRecord, float& value) {
    __try {
        hasRecord = *reinterpret_cast<const uintptr_t*>(status + g_arrayOff + 8 * static_cast<intptr_t>(index)) != 0;
        value = hasRecord ? g_getStat(status, index) : 0.0f;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallAdjust(uintptr_t status, int index, float delta) {
    __try {
        g_adjust(status, index, delta, nullptr, kNoHit);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Note(const char* what, uint64_t id, uint32_t stat) {
    Log("[game] game.actor_stats: %s (id %llu, stat %u)", what, static_cast<unsigned long long>(id), stat);
}

sco_result SvcRead(uint64_t id, uint32_t stat, float* outValue) {
    if (outValue) *outValue = 0;
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!outValue || !id) return SCO_BAD_ARG;
    const int index = actor_stats::IndexOf(stat);
    if (index < 0) return SCO_BAD_ARG;
    const char* why = nullptr;
    if (!g_started || !g_canRead || !ActorStatusReady(why)) return SCO_UNAVAILABLE;
    uintptr_t status = 0;
    const Result found = ActorStatusOf(id, status, why);
    if (found != Result::Ok) return static_cast<sco_result>(found);
    bool hasRecord = false;
    float value = 0;
    if (!ReadStat(status, index, hasRecord, value)) { Note("fault while reading a stat", id, stat); return SCO_FAILED; }
    if (!hasRecord) return SCO_NOT_FOUND;
    if (!std::isfinite(value)) { Note("a stat isn't finite", id, stat); return SCO_FAILED; }
    *outValue = value;
    return SCO_OK;
}

const sc_actor_stats_v1 kTable = { sizeof(sc_actor_stats_v1), 0, SvcRead };

bool WriteReady() { return g_started && g_canRead && g_writeRows && g_adjust && g_writesEnabled.load(); }

void UpdateWriteCap() {
    if (!g_started) return;
    const char* why = !g_canRead ? "needs game.actor_stats.read"
                    : !g_writeRows ? "needs actor.adjust_stat"
                    : !g_writesEnabled.load() ? "stat writes are switched off (product setting or SCO_ACTOR_STATS_WRITE=1)"
                    : nullptr;
    caps::Set(kCapWrite, why == nullptr && WriteReady(), why);
}

// The checks adjust and set share. On SCO_OK: *status is your player's, *index the game index and
// *current the stat's value before the write.
sco_result EnterWrite(sco_plugin* self, uint64_t id, uint32_t stat, uintptr_t& status, int& index, float& current) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !sco::host::PluginId(self) || sco::detail::Released(self)) return SCO_BAD_ARG;
    index = actor_stats::IndexOf(stat);
    if (!id || index < 0) return SCO_BAD_ARG;
    const char* why = nullptr;
    if (!WriteReady() || !ActorStatusReady(why) || !caps::Has(kCapWrite)) return SCO_UNAVAILABLE;
    if (!sco::game::host::FreshAdmissionAllowed()) return SCO_UNAVAILABLE;   // offline only, asked before every write
    if (!IsLocalPlayerEntity(id)) return SCO_BAD_ARG;           // 1.0: your own player only
    const Result found = ActorStatusOf(id, status, why);
    if (found != Result::Ok) return static_cast<sco_result>(found);
    bool hasRecord = false;
    if (!ReadStat(status, index, hasRecord, current)) { Note("fault while reading a stat before a write", id, stat); return SCO_FAILED; }
    if (!hasRecord) return SCO_NOT_FOUND;
    if (!std::isfinite(current)) { Note("a stat isn't finite before a write", id, stat); return SCO_FAILED; }
    return SCO_OK;
}

sco_result Apply(uintptr_t status, int index, float delta, uint64_t id, uint32_t stat) {
    if (!std::isfinite(delta) || std::fabs(delta) > SC_ACTOR_STATS_WRITE_MAX_STEP) return SCO_BAD_ARG;
    if (delta == 0.0f) return SCO_OK;
    if (!CallAdjust(status, index, delta)) { Note("fault in the game's stat adjust", id, stat); return SCO_FAILED; }
    return SCO_OK;
}

sco_result SvcAdjust(sco_plugin* self, uint64_t id, uint32_t stat, float delta) {
    if (!std::isfinite(delta) || std::fabs(delta) > SC_ACTOR_STATS_WRITE_MAX_STEP) return OnGameThread() ? SCO_BAD_ARG : SCO_WRONG_THREAD;
    uintptr_t status = 0;
    int index = -1;
    float current = 0;
    const sco_result r = EnterWrite(self, id, stat, status, index, current);
    return r == SCO_OK ? Apply(status, index, delta, id, stat) : r;
}

sco_result SvcSet(sco_plugin* self, uint64_t id, uint32_t stat, float value) {
    if (!std::isfinite(value)) return OnGameThread() ? SCO_BAD_ARG : SCO_WRONG_THREAD;
    uintptr_t status = 0;
    int index = -1;
    float current = 0;
    const sco_result r = EnterWrite(self, id, stat, status, index, current);
    return r == SCO_OK ? Apply(status, index, value - current, id, stat) : r;
}

const sc_actor_stats_write_v1 kWriteTable = { sizeof(sc_actor_stats_write_v1), 0, SvcAdjust, SvcSet };

void Clear() {
    g_canRead = false;
    g_writeRows = false;
    g_getStat = nullptr;
    g_adjust = nullptr;
    g_arrayOff = 0;
}

}  // namespace

void SetActorStatWritesEnabled(bool enabled) {
    g_writesEnabled.store(enabled);
    if (OnGameThread()) UpdateWriteCap();   // otherwise the next StartActorStats or write sees it
}

bool ActorStatWritesEnabled() { return g_writesEnabled.load(); }

Result StartActorStats() {
    if (g_started) return Result::Ok;
    g_why[0] = 0;
    for (const char* id : kRows)
        if (!RowOk(id)) { snprintf(g_why, sizeof(g_why), "needs %s", id); break; }
    caps::SetFromSignatures(kCapRead, kRows, std::size(kRows));
    if (!g_why[0]) {
        g_getStat = reinterpret_cast<GetStatFn>(Sig("actor.get_stat"));
        memcpy(&g_arrayOff, Sig("actor.stat_array"), sizeof(g_arrayOff));
        if (g_arrayOff <= 0) snprintf(g_why, sizeof(g_why), "%s", "the record array offset isn't positive");
    }
    const char* actorsWhy = nullptr;
    if (!g_why[0] && !ActorStatusReady(actorsWhy)) snprintf(g_why, sizeof(g_why), "%s", actorsWhy ? actorsWhy : "game.actors isn't ready");
    g_canRead = !g_why[0] && caps::Has(kCapRead);
    if (!g_canRead) caps::Set(kCapRead, false, g_why[0] ? g_why : "rows not ready");
    g_writeRows = RowOk("actor.adjust_stat");
    if (g_writeRows) g_adjust = reinterpret_cast<AdjustFn>(Sig("actor.adjust_stat"));
    caps::SetFromSignatures(kCapWrite, kWriteRows, std::size(kWriteRows));
    caps::Set(kCapWrite, false, "game.actor_stats_write not published yet");
    char env[8] = "";
    GetEnvironmentVariableA("SCO_ACTOR_STATS_WRITE", env, sizeof(env));
    if (env[0] == '1') g_writesEnabled.store(true);

    Result r = sco::host::ProvideGameService(SC_ACTOR_STATS_NAME, SC_ACTOR_STATS_VERSION_1_0, &kTable);
    if (r == Result::Ok) {
        r = sco::host::ProvideGameService(SC_ACTOR_STATS_WRITE_NAME, SC_ACTOR_STATS_WRITE_VERSION_1_0, &kWriteTable);
        if (r != Result::Ok) sco::host::WithdrawGameService(SC_ACTOR_STATS_NAME);
    }
    if (r != Result::Ok) {
        caps::Set(kCapRead, false, "game.actor_stats not published");
        caps::Set(kCapWrite, false, "game.actor_stats_write not published");
        Clear();
        return r;
    }
    g_started = true;
    UpdateWriteCap();
    if (!g_canRead) Log("[game] %s not ready: %s; read answers SCO_UNAVAILABLE", kCapRead, g_why);
    Log("[game] %s is %s", kCapWrite, caps::Has(kCapWrite) ? "ON (every write still needs fresh offline admission)" : "off");
    return Result::Ok;
}

void StopActorStats() {
    if (!g_started) return;
    sco::host::WithdrawGameService(SC_ACTOR_STATS_WRITE_NAME);
    sco::host::WithdrawGameService(SC_ACTOR_STATS_NAME);
    caps::Set(kCapRead, false, "stopped");
    caps::Set(kCapWrite, false, "stopped");
    Clear();
    g_started = false;
}

}  // namespace sco::game::services
