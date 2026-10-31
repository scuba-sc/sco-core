// SPDX-License-Identifier: GPL-3.0-only
#include "sco/app.h"
#include "sco/runtime.h"
#include "sco/net/session.h"
#ifdef SCO_GAME_SERVICES
#include "sco/game/services.h"
#endif
#include "sco_ui.h"
#include "sco_storage.h"
#include "sco_net.h"
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
#include <atomic>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
namespace sco::ui::testing { void SetTableExitHook(void (*)(void*), void*) noexcept; }
namespace sco::storage::testing { void SetTableExitHook(void (*)(void*), void*) noexcept; }
namespace sco::net::testing {
void SetTableExitHook(void (*)(void*), void*) noexcept;
const sco_net_v1* TableUnchecked() noexcept;
void SetObservationHook(void (*)(void*), void*) noexcept;
void SetWaitPredicateHook(void (*)(void*), void*) noexcept;
bool RawStarted();
void SeedObservations(void (*)(const void*));
bool RawObservationsRetained(void (*)(const void*));
}
struct TableGate {
    std::promise<void> entered;
    std::shared_future<void> release;
    std::shared_ptr<sco::OwnedShutdown> faultOwner;
    static void Hold(void* context) {
        auto& gate = *static_cast<TableGate*>(context);
        gate.entered.set_value();
        gate.release.wait();
        if (gate.faultOwner) gate.faultOwner->fault();
    }
};
static std::shared_ptr<sco::OwnedShutdown> domain;
static int stopCalls = 0, barrierCalls = 0, exits = 0;
static int queuedRuns = 0;
static bool UnexpectedNativeBarrier(void*) { std::abort(); }
static void ThrowCapabilities() { throw std::runtime_error("startup fault"); }
static void ThrowObservation(void*) { throw std::runtime_error("observation fault"); }
struct WaitSignal {
    std::promise<void> entered;
    std::atomic<bool> signaled{false};
    static void Signal(void* p) {
        auto& self = *static_cast<WaitSignal*>(p);
        if (!self.signaled.exchange(true)) self.entered.set_value();
    }
};
static bool RefuseCleanup(void* p) { ++*static_cast<int*>(p); return false; }
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
static_assert(offsetof(sco_net_v1, is_active) == 2 * sizeof(uint32_t));
static_assert(sizeof(sco_net_v1) == offsetof(sco_net_v1, is_active) + 7 * sizeof(sco_net_v1::is_active));
static void CheckClosedNet(const sco_net_v1& net) {
    uint32_t count = 7;
    CHECK(net.is_active() == 0 && net.self_peer() == 0);
    CHECK(net.get_peers(nullptr, &count) == SCO_UNAVAILABLE && count == 7);
    char name[8]{};
    CHECK(net.get_peer_name(0, name, sizeof(name)) == SCO_UNAVAILABLE);
    CHECK(net.send_channel(nullptr, "a.b", nullptr, 0) == SCO_UNAVAILABLE);
    CHECK(net.register_channel(nullptr, "a.b", 0, 1, nullptr, nullptr) == SCO_UNAVAILABLE);
    CHECK(net.unregister_channel(nullptr, "a.b") == SCO_UNAVAILABLE);
}
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
    if (argc > 1 && std::string(argv[1]) == "observer-startup-seeded") {
        CHECK(sco::OwnedShutdownHostAccess::begin_startup(*domain));
        auto transition = sco::OwnedShutdownHostAccess::transition(*domain);
        CHECK(transition && sco::detail::BindRuntimeStartup(domain, transition));
        sco::net::testing::SeedObservations(StartupRelease);
        const auto checkClosed = [] {
            { sco::detail::RuntimeObservation observation; CHECK(!observation); }
            CHECK(!sco::net::Started() && !sco::net::HasOwnerReleaseDispatcher(StartupRelease));
            CHECK(!sco::net::WaitForDelivery(0) && sco::net::GetState() == sco::net::State::Idle);
            CHECK(sco::net::LastReason().empty() && sco::net::BoundPort() == 0);
            CHECK(sco::net::GetServiceStats().quotaRefused == 0);
        };
        checkClosed();
        std::thread worker(checkClosed);
        worker.join();
        CHECK(sco::net::testing::RawObservationsRetained(StartupRelease));
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "observer-prebind") {
        std::promise<void> release; TableGate gate{{}, release.get_future().share(), {}};
        sco::net::testing::SetObservationHook(TableGate::Hold, &gate);
        auto call = std::async(std::launch::async, [] { return sco::net::Started(); });
        CHECK(gate.entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        CHECK(sco::OwnedShutdownHostAccess::begin_startup(*domain));
        {
            auto transition = sco::OwnedShutdownHostAccess::transition(*domain);
            CHECK(transition && !sco::detail::BindRuntimeStartup(domain, transition));
        }
        release.set_value();
        CHECK(!call.get());
        sco::net::testing::SetObservationHook(nullptr, nullptr);
        auto transition = sco::OwnedShutdownHostAccess::transition(*domain);
        CHECK(transition && sco::detail::BindRuntimeStartup(domain, transition));
        return 0;
    }
#ifdef SCO_RUNTIME_OBSERVER_TESTING
    if (argc > 1 && std::string(argv[1]) == "observer-close-before-read") {
        CHECK(StartFixture(p));
        std::promise<void> release; TableGate gate{{}, release.get_future().share(), {}};
        sco::detail::SetObservationEntryHookForTest(TableGate::Hold, &gate);
        auto call = std::async(std::launch::async, [] { return sco::net::Started(); });
        CHECK(gate.entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        CHECK(domain->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(2)));
        release.set_value();
        CHECK(!call.get());
        sco::detail::SetObservationEntryHookForTest(nullptr, nullptr);
        CHECK(sco::net::testing::RawStarted());
        CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(p));
        return 0;
    }
#endif
    if (argc > 1 && std::string(argv[1]) == "start-success") {
        sco::app::Platform legacy;
        legacy.pluginsEnabled = false;
        legacy.privateSession.enabled = false;
        int priorContext = 0;
        legacy.beforeDependentStopContext = &priorContext;
        legacy.beforeDependentStop = [](void* p) { ++*static_cast<int*>(p); return true; };
        CHECK(sco::app::StartDetailed(legacy) == sco::app::StartResult::Started);
        auto refused = legacy;
        refused.gameServices = true;
        CHECK(sco::app::StartDetailed(refused) == sco::app::StartResult::RefusedBeforeExposure);
        CHECK(priorContext == 0);
        CHECK(sco::app::Stop() == sco::Result::Ok);
        CHECK(priorContext == 1);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "start-retained") {
        sco::app::Platform partial;
        sco::Image image{};
        auto token = std::make_shared<int>(1);
        std::weak_ptr<int> retained = token;
        int callbackContext = 0;
        partial.image = &image;
        partial.imageOwner = partial.resolverOwner = token;
        partial.setCapabilities = ThrowCapabilities;
        partial.beforeDependentStop = RefuseCleanup;
        partial.beforeDependentStopContext = &callbackContext;
        CHECK(sco::app::StartDetailed(partial) == sco::app::StartResult::RetainDependencies);
        CHECK(callbackContext == 1);
        token.reset(); partial.imageOwner.reset(); partial.resolverOwner.reset();
        CHECK(!retained.expired());
        CHECK(sco::app::Stop() == sco::Result::Failed);
        CHECK(sco::app::StartDetailed(partial) == sco::app::StartResult::RetainDependencies);
        return 0;
    }
#ifdef SCO_GAME_SERVICES
    if (argc > 1 && std::string(argv[1]) == "direct-native-refusal") {
        CHECK(!sco::game::services::Started());
        CHECK(sco::game::services::Start() == sco::Result::Unavailable);
        CHECK(!sco::game::services::Started());
        // No game thread is installed: even an empty Stop is refused there.
        CHECK(sco::game::services::Stop() == sco::Result::WrongThread);
        CHECK(sco::OwnedShutdownHostAccess::begin_startup(*domain));
        {
            auto transition = sco::OwnedShutdownHostAccess::transition(*domain);
            CHECK(transition && !sco::detail::BindRuntimeStartup(domain, transition));
        }
        sco::SetGameThread();
        CHECK(sco::game::services::Stop() == sco::Result::Ok);
        CHECK(sco::game::services::Stop() == sco::Result::Ok);
        CHECK(sco::game::services::FinalizeStopAfterQuiescence() == sco::Result::Ok);
        sco::Result other = sco::Result::Ok;
        std::thread worker([&] { other = sco::game::services::Stop(); });
        worker.join();
        CHECK(other == sco::Result::WrongThread && !sco::game::services::Started());
        CHECK(!sco::AcquireHostSignatureRegistry());
        return 0;
    }
#endif
    if (argc > 1 && std::string(argv[1]) == "native-service-coverage-unavailable") {
        std::puts("SKIP: native game-service table fixture requires an enabled production service path");
        return 77;
    }
    if (argc > 1 && std::string(argv[1]) == "native-refusal") {
        sco::app::Platform native;
        native.gameServices = true;
        native.gameReadersQuiesced = UnexpectedNativeBarrier;
        CHECK(sco::app::StartDetailed(native) == sco::app::StartResult::RefusedBeforeExposure);
        CHECK(!sco::app::Start(native));
        CHECK(!sco::net::Started() && !sco::AcquireHostSignatureRegistry());
        return 0;
    }
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
            CHECK(!sco::net::Table());
            CheckClosedNet(*sco::net::testing::TableUnchecked());
            CHECK(!sco::net::Started() && !sco::net::HasOwnerReleaseDispatcher(StartupRelease));
            CHECK(!sco::net::WaitForDelivery(0) && sco::net::GetState() == sco::net::State::Idle);
            CHECK(sco::net::LastReason().empty() && sco::net::BoundPort() == 0);
            CHECK(sco::net::GetServiceStats().quotaRefused == 0);
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
    if (argc > 1 && std::string(argv[1]) == "provider-refusal") {
        CHECK(StartFixture(p));
        static int owner, table;
        CHECK(sco::ProvideService(&owner, "fixture", "fixture.service", 1u << 16, &table) == sco::Result::Unavailable);
        const void* result = nullptr;
        CHECK(sco::QueryService("fixture.service", 1u << 16, &result) == sco::Result::NotFound && !result);
        CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(p));
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "observer-seeded-refusal") {
        CHECK(StartFixture(p));
        sco::net::testing::SeedObservations(StartupRelease);
        CHECK(sco::net::Started() && sco::net::HasOwnerReleaseDispatcher(StartupRelease));
        CHECK(sco::net::WaitForDelivery(0) && sco::net::GetState() == sco::net::State::Hosting);
        CHECK(sco::net::LastReason() == "fixture" && sco::net::BoundPort() == 1234);
        CHECK(sco::net::GetServiceStats().quotaRefused == 3);
        sco::detail::SetRuntimeStopping(true);
        CHECK(domain->open());
        CHECK(!sco::net::Started() && !sco::net::HasOwnerReleaseDispatcher(StartupRelease));
        CHECK(!sco::net::WaitForDelivery(0) && sco::net::GetState() == sco::net::State::Idle);
        CHECK(sco::net::LastReason().empty() && sco::net::BoundPort() == 0);
        CHECK(sco::net::GetServiceStats().quotaRefused == 0);
        CHECK(sco::net::testing::RawObservationsRetained(StartupRelease));
        sco::detail::SetRuntimeStopping(false);
        domain->fault();
        CHECK(!sco::net::Started() && !sco::net::HasOwnerReleaseDispatcher(StartupRelease));
        CHECK(!sco::net::WaitForDelivery(0) && sco::net::GetState() == sco::net::State::Idle);
        CHECK(sco::net::LastReason().empty() && sco::net::BoundPort() == 0);
        CHECK(sco::net::GetServiceStats().quotaRefused == 0);
        CHECK(sco::net::testing::RawObservationsRetained(StartupRelease));
        CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(p));
        return 0;
    }
    if (argc > 1 && (std::string(argv[1]) == "observer-dispatcher-drain" ||
                     std::string(argv[1]) == "observer-state-drain" ||
                     std::string(argv[1]) == "observer-reason-drain" ||
                     std::string(argv[1]) == "observer-port-drain" ||
                     std::string(argv[1]) == "observer-stats-drain")) {
        CHECK(StartFixture(p));
        sco::net::testing::SeedObservations(StartupRelease);
        std::promise<void> release; TableGate gate{{}, release.get_future().share(), {}};
        sco::net::testing::SetObservationHook(TableGate::Hold, &gate);
        const std::string mode = argv[1];
        auto call = std::async(std::launch::async, [&]() -> uint64_t {
            if (mode == "observer-dispatcher-drain") return sco::net::HasOwnerReleaseDispatcher(StartupRelease) ? 1 : 0;
            if (mode == "observer-state-drain") return static_cast<uint64_t>(sco::net::GetState());
            if (mode == "observer-reason-drain") return sco::net::LastReason().size();
            if (mode == "observer-port-drain") return sco::net::BoundPort();
            return sco::net::GetServiceStats().quotaRefused;
        });
        CHECK(gate.entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        auto drain = std::async(std::launch::async, [&] {
            return domain->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(3));
        });
        CHECK(domain->wait_until_closed(std::chrono::steady_clock::now() + std::chrono::seconds(2)));
        CHECK(drain.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
        release.set_value();
        const uint64_t expected = mode == "observer-dispatcher-drain" ? 1 :
            mode == "observer-state-drain" ? static_cast<uint64_t>(sco::net::State::Hosting) :
            mode == "observer-reason-drain" ? 7 : mode == "observer-port-drain" ? 1234 : 3;
        CHECK(call.get() == expected && drain.get());
        sco::net::testing::SetObservationHook(nullptr, nullptr);
        CHECK(sco::net::testing::RawObservationsRetained(StartupRelease));
        CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(p));
        return 0;
    }
    if (argc > 1 && (std::string(argv[1]) == "observer-call-drain" ||
                     std::string(argv[1]) == "observer-wait-drain" ||
                     std::string(argv[1]) == "observer-exception")) {
        CHECK(StartFixture(p));
        if (std::string(argv[1]) == "observer-exception") {
            sco::net::testing::SetObservationHook(ThrowObservation, nullptr);
            bool threw = false;
            try { (void)sco::net::Started(); } catch (const std::runtime_error&) { threw = true; }
            CHECK(threw);
            sco::net::testing::SetObservationHook(nullptr, nullptr);
            CHECK(domain->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(2)));
        } else {
            std::promise<void> release; TableGate gate{{}, release.get_future().share(), {}};
            const bool waiting = std::string(argv[1]) == "observer-wait-drain";
            WaitSignal signal;
            if (waiting) sco::net::testing::SetWaitPredicateHook(WaitSignal::Signal, &signal);
            else sco::net::testing::SetObservationHook(TableGate::Hold, &gate);
            auto call = std::async(std::launch::async, [&] {
                return waiting ? sco::net::WaitForDelivery(2000) : sco::net::Started();
            });
            if (waiting) {
                CHECK(signal.entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
                // RawStarted takes g_lock; returning proves the waiter released it.
                CHECK(sco::net::testing::RawStarted());
                CHECK(call.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
            } else {
                CHECK(gate.entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
            }
            auto drain = std::async(std::launch::async, [&] {
                return domain->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(4));
            });
            CHECK(domain->wait_until_closed(std::chrono::steady_clock::now() + std::chrono::seconds(2)));
            CHECK(drain.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
            if (!waiting) release.set_value();
            CHECK(call.get() == !waiting);
            CHECK(drain.get());
            sco::net::testing::SetObservationHook(nullptr, nullptr);
            sco::net::testing::SetWaitPredicateHook(nullptr, nullptr);
        }
        CHECK(!sco::net::Started() && sco::net::testing::RawStarted());
        CHECK(!sco::net::HasOwnerReleaseDispatcher(StartupRelease));
        CHECK(!sco::net::WaitForDelivery(0));
        CHECK(sco::net::GetState() == sco::net::State::Idle);
        CHECK(sco::net::LastReason().empty() && sco::net::BoundPort() == 0);
        CHECK(sco::net::GetServiceStats().quotaRefused == 0);
        CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(p));
        return 0;
    }
    if (argc > 1 && (std::string(argv[1]) == "net-cached-table" || std::string(argv[1]) == "net-call-drain" ||
                     std::string(argv[1]) == "net-active-drain" || std::string(argv[1]) == "net-peer-drain" ||
                     std::string(argv[1]) == "net-fault-drain")) {
        CHECK(StartFixture(p));
        const void* raw = nullptr;
        CHECK(sco::QueryService(SCO_NET_NAME, SCO_NET_VERSION_1_0, &raw) == sco::Result::Ok && raw);
        const auto net = *static_cast<const sco_net_v1*>(raw);
        if (std::string(argv[1]) != "net-cached-table") {
            std::promise<void> release; TableGate gate{{}, release.get_future().share(), {}};
            if (std::string(argv[1]) == "net-fault-drain") gate.faultOwner = domain;
            sco::net::testing::SetTableExitHook(TableGate::Hold, &gate);
            uint32_t count = 0;
            const std::string mode = argv[1];
            auto call = std::async(std::launch::async, [&]() -> uint64_t {
                if (mode == "net-active-drain") return static_cast<uint64_t>(net.is_active());
                if (mode == "net-peer-drain") return net.self_peer();
                return static_cast<uint64_t>(net.get_peers(nullptr, &count));
            });
            CHECK(gate.entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
            auto drain = std::async(std::launch::async, [&] {
                return domain->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(3));
            });
            CHECK(domain->wait_until_closed(std::chrono::steady_clock::now() + std::chrono::seconds(2)));
            CHECK(drain.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
            release.set_value();
            CHECK(call.get() == 0 && count == 0);
            CHECK(drain.get() == (std::string(argv[1]) != "net-fault-drain"));
            sco::net::testing::SetTableExitHook(nullptr, nullptr);
        } else {
            uint32_t count = 0;
            CHECK(net.get_peers(nullptr, &count) == SCO_OK && count == 0);
            sco::detail::SetRuntimeStopping(true);
            CHECK(domain->open());
            CheckClosedNet(net);
            sco::detail::SetRuntimeStopping(false);
            domain->fault();
        }
        CheckClosedNet(net);
        CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(p));
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
        std::promise<void> release; TableGate gate{{}, release.get_future().share(), {}};
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
        CHECK(stopCalls == 0 && barrierCalls == 0 && !sco::net::Started() && sco::net::testing::RawStarted());
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
