// SPDX-License-Identifier: GPL-3.0-only
#include "sco/app.h"
#include "sco/runtime.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); std::abort(); } } while (0)
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
static sco::Result ServiceOk(void*) { return sco::Result::Ok; }
static sco::Result ServiceFail(void*) { return sco::Result::Failed; }
int main(int argc, char** argv) {
    domain = std::make_shared<sco::OwnedShutdown>();
    sco::app::Platform p;
    p.pluginsEnabled = false;
    p.shutdownOwner = domain;
    p.beforeDependentStop = Drain;
    if (argc > 1 && std::string(argv[1]) == "service-failure") {
        p.hostService.start = ServiceOk;
        p.hostService.tick = ServiceOk;
        p.hostService.stop = ServiceFail;
        CHECK(sco::app::Start(p));
        CHECK(sco::app::Stop() == sco::Result::Failed);
        CHECK(!domain->ready());
        CHECK(sco::app::Stop() == sco::Result::Failed);
        CHECK(!sco::app::Start(p));
        return 0;
    }
    const sco::plugins::Builtin builtin{"test", Query, Load, Unload};
    p.builtins = &builtin; p.nBuiltins = 1;
    CHECK(sco::app::Start(p));
    int token = 0;
    CHECK(sco::Subscribe(&token, "game.exit", Exit, nullptr) == sco::Result::Ok);
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(unloads == 0 && !domain->ready());
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(!sco::app::Start(p));
}
