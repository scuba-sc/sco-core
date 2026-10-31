// SPDX-License-Identifier: GPL-3.0-only
#include "sco/app.h"
#include "sco/runtime.h"
#include <cassert>
#include <chrono>
#include <string>
static std::shared_ptr<sco::OwnedShutdown> domain;
static int unloads = 0;
static const sco_plugin_info* Query() {
    static const sco_plugin_info p{sizeof(p), SCO_API_MAJOR, SCO_API_MINOR, "test", "1", "test"};
    return &p;
}
static sco_result Load(const sco_api*, sco_plugin*) { return SCO_OK; }
static void Unload() { ++unloads; }
static void Exit(const char*, const void*, void*) { domain->fault(); }
static bool Drain(void*) { return domain->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(1)); }
int main() {
    domain = std::make_shared<sco::OwnedShutdown>();
    sco::app::Platform p;
    p.pluginsEnabled = false;
    p.shutdownOwner = domain;
    p.beforeDependentStop = Drain;
    const sco::plugins::Builtin builtin{"test", Query, Load, Unload};
    p.builtins = &builtin; p.nBuiltins = 1;
    assert(sco::app::Start(p));
    int token = 0;
    assert(sco::Subscribe(&token, "game.exit", Exit, nullptr) == sco::Result::Ok);
    assert(sco::app::Stop() == sco::Result::Failed);
    assert(unloads == 0 && !domain->ready());
    assert(sco::app::Stop() == sco::Result::Failed);
    assert(!sco::app::Start(p));
}
