// SPDX-License-Identifier: GPL-3.0-only
#include "sco/app.h"
#include "sco/host.h"
#include <cstdio>
#include <string>
#include <vector>
static std::vector<std::string> calls;
static bool refuse = false;
static sco::Result stopResult = sco::Result::Failed;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static const uint32_t table[] = {8, 0};
static sco::Result Start(void*) {
    calls.emplace_back("start");
    return refuse ? sco::Result::Unavailable : sco::host::ProvideGameService("game.synthetic", 0x00010000u, table);
}
static sco::Result Tick(void*) { calls.emplace_back("pump"); return sco::Result::Ok; }
static sco::Result Stop(void*) { calls.emplace_back("stop"); stopResult = sco::host::WithdrawGameService("game.synthetic"); return stopResult; }
static const sco_plugin_info* Query() {
    static const sco_plugin_info info{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "probe", "1.0.0", "test"};
    return &info;
}
static void Event(const char*, const void*, void*) { calls.emplace_back("plugin.tick"); }
static sco_result Load(const sco_api* api, sco_plugin* self) {
    calls.emplace_back("plugin.load");
    const void* found = nullptr;
    const auto result = api->query_service("game.synthetic", 0x00010000u, &found);
    CHECK(refuse ? result != SCO_OK : (result == SCO_OK && found == table));
    return api->subscribe(self, "tick", Event, nullptr);
}
static void Unload() { calls.emplace_back("plugin.unload"); }
int main() {
    const sco::plugins::Builtin plugin{"probe", Query, Load, Unload};
    sco::app::Platform p; p.builtins = &plugin; p.nBuiltins = 1;
    p.hostService = {nullptr, Start, Tick, Stop};
    CHECK(sco::app::Start(p)); sco::app::Tick(1); sco::app::Stop();
    CHECK(calls == std::vector<std::string>({"start", "plugin.load", "pump", "plugin.tick", "plugin.unload", "stop"}));
    CHECK(stopResult == sco::Result::Ok);
    calls.clear(); refuse = true;
    CHECK(sco::app::Start(p)); sco::app::Tick(2); sco::app::Stop();
    CHECK(calls == std::vector<std::string>({"start", "plugin.load", "plugin.tick", "plugin.unload"}));
    std::printf("host service lifecycle: %d failures\n", failures);
    return failures ? 1 : 0;
}
