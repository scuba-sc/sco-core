// SPDX-License-Identifier: GPL-3.0-only
// game.world 1.1's free_camera_flag policy (src/game/services/world_free_cam.cpp) against a
// synthetic provider and the real caps registry: the default-OFF gate, the thread and argument
// checks, the zeroed output, and the 1.1 table tail. Nothing here is in-game evidence.
#include "../src/game/services/world_free_cam.h"
#include <sc_world.h>
#include <sco/caps.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

using sco::game::services::FreeCamFlag;
using sco::game::services::FreeCamFlagProvider;
using sco::game::services::kWorldCapFreeCamFlag;

namespace {

int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

bool gameThread = true;
int reads = 0;
uint32_t flagValue = 0;
sco_result readResult = SCO_OK;

bool OnThread() { return gameThread; }
sco_result ReadFlag(uint32_t* set, const char** why) {
    ++reads;
    if (readResult != SCO_OK) {
        *why = "fault while reading the free-camera flag";
        return readResult;
    }
    *set = flagValue;
    return SCO_OK;
}

FreeCamFlagProvider Provider(bool probed, bool rows) {
    FreeCamFlagProvider p;
    p.on_thread = OnThread;
    p.rows = rows;
    p.probed = probed;
    p.read_flag = ReadFlag;
    return p;
}

bool Reason(const char* needle) {
    sco::caps::Entry e[sco::caps::kMaxCaps];
    const size_t n = sco::caps::List(e, sco::caps::kMaxCaps);
    for (size_t i = 0; i < n && i < sco::caps::kMaxCaps; ++i)
        if (std::strcmp(e[i].name, kWorldCapFreeCamFlag) == 0) return std::strstr(e[i].reason, needle) != nullptr;
    return false;
}

FreeCamFlag* g_f = nullptr;
const char* g_why = "";
sco_result SvcFreeCameraFlag(uint32_t* out) { return g_f->Query(out, &g_why); }

// Production: rows OK, no in-game run yet. OFF, and the provider is never reached.
void TestDefaultOff() {
    FreeCamFlag f;
    f.Start(Provider(false, true));
    CHECK(!f.Ready() && !sco::caps::Has(kWorldCapFreeCamFlag));
    CHECK(Reason("OFF pending in-game confirmation"));
    uint32_t set = 7;
    const char* why = "";
    CHECK(f.Query(&set, &why) == SCO_UNAVAILABLE && set == 0);
    CHECK(std::strstr(why, "OFF pending in-game confirmation") != nullptr);
    CHECK(reads == 0);
    f.Stop();
}

// Rows not OK: OFF whatever probed says, with the rows reason.
void TestRowsNotOk() {
    FreeCamFlag f;
    f.Start(Provider(true, false));
    CHECK(!sco::caps::Has(kWorldCapFreeCamFlag) && Reason("rows not OK"));
    uint32_t set = 7;
    const char* why = "";
    CHECK(f.Query(&set, &why) == SCO_UNAVAILABLE && set == 0 && reads == 0);
    f.Stop();
}

// A probed provider (no production adapter is one): checks still come before the read.
void TestProbed() {
    FreeCamFlag f;
    g_f = &f;
    sc_world_v1 table{};
    table.size = sizeof(sc_world_v1);
    table.free_camera_flag = SvcFreeCameraFlag;
    f.Start(Provider(true, true));
    CHECK(f.Ready() && sco::caps::Has(kWorldCapFreeCamFlag));
    CHECK(table.size > offsetof(sc_world_v1, free_camera_flag));

    uint32_t set = 7;
    flagValue = 0;
    CHECK(table.free_camera_flag(&set) == SCO_OK && set == 0 && reads == 1);
    flagValue = 0x80;
    CHECK(table.free_camera_flag(&set) == SCO_OK && set == 1 && reads == 2);

    CHECK(table.free_camera_flag(nullptr) == SCO_BAD_ARG && std::strcmp(g_why, "out_set is NULL") == 0);
    gameThread = false;
    set = 7;
    CHECK(table.free_camera_flag(&set) == SCO_WRONG_THREAD && set == 0);
    CHECK(std::strcmp(g_why, "game thread only") == 0);
    gameThread = true;
    CHECK(reads == 2);

    readResult = SCO_FAILED;
    set = 7;
    CHECK(table.free_camera_flag(&set) == SCO_FAILED && set == 0);
    CHECK(std::strcmp(g_why, "fault while reading the free-camera flag") == 0);
    readResult = SCO_OK;

    f.Stop();
    CHECK(!f.Ready() && !sco::caps::Has(kWorldCapFreeCamFlag) && Reason("stopped"));
    const int before = reads;
    set = 7;
    CHECK(table.free_camera_flag(&set) == SCO_UNAVAILABLE && set == 0 && reads == before);
    CHECK(std::strcmp(g_why, "game.world is stopped") == 0);
    g_f = nullptr;
}

// Never started: no provider, so it is refused before anything could be read.
void TestNotStarted() {
    FreeCamFlag f;
    uint32_t set = 7;
    const char* why = "";
    CHECK(f.Query(&set, &why) == SCO_WRONG_THREAD && set == 0);
}

// The 1.1 tail: 1.0's members keep their offsets, the new member sits after last_error.
void TestTableTail() {
    static_assert(SC_WORLD_VERSION_1_1 == ((1u << 16) | 1u), "world version");
    static_assert(offsetof(sc_world_v1, last_error) == 24, "frozen error member");
    static_assert(offsetof(sc_world_v1, free_camera_flag) == 32 && sizeof(sc_world_v1) == 40,
                  "free-camera table tail");
}

}  // namespace

int main() {
    TestDefaultOff();
    TestRowsNotOk();
    TestProbed();
    TestNotStarted();
    TestTableTail();
    std::printf("game.world free_camera_flag policy: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
