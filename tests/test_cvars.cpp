// SPDX-License-Identifier: GPL-3.0-only
#include <sc_cvars.h>
#include "../src/game/services/cvars.h"
#include "sco/host.h"
#include <cstdio>
#include <thread>
#include "sco/plugins.h"
#include "sco/caps.h"
#include "../src/api/internal.h"
using sco::Result;
namespace C = sco::game::services;
namespace sco::game::services { bool SpawnReleaseHooked() { return false; } }
static int pass = 0, fail = 0;
#define CHECK(x) do { if (x) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static uint32_t values[4] = {1, 0, 1, 0};
static const sc_cvars_v1* table = nullptr;
static sco_plugin* self = nullptr;
static bool reenter = false;
static sco_result nested = SCO_OK;
static sco_result read_result = SCO_OK;
static unsigned reads = 0;
static sco_result Read(void*, uint32_t index, uint32_t* out) {
    ++reads;
    if (reenter) { reenter = false; uint32_t v = 8;
        nested = table->get_bool(self, "g_squadron_frontend", SC_CVAR_BOOL, &v); }
    *out = values[index]; return read_result;
}
int main() {
    sco::SetGameThread();
    self = sco::host::NewPlugin("cvars_test");
    const auto* api = sco::host::BuildApi({"synthetic cvars"});
    C::CvarsPort port{}; port.verified = true; port.offline_admitted = true; port.read_bool = Read;
    CHECK(C::StartCvars() == Result::Ok); // unverified production default
    const void* absent_raw = nullptr;
    CHECK(api->query_service(SC_CVARS_NAME, SC_CVARS_VERSION, &absent_raw) == SCO_OK);
    uint32_t absent_value = 9;
    CHECK(static_cast<const sc_cvars_v1*>(absent_raw)->get_bool(self,
        "g_squadron_frontend", SC_CVAR_BOOL, &absent_value) == SCO_UNAVAILABLE && absent_value == 9);
    C::StopCvars();
    auto offline_port = port; offline_port.offline_admitted = false;
    CHECK(C::SetCvarsPort(offline_port) == Result::Ok);
    CHECK(C::StartCvars() == Result::Ok);
    CHECK(static_cast<const sc_cvars_v1*>(absent_raw)->get_bool(self,
        "g_squadron_frontend", SC_CVAR_BOOL, &absent_value) == SCO_UNAVAILABLE);
    C::StopCvars();
    CHECK(C::SetCvarsPort(port) == Result::Ok);
    CHECK(C::StartCvars() == Result::Ok);
    const void* raw = nullptr;
    CHECK(api->query_service(SC_CVARS_NAME, SC_CVARS_VERSION, &raw) == SCO_OK);
    table = static_cast<const sc_cvars_v1*>(raw);
    uint32_t value = 9;
    CHECK(table && table->get_bool(self, "i_target_selector.targeting2_enabled", SC_CVAR_BOOL, &value) == SCO_OK);
    CHECK(value == 1);
    reenter = true;
    CHECK(table->get_bool(self, "g_squadron_frontend", SC_CVAR_BOOL, &value) == SCO_OK);
    CHECK(nested == SCO_FAILED);
    const char* names[] = {"i_target_selector.targeting2_enabled", "pl_lensdisplay.minimap_enabled",
        "pl_lensdisplay.greebles_enabled", "g_squadron_frontend"};
    for (unsigned i = 0; i != 4; ++i) {
        CHECK(table->get_bool(self, names[i], SC_CVAR_BOOL, &value) == SCO_OK);
        CHECK(value == values[i]);
    }
    const unsigned before = reads; value = 9;
    CHECK(table->get_bool(self, "g_squadron_frontend 1", SC_CVAR_BOOL, &value) == SCO_BAD_ARG);
    CHECK(table->get_bool(self, "G_squadron_frontend", SC_CVAR_BOOL, &value) == SCO_BAD_ARG);
    CHECK(table->get_bool(self, "net_enable", SC_CVAR_BOOL, &value) == SCO_BAD_ARG);
    CHECK(table->get_bool(self, nullptr, SC_CVAR_BOOL, &value) == SCO_BAD_ARG);
    CHECK(table->get_bool(self, names[0], 2, &value) == SCO_BAD_ARG);
    CHECK(table->get_bool(self, names[0], SC_CVAR_BOOL, nullptr) == SCO_BAD_ARG);
    CHECK(table->get_bool(nullptr, names[0], SC_CVAR_BOOL, &value) == SCO_BAD_ARG);
    CHECK(table->get_bool(reinterpret_cast<sco_plugin*>(1), names[0], SC_CVAR_BOOL, &value) == SCO_BAD_ARG);
    std::thread worker([&] { CHECK(table->get_bool(self, names[0], SC_CVAR_BOOL, &value) == SCO_WRONG_THREAD); });
    worker.join();
    CHECK(reads == before && value == 9);
    sco::caps::Set("game.cvars.read", false, "synthetic absent capability");
    CHECK(table->get_bool(self, names[0], SC_CVAR_BOOL, &value) == SCO_UNAVAILABLE);
    sco::caps::Set("game.cvars.read", true, "synthetic");
    sco::detail::SetRuntimeStopping(true);
    CHECK(table->get_bool(self, names[0], SC_CVAR_BOOL, &value) == SCO_UNAVAILABLE);
    sco::detail::SetRuntimeStopping(false);
    read_result = SCO_UNAVAILABLE;
    CHECK(table->get_bool(self, names[0], SC_CVAR_BOOL, &value) == SCO_UNAVAILABLE && value == 9);
    read_result = SCO_OK; values[0] = 2;
    CHECK(table->get_bool(self, names[0], SC_CVAR_BOOL, &value) == SCO_FAILED && value == 9);
    values[0] = 1;
    sco::plugins::Options opts; opts.enabled = true;
    auto plugins = sco::plugins::Discover(CVARS_PLUGIN_ROOT, opts);
    CHECK(plugins.size() == 1);
    auto* probe = sco::host::NewPlugin("cvars_probe");
    CHECK(sco::plugins::LoadNative(plugins[0], api, probe, opts));
    CHECK(plugins[0].state == sco::plugins::State::Loaded);
    sco::plugins::UnloadNative(plugins[0]);
    CHECK(plugins[0].state == sco::plugins::State::Unloaded);
    CHECK(table->get_bool(probe, names[0], SC_CVAR_BOOL, &value) == SCO_BAD_ARG);
    read_result = SCO_CRASHED;
    CHECK(table->get_bool(self, names[0], SC_CVAR_BOOL, &value) == SCO_CRASHED && value == 9);
    const unsigned after_fault = reads; read_result = SCO_OK;
    CHECK(table->get_bool(self, names[0], SC_CVAR_BOOL, &value) == SCO_UNAVAILABLE);
    CHECK(reads == after_fault);
    C::StopCvars();
    CHECK(C::SetCvarsPort(port) == Result::Failed);
    CHECK(C::StartCvars() == Result::Failed);
    CHECK(table->get_bool(self, names[0], SC_CVAR_BOOL, &value) == SCO_UNAVAILABLE);
    CHECK(api->query_service(SC_CVARS_NAME, SC_CVARS_VERSION, &raw) == SCO_NOT_FOUND);
    std::printf("cvars: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
