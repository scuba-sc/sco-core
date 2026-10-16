// game.actor_stats 1.0 (sc_actor_stats.h) from the game pack: an actor's status stats by name
// (F-4.10.197.2878-006). Started and stopped by game.actors (actors.cpp), whose actor lookup it
// uses (ActorStatusOf); the stat itself is read with the game's GetStat (actor.get_stat) after the
// record array (actor.stat_array) is checked for a record, so "no such stat" is never a silent 0.
// actor.stat_names keeps every sc_actor_stat on the game index that carries its name
// (actor_stats_map.h). Read-only. Nothing here has been run in game.
#include "actors.h"
#include "../actor_stats_map.h"
#include "sco/caps.h"
#include "sco/host.h"
#include "sco/log.h"
#include "sco/signatures.h"
#include <sc_actor_stats.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <windows.h>

namespace sco::game::services {

namespace {

using GetStatFn = float(__fastcall*)(uintptr_t status, int stat);

constexpr const char* kCapRead = "game.actor_stats.read";
constexpr const char* kRows[] = { "actor.stat_names", "actor.stat_array", "actor.get_stat" };

bool      g_started = false;
bool      g_canRead = false;
GetStatFn g_getStat = nullptr;
int32_t   g_arrayOff = 0;   // the record array's offset in CSCActorStatus, from actor.stat_array
char      g_why[128] = "";

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

sco_result Refuse(sco_result r, const char* fmt, uint64_t id, uint32_t stat, const char* why) {
    char text[200];
    snprintf(text, sizeof(text), fmt, static_cast<unsigned long long>(id), stat, why ? why : "");
    Log("[game] game.actor_stats: %s", text);
    return r;
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
    if (!ReadStat(status, index, hasRecord, value))
        return Refuse(SCO_FAILED, "read(%llu, %u): fault while reading the stat%s", id, stat, "");
    if (!hasRecord) return SCO_NOT_FOUND;
    if (!std::isfinite(value)) return Refuse(SCO_FAILED, "read(%llu, %u): the value isn't finite%s", id, stat, "");
    *outValue = value;
    return SCO_OK;
}

const sc_actor_stats_v1 kTable = { sizeof(sc_actor_stats_v1), 0, SvcRead };

}  // namespace

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
    const Result r = host::ProvideGameService(SC_ACTOR_STATS_NAME, SC_ACTOR_STATS_VERSION_1_0, &kTable);
    if (r != Result::Ok) {
        caps::Set(kCapRead, false, "game.actor_stats not published");
        g_canRead = false;
        g_getStat = nullptr;
        g_arrayOff = 0;
        return r;
    }
    if (!g_canRead) Log("[game] %s not ready: %s; read answers SCO_UNAVAILABLE", kCapRead, g_why);
    g_started = true;
    return Result::Ok;
}

void StopActorStats() {
    if (!g_started) return;
    host::WithdrawGameService(SC_ACTOR_STATS_NAME);
    caps::Set(kCapRead, false, "stopped");
    g_canRead = false;
    g_getStat = nullptr;
    g_arrayOff = 0;
    g_started = false;
}

}  // namespace sco::game::services
