// SPDX-License-Identifier: GPL-3.0-only
#include "sco/app.h"
#include "sco/runtime.h"
#include "sco/net/session.h"
#include "sco_ui.h"
#include "sco_storage.h"
#include "../src/app/owned_start.h"
#include "../src/owned_shutdown_access.h"
#include "../src/owned_registry_reservation.h"
#include "../src/api/internal.h"
#include "../src/api/owned_publication.h"
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); std::abort(); } } while (0)
#include <chrono>
#include <future>
#include <string>
#include <thread>
namespace sco::ui::testing { void SetTableExitHook(void (*)(void*), void*) noexcept; }
namespace sco::storage::testing { void SetTableExitHook(void (*)(void*), void*) noexcept; }
struct TableGate {
    std::promise<void> entered;
    std::shared_future<void> release;
    static void Hold(void* context) {
        auto& gate = *static_cast<TableGate*>(context);
        gate.entered.set_value();
        gate.release.wait();
    }
};
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
// Exhaustive copied-table slot census; adding a tail member breaks compilation.
static_assert(offsetof(sco_ui_v1, register_tab) == 2 * sizeof(uint32_t));
static_assert(sizeof(sco_ui_v1) == offsetof(sco_ui_v1, register_tab) + 9 * sizeof(sco_ui_v1::register_tab));
static_assert(offsetof(sco_storage_v1, put) == 2 * sizeof(uint32_t));
static_assert(sizeof(sco_storage_v1) == offsetof(sco_storage_v1, put) + 17 * sizeof(sco_storage_v1::put));
static void CheckClosedTables(const sco_ui_v1& u, const sco_storage_v1& s) {
    char buffer[16] = {}; uint32_t size = sizeof(buffer), index = 4, flags = 5, count = 6;
    int64_t changes = 7; uint64_t cursor = 8; sco_sql_value value{};
    CHECK(u.register_tab(nullptr, "a", "b", 0, nullptr, nullptr) == SCO_UNAVAILABLE);
    CHECK(u.unregister_tab(nullptr, "a") == SCO_UNAVAILABLE);
    CHECK(u.set_badge(nullptr, "a", "b") == SCO_UNAVAILABLE);
    CHECK(u.register_overlay(nullptr, "a", nullptr, nullptr) == SCO_UNAVAILABLE);
    CHECK(u.unregister_overlay(nullptr, "a") == SCO_UNAVAILABLE);
    CHECK(u.bind_hotkey(nullptr, "a", "b", nullptr, 0) == SCO_UNAVAILABLE);
    CHECK(u.unbind_hotkey(nullptr, "a") == SCO_UNAVAILABLE);
    CHECK(u.normalize_chord("f1", buffer, &size) == SCO_UNAVAILABLE && size == 0);
    size = sizeof(buffer);
    CHECK(u.last_error(nullptr, buffer, &size) == SCO_UNAVAILABLE && size == sizeof(buffer));
    CHECK(s.put(nullptr, "a", "b", 1) == SCO_UNAVAILABLE);
    CHECK(s.get(nullptr, "a", buffer, &size) == SCO_UNAVAILABLE && size == sizeof(buffer));
    CHECK(s.del(nullptr, "a") == SCO_UNAVAILABLE);
    CHECK(s.next_key(nullptr, "a", "b", buffer, &size) == SCO_UNAVAILABLE && size == sizeof(buffer));
    CHECK(s.begin(nullptr) == SCO_UNAVAILABLE);
    CHECK(s.commit(nullptr) == SCO_UNAVAILABLE);
    CHECK(s.rollback(nullptr) == SCO_UNAVAILABLE);
    CHECK(s.exec(nullptr, "select 1", nullptr, 0, &changes) == SCO_UNAVAILABLE && changes == 7);
    CHECK(s.query(nullptr, "select 1", nullptr, 0, &cursor) == SCO_UNAVAILABLE && cursor == 8);
    CHECK(s.step(nullptr, cursor) == SCO_UNAVAILABLE);
    CHECK(s.column_count(nullptr, cursor, &count) == SCO_UNAVAILABLE && count == 6);
    CHECK(s.column(nullptr, cursor, 0, &value, buffer, &size) == SCO_UNAVAILABLE);
    CHECK(s.column_name(nullptr, cursor, 0, buffer, &size) == SCO_UNAVAILABLE);
    CHECK(s.close(nullptr, cursor) == SCO_UNAVAILABLE);
    CHECK(s.last_error(nullptr, buffer, &size) == SCO_UNAVAILABLE);
    CHECK(s.next_table(nullptr, nullptr, buffer, &size, &flags) == SCO_UNAVAILABLE && flags == 5);
    uint32_t typeSize = sizeof(buffer);
    CHECK(s.next_column(nullptr, "a", &index, buffer, &size, buffer, &typeSize, &flags) == SCO_UNAVAILABLE && index == 4);
}
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
    if (argc > 1 && (std::string(argv[1]) == "ui-call-drain" || std::string(argv[1]) == "storage-call-drain")) {
        p.dataRoot = std::filesystem::temp_directory_path() / "sco-owned-table-drain";
        CHECK(StartFixture(p));
        const void* uiRaw = nullptr; const void* storageRaw = nullptr;
        CHECK(sco::QueryService(SCO_UI_NAME, SCO_UI_VERSION_1_0, &uiRaw) == sco::Result::Ok && uiRaw);
        CHECK(sco::QueryService(SCO_STORAGE_NAME, SCO_STORAGE_VERSION_1_0, &storageRaw) == sco::Result::Ok && storageRaw);
        const auto ui = *static_cast<const sco_ui_v1*>(uiRaw);
        const auto storage = *static_cast<const sco_storage_v1*>(storageRaw);
        std::promise<void> release; TableGate gate{{}, release.get_future().share()};
        const bool uiCase = std::string(argv[1]) == "ui-call-drain";
        if (uiCase) sco::ui::testing::SetTableExitHook(TableGate::Hold, &gate);
        else sco::storage::testing::SetTableExitHook(TableGate::Hold, &gate);
        char out[32]{}; uint32_t size = sizeof(out);
        auto call = std::async(std::launch::async, [&] {
            return uiCase ? ui.normalize_chord("Shift + F1", out, &size) : storage.put(nullptr, "k", "v", 1);
        });
        auto entered = gate.entered.get_future();
        CHECK(entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        auto draining = std::async(std::launch::async, [&] {
            return domain->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(3));
        });
        CHECK(domain->wait_until_closed(std::chrono::steady_clock::now() + std::chrono::seconds(2)));
        CHECK(draining.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
        release.set_value();
        const auto result = call.get();
        CHECK(result == (uiCase ? SCO_OK : SCO_BAD_ARG));
        CHECK(draining.get());
        CHECK(!domain->try_read());
        CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(p));
        if (uiCase) sco::ui::testing::SetTableExitHook(nullptr, nullptr);
        else sco::storage::testing::SetTableExitHook(nullptr, nullptr);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "cached-tables") {
        p.dataRoot = std::filesystem::temp_directory_path() / "sco-owned-cached-table";
        CHECK(StartFixture(p));
        const void* uiRaw = nullptr;
        const void* storageRaw = nullptr;
        CHECK(sco::QueryService(SCO_UI_NAME, SCO_UI_VERSION_1_0, &uiRaw) == sco::Result::Ok && uiRaw);
        CHECK(sco::QueryService(SCO_STORAGE_NAME, SCO_STORAGE_VERSION_1_0, &storageRaw) == sco::Result::Ok && storageRaw);
        const auto ui = *static_cast<const sco_ui_v1*>(uiRaw);
        const auto storage = *static_cast<const sco_storage_v1*>(storageRaw);
        char result[32]{}; uint32_t size = sizeof(result);
        CHECK(ui.normalize_chord("Shift + F1", result, &size) == SCO_OK);
        sco::detail::SetRuntimeStopping(true);
        CHECK(domain->open());
        size = sizeof(result);
        CHECK(ui.normalize_chord("Shift + F1", result, &size) == SCO_UNAVAILABLE && size == 0);
        CheckClosedTables(ui, storage);
        sco::detail::SetRuntimeStopping(false);
        domain->fault();
        size = sizeof(result);
        CHECK(ui.normalize_chord("Shift + F1", result, &size) == SCO_UNAVAILABLE && size == 0);
        CheckClosedTables(ui, storage);
        CHECK(!domain->try_read() && sco::app::Stop() == sco::Result::Failed);
        CHECK(!sco::app::Start(p));
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
    if (argc > 1) { std::fprintf(stderr, "unknown owned app test mode: %s\n", argv[1]); return 2; }
    CHECK(StartFixture(p));
    int token = 0;
    CHECK(sco::Subscribe(&token, "game.exit", Exit, nullptr) == sco::Result::Ok);
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(exits == 0 && barrierCalls == 0 && !domain->ready());
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(!sco::app::Start(p));
}
