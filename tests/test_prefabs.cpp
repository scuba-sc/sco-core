// GPL-3.0-only. Synthetic inputs only; no engine/content files.
#include "../src/game/services/prefabs.h"
#include "scosdk/game/prefabs.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
#include <fstream>
#include <sstream>
using namespace sco::game::services;
namespace {
int a, b; auto* owner = reinterpret_cast<sco_plugin*>(&a);
auto* foreign = reinterpret_cast<sco_plugin*>(&b);
bool thread = true, live = true, ready = true, fault = false, zero_id = false;
uint32_t spawns = 0, remembered = 0;
sco_result reservation = SCO_OK;
sco_plugin* saved_owner = nullptr; uint64_t saved_id = 0;
bool Thread() { return thread; }
bool Live(const sco_plugin* self) { return live && (self == owner || self == foreign); }
bool Ready() { return ready; }
sco_result Reserve(const sco_plugin*) { return reservation; }
sco_result Zone(uint64_t zone) { return zone == 7 ? SCO_OK : SCO_NOT_FOUND; }
sco_result Spawn(const char* path, uint64_t zone, const double* pos, const double* q, uint64_t* id) {
    if (std::strcmp(path, "ObjectContainers/synthetic/room.socpak") || zone != 7 || pos[2] != 3 || q[3] != 1) return SCO_FAILED;
    ++spawns; *id = zero_id ? 0 : 55; return fault ? SCO_FAILED : SCO_OK;
}
void Remember(sco_plugin* self, uint64_t id) { ++remembered; saved_owner = self; saved_id = id; }
const PrefabProvider provider{Thread, Live, Ready, Reserve, Zone, Spawn, Remember};
sco_result Forward(sco_plugin* self, const char* path, uint64_t zone, const double* pos, const double* q, uint64_t* id) {
    return SpawnOwnedPrefab(provider, self, path, zone, pos, q, id);
}
const void* offered = nullptr; sco_result query_result = SCO_OK;
sco_result Query(const char* name, uint32_t version, const void** out) {
    if (std::strcmp(name, SC_PREFABS_NAME) || version != SC_PREFABS_VERSION_1_0) return SCO_BAD_ARG;
    *out = offered; return query_result;
}
unsigned passed = 0, failed = 0;
#define CHECK(expr) do { if (expr) ++passed; else { ++failed; std::printf("FAIL %u: %s\n", __LINE__, #expr); } } while (0)
}
int main() {
    const char* path = "ObjectContainers/synthetic/room.socpak";
    CHECK(ValidPrefabPath(path)); CHECK(!ValidPrefabPath(nullptr));
    for (const char* invalid : {"", "/ObjectContainers/a.socpak", "ObjectContainers//a.socpak",
        "ObjectContainers/../a.socpak", "ObjectContainers/./a.socpak", "ObjectContainers/a/../../b.socpak",
        "ObjectContainers/a.socpak/", "ObjectContainers/a.socpak?x", "ObjectContainers/a.socpak#x",
        "ObjectContainers/%2e%2e/a.socpak", "C:/ObjectContainers/a.socpak", "ObjectContainers/a\\b.socpak",
        "objectcontainers/a.socpak", "ObjectContainers/.socpak", "ObjectContainers/a.txt", "ObjectContainers/a .socpak"})
        CHECK(!ValidPrefabPath(invalid));
    const std::string prefix = "ObjectContainers/";
    const std::string max = prefix + std::string(SC_PREFABS_MAX_PATH - prefix.size() - 7, 'a') + ".socpak";
    CHECK(max.size() == SC_PREFABS_MAX_PATH); CHECK(ValidPrefabPath(max.c_str()));
    CHECK(!ValidPrefabPath((prefix + "a" + max.substr(prefix.size())).c_str()));
    // Nonterminated oversized string: bounded scan, no read beyond MAX_PATH+1.
    char unterminated[SC_PREFABS_MAX_PATH+1]; std::memset(unterminated, 'a', sizeof(unterminated));
    CHECK(!ValidPrefabPath(unterminated));
    const double pos[3]{1,2,3}, rot[4]{0,0,0,2}; uint64_t id = 99;
    CHECK(Forward(owner, path, 7, pos, rot, &id) == SCO_OK);
    CHECK(id == 55 && spawns == 1 && remembered == 1 && saved_owner == owner && saved_id == 55);
    fault = true; id = 99;
    CHECK(Forward(owner, path, 7, pos, rot, &id) == SCO_FAILED);
    CHECK(id == 0 && remembered == 2); fault = false;
    const auto baseline = spawns;
    auto refuses = [&](const PrefabProvider& p, sco_plugin* self, const char* content, uint64_t zone,
                       const double* position, const double* q, sco_result want) {
        id = 99; CHECK(SpawnOwnedPrefab(p, self, content, zone, position, q, &id) == want);
        CHECK(id == 0 && spawns == baseline);
    };
    thread = false; refuses(provider, owner, path, 7, pos, rot, SCO_WRONG_THREAD); thread = true;
    live = false; refuses(provider, owner, path, 7, pos, rot, SCO_BAD_ARG); live = true;
    refuses(provider, nullptr, path, 7, pos, rot, SCO_BAD_ARG);
    refuses(provider, reinterpret_cast<sco_plugin*>(uintptr_t(1)), path, 7, pos, rot, SCO_BAD_ARG);
    refuses(provider, owner, "../bad.socpak", 7, pos, rot, SCO_BAD_ARG);
    refuses(provider, owner, path, 0, pos, rot, SCO_BAD_ARG);
    refuses(provider, owner, path, 7, nullptr, rot, SCO_BAD_ARG);
    refuses(provider, owner, path, 7, pos, nullptr, SCO_BAD_ARG);
    CHECK(Forward(owner, path, 7, pos, rot, nullptr) == SCO_BAD_ARG);
    double bad[3]{0,0,std::numeric_limits<double>::infinity()}, qzero[4]{};
    refuses(provider, owner, path, 7, bad, rot, SCO_BAD_ARG);
    refuses(provider, owner, path, 7, pos, qzero, SCO_BAD_ARG);
    qzero[3] = std::numeric_limits<double>::quiet_NaN();
    refuses(provider, owner, path, 7, pos, qzero, SCO_BAD_ARG);
    refuses(provider, owner, path, 99, pos, rot, SCO_NOT_FOUND);
    ready = false; refuses(provider, owner, path, 7, pos, rot, SCO_UNAVAILABLE); ready = true;
    reservation = SCO_TOO_MANY; refuses(provider, owner, path, 7, pos, rot, SCO_TOO_MANY); reservation = SCO_OK;
    auto missing = provider; missing.remember = nullptr;
    refuses(missing, owner, path, 7, pos, rot, SCO_UNAVAILABLE);
    missing = provider; missing.spawn = nullptr; refuses(missing, owner, path, 7, pos, rot, SCO_UNAVAILABLE);
    zero_id = true; id = 99; CHECK(Forward(owner, path, 7, pos, rot, &id) == SCO_FAILED);
    CHECK(id == 0 && remembered == 2); zero_id = false;
    // C++ route: direct method/full extent/null/reopen refusal, physically short tails under ASan.
    sco_api api{}; api.size = sizeof(api); api.query_service = Query;
    sc_prefabs_v1 table{sizeof(table), 0, Forward}; offered = &table;
    sco::sdk::game::Prefabs wrapper;
    CHECK(wrapper.Open(&api, foreign) == SCO_OK);
    CHECK(wrapper.Spawn(path, 7, pos, rot, id) == SCO_OK && saved_owner == foreign);
    for (uint32_t extent = 8; extent < 16; ++extent) {
        void* short_table = std::malloc(extent); CHECK(short_table != nullptr);
        std::memcpy(short_table, &extent, sizeof(extent)); offered = short_table;
        CHECK(wrapper.Open(&api, owner) == SCO_OK);
        id = 99; CHECK(wrapper.Spawn(path, 7, pos, rot, id) == SCO_UNAVAILABLE && id == 0);
        std::free(short_table);
    }
    offered = &table; table.spawn = nullptr; CHECK(wrapper.Open(&api, owner) == SCO_OK);
    CHECK(wrapper.Spawn(path, 7, pos, rot, id) == SCO_UNAVAILABLE && id == 0);
    table.spawn = Forward; CHECK(wrapper.Open(&api, owner) == SCO_OK);
    query_result = SCO_NOT_FOUND; CHECK(wrapper.Open(&api, owner) == SCO_NOT_FOUND);
    CHECK(wrapper.Spawn(path, 7, pos, rot, id) == SCO_UNAVAILABLE && id == 0);
    // Owner-approved R1 default; all refusal/admission cases above still apply.
    CHECK(kPrefabConfirmed);
    // Readiness needs the spawn.prefabs rows: a build where they do not resolve must not report
    // the capability ON (the first placement would fail and latch it off until restart).
    CHECK(PrefabServiceReady(true, true, true, true, false));
    CHECK(!PrefabServiceReady(true, true, true, false, false));   // rows missing: never ready
    CHECK(!PrefabServiceReady(false, true, true, true, false));
    CHECK(!PrefabServiceReady(true, false, true, true, false));
    CHECK(!PrefabServiceReady(true, true, false, true, false));
    CHECK(!PrefabServiceReady(true, true, true, true, true));     // engine fault latch
    // Native refusals: unknown content is NOT_FOUND, absent rows are UNAVAILABLE without a latch,
    // anything else is a fault that latches.
    CHECK(ClassifyPrefabError("unknown prefab content path") == PrefabOutcome::NotFound);
    CHECK(ClassifyPrefabError("unknown entity class") == PrefabOutcome::NotFound);
    CHECK(ClassifyPrefabError(kPrefabUnavailable) == PrefabOutcome::Unavailable);
    CHECK(ClassifyPrefabError("prefab spawning isn't available") == PrefabOutcome::Unavailable);
    CHECK(ClassifyPrefabError("fault while spawning") == PrefabOutcome::Fault);
    CHECK(ClassifyPrefabError("the spawn batch gave no entity id") == PrefabOutcome::Fault);
    CHECK(ClassifyPrefabError(nullptr) == PrefabOutcome::Fault);
    // A stopped spawner also answers "unavailable"; only a running spawner means rows are missing.
    CHECK(PrefabUnavailableMeansRowsMissing(true, true, true));
    CHECK(!PrefabUnavailableMeansRowsMissing(false, true, true));
    CHECK(!PrefabUnavailableMeansRowsMissing(true, false, true));
    CHECK(!PrefabUnavailableMeansRowsMissing(true, true, false));
    // entities.cpp links into no test, so guard the wiring by source: PrefabReady must feed the
    // spawn.prefabs rows into PrefabServiceReady, and the rows check must test all three rows.
    {
        const std::string here = __FILE__;
        const std::string dir = here.substr(0, here.find_last_of("/\\") + 1) + "../src/game/services/";
        auto slurp = [](const std::string& f) { std::ifstream in(f, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); };
        const std::string ent = slurp(dir + "entities.cpp"), spn = slurp(dir + "spawn.cpp");
        CHECK(!ent.empty() && !spn.empty());
        CHECK(ent.find("PrefabServiceReady(g_started, g_canSpawn, SpawnReleaseHooked(), SpawnPrefabRowsResolved(), g_prefabFaulted)") != std::string::npos);
        CHECK(ent.find("if (PrefabUnavailableMeansRowsMissing(g_started, g_canSpawn, SpawnReleaseHooked()))") != std::string::npos);
        CHECK(spn.find("Ready() && g_sp.prefabSystem && g_sp.attrWriter && g_sp.attrTypeId") != std::string::npos);
        CHECK(spn.find("if (!SpawnPrefabRowsResolved()) return kPrefabUnavailable;") != std::string::npos);
    }
    std::printf("prefabs: %u passed, %u failed\n", passed, failed); return failed ? 1 : 0;
}
