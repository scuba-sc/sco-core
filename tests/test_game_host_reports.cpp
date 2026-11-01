// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// Same production game-host route, synthetic bytes and executable-pinned code.
#include "sco/game/host.h"
#include "../src/game/host_testing.h"
#include "owned_shutdown_test_access.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace gh = sco::game::host;
using D = gh::DependencyDisposition;
using R = sco::Result;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
static std::atomic<int> destroyed{0}, inspections{0};
// One actual ordinary allocation inside Options assignment can fail. This
// executable-only replacement is not a production allocator or aligned proof.
static thread_local bool optionsAllocationFired = false;
static thread_local bool reenterAllocation = false;
static thread_local bool failNextOrdinaryAllocation = false, ordinaryAllocationFired = false;
static thread_local gh::HostReport allocationReport, allocationStatus;
static thread_local bool statusAllocation = false, failAfterStatus = false;
static thread_local bool pauseRootAllocation = false;
static std::atomic<bool> rootAllocationReached{false}, rootAllocationRelease{false};
static thread_local sco::OwnedShutdown* helperAllocationOwner = nullptr;
static thread_local bool helperAllocationObserved = false;
#if defined(_MSC_VER)
#define HOST_REPORT_NOINLINE __declspec(noinline)
#else
#define HOST_REPORT_NOINLINE __attribute__((noinline))
#endif
HOST_REPORT_NOINLINE void* operator new(std::size_t n) {
    if (auto* owner = std::exchange(helperAllocationOwner, nullptr)) {
        helperAllocationObserved = owner->reap_retained_until(std::chrono::steady_clock::now() + 1s) ==
            sco::OwnedShutdown::ReapResult::Refused && !owner->ready();
    }
    if (pauseRootAllocation) {
        pauseRootAllocation = false;
        rootAllocationReached.store(true, std::memory_order_release);
        while (!rootAllocationRelease.load(std::memory_order_acquire)) std::this_thread::yield();
    }
    if (failNextOrdinaryAllocation) {
        failNextOrdinaryAllocation = false;
        ordinaryAllocationFired = true;
        throw std::bad_alloc();
    }
    if (statusAllocation) {
        statusAllocation = false;
        allocationStatus = gh::StatusDetailed();
        if (failAfterStatus) throw std::bad_alloc();
    }
    if (reenterAllocation) {
        reenterAllocation = false;
        allocationReport = gh::StartDetailed();
    }
    if (gh::testing::ConsumeOptionsAllocationFault()) {
        optionsAllocationFired = true;
        throw std::bad_alloc();
    }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
HOST_REPORT_NOINLINE void* operator new[](std::size_t n) { return ::operator new(n); }
HOST_REPORT_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
HOST_REPORT_NOINLINE void operator delete[](void* p) noexcept { ::operator delete(p); }
HOST_REPORT_NOINLINE void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
HOST_REPORT_NOINLINE void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
static bool reenter = false;
static bool admitted = true;
static std::weak_ptr<const gh::HostCapsule> reentryCapsule;
struct Backing {
    std::array<uint8_t, 8192> bytes{};
    sco::Image image{bytes.data(), {bytes.data(), 4096}, {bytes.data() + 4096, 4096}, {}, 0, 8192};
    std::shared_ptr<sco::OwnedShutdown> owner = std::make_shared<sco::OwnedShutdown>();
    std::string version = "owned mock host", output = "complete copied value";
    ~Backing() { ++destroyed; }
};
static gh::Admission Inspect() {
    ++inspections;
    const auto report = gh::StatusDetailed();
    CHECK(report.sessionId == 1 && report.dependencies == D::ProcessRetained);
    if (reenter) {
        reenter = false;
        auto capsule = reentryCapsule.lock(); CHECK(capsule);
        CHECK(gh::PrepareDetailed(capsule).result == R::Unavailable);
        CHECK(gh::Prepare(capsule->configuration()) == R::BadArg);
        CHECK(gh::StartDetailed().result == R::Unavailable);
        CHECK(gh::StopDetailed().result == R::Unavailable);
    }
    return {admitted ? gh::Session::Offline : gh::Session::Online, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
}
static gh::Options Configuration(const std::shared_ptr<Backing>& backing) {
    gh::Options o;
    o.inspect = Inspect; o.image = &backing->image;
    o.imageOwner = backing; o.resolverOwner = backing; o.shutdownOwner = backing->owner;
    o.platform.hostVersion = backing->version.c_str();
    o.quit = gh::QuitPolicy::External; o.dataCoreHook = gh::DataCoreHook::External;
    o.platform.pluginsEnabled = false; o.platform.privateSession.enabled = false;
    return o;
}
static void Retained(const gh::HostReport& report) {
    CHECK(report.sessionId == 1 && report.dependencies == D::ProcessRetained);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    if (mode == "retained-worker" || mode == "retained-worker-fault" || mode == "retained-helper-allocation-reentry") {
        // Stack destruction would hide a finished but unjoined helper. Keep
        // the actual owner process-rooted so TSan checks the production path.
        static auto* owner = new sco::OwnedShutdown;
        static std::atomic<bool> tlsDestroyed{false};
        struct Tail { ~Tail() { tlsDestroyed.store(true, std::memory_order_release); } };
        CHECK(owner->start_worker([&](const auto&) {
            thread_local Tail tail;
            (void)tail;
            if (mode == "retained-worker-fault") throw 1;
        }));
        if (mode == "retained-helper-allocation-reentry") {
            owner->fault();
            helperAllocationOwner = owner;
        }
        const bool drained = owner->drain_until(std::chrono::steady_clock::now() + 2s);
        if (mode == "retained-helper-allocation-reentry") CHECK(helperAllocationObserved);
        CHECK(drained == (mode == "retained-worker"));
        CHECK(owner->ready() == drained && tlsDestroyed.load(std::memory_order_acquire));
        CHECK(sco::OwnedShutdownTestAccess::handles_reaped(*owner));
        CHECK(!owner->drain_until(std::chrono::steady_clock::now() + 2s));
        std::puts("retained worker: callable/TLS joined, completed helper reaped");
        return 0;
    }
    if (mode == "empty-owner-no-allocation") {
        sco::OwnedShutdown owner;
        failNextOrdinaryAllocation = true;
        const bool drained = owner.drain_until(std::chrono::steady_clock::now() + 1s);
        failNextOrdinaryAllocation = false;
        CHECK(drained && owner.ready() && !ordinaryAllocationFired);
        std::puts("empty owner: real drain needs no joiner allocation");
        return 0;
    }
    static_assert(!std::is_copy_constructible_v<gh::HostCapsule> && !std::is_move_constructible_v<gh::HostCapsule>);
    static_assert(std::is_same_v<decltype(&gh::Prepare), R (*)(const gh::Options&)>);
    static_assert(std::is_same_v<decltype(&gh::Start), R (*)()>);
    static_assert(std::is_same_v<decltype(&gh::Stop), R (*)()>);
    CHECK(gh::StatusDetailed().dependencies == D::NeverExposed);
    auto backing = std::make_shared<Backing>();
    auto configuration = Configuration(backing);
    if (mode == "drain-timeout") configuration.drainTimeoutMs = 5;
    if (mode == "allocation-copy") configuration.platform.pluginRoot = std::string(8192, 'x');
    if (mode == "native-refusal") configuration.platform.gameServices = true;
    if (mode == "invalid") configuration.inspect = nullptr;
    auto capsule = std::make_shared<const gh::HostCapsule>(configuration, backing);
    std::weak_ptr<const gh::HostCapsule> weakCapsule = capsule;
    std::weak_ptr<Backing> weakBacking = backing;
    reentryCapsule = capsule;
    CHECK(capsule->configuration().image != &backing->image);
    CHECK(capsule->configuration().platform.hostVersion != backing->version.c_str());
    backing->image = {}; backing->version = "discarded builder version";
    CHECK(capsule->configuration().image->base == backing->bytes.data());
    CHECK(std::strcmp(capsule->configuration().platform.hostVersion, "owned mock host") == 0);
    if (mode == "status-overlap") {
        gh::HostReport prepared, delayed;
        std::thread preparing([&] {
            pauseRootAllocation = true;
            prepared = gh::PrepareDetailed(capsule);
        });
        const auto until = std::chrono::steady_clock::now() + 5s;
        while (!rootAllocationReached.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < until)
            std::this_thread::yield();
        const bool rootPaused = rootAllocationReached.load(std::memory_order_acquire);
        if (!rootPaused) { rootAllocationRelease.store(true); preparing.join(); CHECK(rootPaused); }
        gh::testing::StatusPause pause;
        gh::testing::PauseNextStatus(&pause);
        std::thread observing([&] { delayed = gh::StatusDetailed(); });
        while (!pause.reached.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < until)
            std::this_thread::yield();
        const bool statusPaused = pause.reached.load(std::memory_order_acquire);
        if (!statusPaused) {
            // Cancel an unconsumed pause, and release one already consumed by
            // either thread, before joining or making another status call.
            gh::testing::PauseNextStatus(nullptr);
            pause.release.store(true, std::memory_order_release);
            rootAllocationRelease.store(true, std::memory_order_release);
            preparing.join(); observing.join();
            CHECK(statusPaused);
        }
        rootAllocationRelease.store(true, std::memory_order_release);
        preparing.join();
        const auto newer = gh::StatusDetailed();
        pause.release.store(true, std::memory_order_release);
        observing.join();
        CHECK(statusPaused && prepared.result == R::Ok && newer.result == R::Ok);
        Retained(prepared); Retained(newer);
        CHECK(delayed.result == R::Unavailable && delayed.sessionId == 0 && delayed.dependencies == D::ProcessRetained);
        configuration = gh::Options{}; capsule.reset(); backing.reset();
        CHECK(!weakCapsule.expired() && !weakBacking.expired() && destroyed == 0);
        std::puts("status overlap: busy observation stays pending after adoption; actual capsule retained");
        return 0;
    }
    if (mode == "legacy") {
        CHECK(gh::Stop() == R::Ok);
        CHECK(gh::StatusDetailed().dependencies == D::ProcessRetained);
        CHECK(gh::PrepareDetailed(capsule).result == R::Unavailable);
        CHECK(gh::StatusDetailed().sessionId == 0);
    } else {
        if (mode == "allocation") gh::testing::SetReportFault(gh::testing::ReportFault::BeforeRootAllocation);
        if (mode == "allocation-copy") gh::testing::SetReportFault(gh::testing::ReportFault::OptionsAllocation);
        if (mode == "copy-before") gh::testing::SetReportFault(gh::testing::ReportFault::BeforeOptionsCopy);
        if (mode == "copy-after") gh::testing::SetReportFault(gh::testing::ReportFault::AfterOptionsCopy);
        reenter = mode == "reentry";
        reenterAllocation = mode == "allocation-reentry";
        statusAllocation = mode == "allocation-status-failure" || mode == "allocation-reentry";
        failAfterStatus = mode == "allocation-status-failure";
        const auto prepared = gh::PrepareDetailed(capsule);
        gh::testing::SetReportFault(gh::testing::ReportFault::None);
        if (mode == "invalid" || mode == "native-refusal" || mode == "allocation" || mode == "allocation-status-failure") {
            CHECK(prepared.result == ((mode == "allocation" || mode == "allocation-status-failure") ? R::Failed : R::BadArg));
            CHECK(prepared.sessionId == 0 && prepared.dependencies == D::NeverExposed);
            CHECK(inspections == 0);
            if (mode == "allocation-status-failure") {
                CHECK(allocationStatus.result == R::Unavailable && allocationStatus.sessionId == 0);
                CHECK(allocationStatus.dependencies == D::ProcessRetained);
                CHECK(gh::StatusDetailed().dependencies == D::NeverExposed);
            }
        } else {
            Retained(prepared);
            if (mode == "allocation-reentry") {
                CHECK(allocationStatus.result == R::Unavailable && allocationStatus.dependencies == D::ProcessRetained);
                CHECK(allocationReport.result == R::Unavailable && allocationReport.sessionId == 0);
                CHECK(allocationReport.dependencies == D::ProcessRetained);
            }
            // Drop every caller owner before checking that the actual core root
            // retained both the immutable capsule and all its real backing.
            configuration = gh::Options{}; capsule.reset(); backing.reset();
            CHECK(!weakCapsule.expired() && !weakBacking.expired() && destroyed == 0);
            auto retainedBacking = weakBacking.lock(); CHECK(retainedBacking);
            if (mode == "copy-before" || mode == "copy-after" || mode == "allocation-copy") {
                CHECK(prepared.result == R::Failed && prepared.phase == gh::Phase::Failed);
                CHECK(retainedBacking->owner->closed() && inspections == 0);
                if (mode == "allocation-copy") CHECK(optionsAllocationFired);
            } else {
                CHECK(prepared.result == R::Ok && prepared.phase == gh::Phase::Prepared);
                if (mode == "activate-refusal") {
                    admitted = false;
                    const auto refused = gh::ActivateDetailed(); Retained(refused);
                    CHECK(refused.result == R::Unavailable && refused.phase == gh::Phase::Prepared);
                    admitted = true;
                }
                if (mode == "activate" || mode == "activate-refusal") {
                    const auto activated = gh::ActivateDetailed(); Retained(activated);
                    CHECK(activated.result == R::Ok && activated.phase == gh::Phase::Activated);
                }
                if (mode == "post-start") gh::testing::FailAfterAppStart(true);
                const auto started = gh::StartDetailed(); Retained(started);
                if (mode == "post-start") {
                    CHECK(started.result == R::Failed && started.phase == gh::Phase::Failed);
                    CHECK(retainedBacking->owner->closed());
                } else {
                    CHECK(started.result == R::Ok && started.phase == gh::Phase::Running);
                    if (mode == "wrong-thread") {
                        gh::HostReport other;
                        std::thread caller([&] { other = gh::StopDetailed(); }); caller.join();
                        Retained(other); CHECK(other.result == R::WrongThread);
                    }
                    if (mode == "copy" || mode == "early-copy" || mode == "drain-timeout") {
                        const bool withLease = mode != "early-copy";
                        std::promise<void> entered, release;
                        auto resumed = release.get_future().share();
                        std::atomic<bool> stopDone{false}; std::string output;
                        std::thread caller([&] {
                            auto pin = withLease ? retainedBacking->owner->try_read() : sco::OwnedShutdown::Read{};
                            CHECK(!withLease || pin);
                            output = retainedBacking->output.substr(0, 1);
                            entered.set_value(); resumed.wait();
                            output += retainedBacking->output.substr(1);
                        });
                        CHECK(entered.get_future().wait_for(2s) == std::future_status::ready);
                        std::thread completion([&] {
                            CHECK(retainedBacking->owner->wait_until_closed(std::chrono::steady_clock::now() + 2s));
                            CHECK(!weakBacking.expired() && destroyed == 0);
                            if (withLease && mode != "drain-timeout") CHECK(!stopDone.load() && !retainedBacking->owner->ready());
                            else {
                                const auto deadline = std::chrono::steady_clock::now() + 2s;
                                while (!stopDone.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
                                CHECK(stopDone.load() && output.size() == 1);
                            }
                            release.set_value();
                        });
                        const auto stopped = gh::StopDetailed(); stopDone.store(true);
                        caller.join(); completion.join();
                        Retained(stopped); CHECK(stopped.result == R::Failed);
                        CHECK(output == retainedBacking->output);
                    } else {
                        auto selfRead = mode == "self-read-stop" ? retainedBacking->owner->try_read() : sco::OwnedShutdown::Read{};
                        CHECK(mode != "self-read-stop" || selfRead);
                        const auto stopped = gh::StopDetailed(); Retained(stopped);
                        CHECK(stopped.result == R::Failed);
                    }
                }
            }
            if (prepared.result == R::Ok) CHECK(gh::testing::RetainedRegistrySurvivesStaticRelease());
            const auto again = gh::StopDetailed(); Retained(again);
            CHECK(again.result != R::Ok);
            CHECK(gh::PrepareDetailed(weakCapsule.lock()).result == R::BadArg);
            CHECK(gh::StartDetailed().result != R::Ok);
            Retained(gh::StatusDetailed());
            CHECK(!weakBacking.expired() && destroyed == 0);
            std::puts("game-host report: retained capsule, phase/refusal and lifetime checks passed");
            return 0;
        }
    }
    configuration = gh::Options{}; capsule.reset(); backing.reset();
    CHECK(weakCapsule.expired() && weakBacking.expired() && destroyed == 1);
    std::puts("game-host report: unadopted private capsule released; no host exposure claimed");
}
