// SPDX-License-Identifier: GPL-3.0-only
#include "sco/app.h"
#include "sco/runtime.h"
#include "sco/net/session.h"
#include "../src/app/owned_start.h"
#include "../src/owned_shutdown_access.h"
#include "../src/owned_registry_reservation.h"
#include "../src/api/internal.h"
#include "../src/api/owned_publication.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); std::abort(); } } while (0)
#include <chrono>
#include <future>
#include <string>
#include <thread>
static std::shared_ptr<sco::OwnedShutdown> domain;
static int stopCalls = 0, barrierCalls = 0, exits = 0;
static int queuedRuns = 0;
static void Queued(void*) { ++queuedRuns; }
static void StartupEvent(const char*, const void*, void*) {}
static void StartupRelease(const void*) {}
static void Exit(const char*, const void*, void*) { ++exits; domain->fault(); }
static bool Drain(void*) { ++barrierCalls; return domain->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(1)); }
static sco::Result ServiceOk(void*) { return sco::Result::Ok; }
static sco::Result ServiceFail(void*) { ++stopCalls; return sco::Result::Failed; }
static sco::Result ServiceFaultOk(void*) { ++stopCalls; domain->fault(); return sco::Result::Ok; }
static bool StartFixture(const sco::app::Platform& p) {
    if (!sco::OwnedShutdownHostAccess::begin_startup(*domain)) return false;
    auto transition = sco::OwnedShutdownHostAccess::transition(*domain);
    std::atomic<int> phase{0};
    if (!transition || !sco::detail::ReserveEmptyHostRegistry(domain, transition)) return false;
    if (!sco::detail::BindRuntimeStartup(domain, transition)) {
        sco::detail::ReleaseUnusedHostRegistryReservation(domain);
        return false;
    }
    return sco::app::detail::StartOwned(p, transition) &&
           transition.publish_and_open(phase, 0, 1);
}
int main(int argc, char** argv) {
    domain = std::make_shared<sco::OwnedShutdown>();
    sco::app::Platform p;
    p.pluginsEnabled = false;
    p.privateSession.enabled = false;
    p.shutdownOwner = domain;
    p.beforeDependentStop = Drain;
    CHECK(!sco::app::Start(p) && !sco::net::Started());
    if (argc > 1 && (std::string(argv[1]) == "startup-admission" || std::string(argv[1]) == "startup-publication")) {
        CHECK(sco::OwnedShutdownHostAccess::begin_startup(*domain));
        {
            auto transition = sco::OwnedShutdownHostAccess::transition(*domain);
            CHECK(transition && sco::detail::BindRuntimeStartup(domain, transition));
            const sco::detail::StartupPublish cap(domain, transition);
            sco::detail::SetGameThreadOwned(&cap);
            const void* table = nullptr;
            CHECK(sco::QueryService("fixture.service", 1u << 16, &table) == sco::Result::Unavailable && !table);
            if (std::string(argv[1]) == "startup-publication") {
                static int reentrantOwner, reentrantTable;
                CHECK(sco::ProvideService(&reentrantOwner, "rogue", "rogue.service", 1u << 16, &reentrantTable) == sco::Result::Unavailable);
                CHECK(sco::host::ProvideHostService("sco.rogue", 1u << 16, &reentrantTable) == sco::Result::Unavailable);
                CHECK(sco::Subscribe(&reentrantOwner, "test", StartupEvent, nullptr) == sco::Result::Unavailable);
                CHECK(sco::AddReleaseHook(StartupRelease) == sco::Result::Unavailable);
                CHECK(sco::detail::PublishHostOwned(&cap, "sco.fixture", 1u << 16, &reentrantTable) == sco::Result::Ok);
                CHECK(sco::QueryService("sco.fixture", 1u << 16, &table) == sco::Result::Unavailable && !table);
            }
            CHECK(sco::detail::PostOwned(Queued, nullptr, nullptr, nullptr) == sco::Result::Unavailable);
            CHECK(sco::DrainTasks() == 0 && queuedRuns == 0 && sco::QueuedTasks() == 0);
            sco::Result other = sco::Result::Ok;
            std::thread worker([&] { other = sco::detail::PostOwned(Queued, nullptr, nullptr, nullptr); });
            worker.join();
            CHECK(other == sco::Result::Unavailable);
        }
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "legacy-runtime-refusal") {
        static int legacyOwner, legacyTable;
        CHECK(sco::ProvideService(&legacyOwner, "fixture", "fixture.service", 1u << 16, &legacyTable) == sco::Result::Ok);
        CHECK(sco::OwnedShutdownHostAccess::begin_startup(*domain));
        auto transition = sco::OwnedShutdownHostAccess::transition(*domain);
        CHECK(transition && !sco::detail::BindRuntimeStartup(domain, transition));
        const void* table = nullptr;
        CHECK(sco::QueryService("fixture.service", 1u << 16, &table) == sco::Result::Ok && table == &legacyTable);
        return 0;
    }
    if (argc > 1 && (std::string(argv[1]) == "service-failure" || std::string(argv[1]) == "service-fault-ok")) {
        p.hostService.start = ServiceOk;
        p.hostService.tick = ServiceOk;
        p.hostService.stop = std::string(argv[1]) == "service-fault-ok" ? ServiceFaultOk : ServiceFail;
        CHECK(!StartFixture(p));
        CHECK(stopCalls == 0 && barrierCalls == 0 && !sco::net::Started() && !domain->open());
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "fault-during-drain") {
        CHECK(StartFixture(p));
        CHECK(sco::net::Started());
        std::promise<void> entered;
        std::thread fault([&entered] {
            auto lease = domain->try_read();
            CHECK(lease);
            entered.set_value();
            while (domain->open()) std::this_thread::yield();
            domain->fault();
        });
        entered.get_future().wait();
        CHECK(sco::app::Stop() == sco::Result::Failed);
        fault.join();
        CHECK(stopCalls == 0 && barrierCalls == 0 && sco::net::Started());
        CHECK(!domain->try_read() && !domain->ready());
        CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(p));
        return 0;
    }
    CHECK(StartFixture(p));
    int token = 0;
    CHECK(sco::Subscribe(&token, "game.exit", Exit, nullptr) == sco::Result::Ok);
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(exits == 0 && barrierCalls == 0 && !domain->ready());
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(!sco::app::Start(p));
}
