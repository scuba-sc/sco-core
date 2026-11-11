// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// Synthetic production-detour dispatch and owner publication; no hook or game.
#include "sco/game/host.h"
#include "../src/game/host_testing.h"
#include "../src/game/quit_owner_gate.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace gh = sco::game::host;
using Gate = gh::detail::QuitOwnerGate;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
static std::atomic<int> originals{0};
static thread_local bool calling = false;
static int systemToken, infoToken;
static sco::OwnedShutdown* observedOwner = nullptr;
static std::atomic<bool> closedAtOriginal{false};
static std::atomic<bool> quitInInspect{false};
static void Original(void* system, void* info, int code) {
    CHECK(calling && system == &systemToken && info == &infoToken && code == -7);
    if (observedOwner) closedAtOriginal.store(observedOwner->closed());
    ++originals;
}
static void Quit() {
    calling = true;
    gh::testing::InvokeQuitDetourForTest(&systemToken, &infoToken, -7);
    calling = false;
}
static gh::Admission Inspect() {
    if (quitInInspect.exchange(false)) Quit();
    return {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
}
struct Backing {
    std::array<uint8_t, 8192> bytes{};
    sco::Image image{bytes.data(), {bytes.data(), 4096}, {bytes.data() + 4096, 4096}, {}, 0, 8192};
    std::shared_ptr<sco::OwnedShutdown> owner = std::make_shared<sco::OwnedShutdown>();
};
static gh::Options Options(const std::shared_ptr<Backing>& backing) {
    gh::Options o;
    o.inspect = Inspect; o.image = &backing->image; o.imageOwner = backing; o.resolverOwner = backing;
    o.shutdownOwner = backing->owner; o.quit = gh::QuitPolicy::Required;
    o.dataCoreHook = gh::DataCoreHook::External;
    o.platform.pluginsEnabled = false; o.platform.privateSession.enabled = false;
    return o;
}
static void Until(const std::atomic<bool>& flag) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!flag.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < end)
        std::this_thread::yield();
    CHECK(flag.load(std::memory_order_acquire));
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    if (mode == "gate-race") {
        // Like the production gate, test gates and their published owners remain
        // reachable through process exit. This is not a destructor/join test.
        static auto* retainedGates = new std::vector<std::unique_ptr<Gate>>;
        for (int i = 0; i < 500; ++i) {
            retainedGates->push_back(std::make_unique<Gate>());
            Gate& gate = *retainedGates->back();
            auto owner = std::make_shared<sco::OwnedShutdown>();
            Gate::Publication result = Gate::Publication::Failed;
            std::thread publish([&] { result = gate.Publish(owner); });
            std::thread notify([&] { (void)gate.Notify(); });
            publish.join(); notify.join();
            CHECK(result == Gate::Publication::Published || result == Gate::Publication::Cancelled);
            CHECK(owner->closed() && !owner->try_read());
        }
        retainedGates->push_back(std::make_unique<Gate>());
        Gate& gate = *retainedGates->back();
        auto one = std::make_shared<sco::OwnedShutdown>();
        auto two = std::make_shared<sco::OwnedShutdown>();
        Gate::Publication a = Gate::Publication::Failed, b = a;
        std::thread first([&] { a = gate.Publish(one); });
        std::thread second([&] { b = gate.Publish(two); });
        first.join(); second.join();
        CHECK((a == Gate::Publication::Published) != (b == Gate::Publication::Published));
        CHECK((a == Gate::Publication::Competing) != (b == Gate::Publication::Competing));
        CHECK(gate.Notify() == Gate::Notification::Requested);
        CHECK(one->closed() == (a == Gate::Publication::Published));
        CHECK(two->closed() == (b == Gate::Publication::Published));
        std::puts("legacy quit gate: publication, quit and competing owners passed");
        return 0;
    }
    // A fixed synthetic original is set before any thread is created. The
    // wrapper invokes actual QuitDetour; no detour is installed into an image.
    gh::testing::SetQuitOriginalForTest(mode == "null-original" ? nullptr : Original);
    auto backing = std::make_shared<Backing>();
    auto owner = backing->owner;
    observedOwner = owner.get();
    auto options = Options(backing);
    if (mode == "early") {
        Quit(); CHECK(originals == 1 && owner->open());
        CHECK(gh::Prepare(options) == sco::Result::Unavailable);
        CHECK(owner->closed() && !owner->ready());
    } else if (mode == "before-publish" || mode == "after-publish" || mode == "competing" || mode == "early-allocation" || mode == "owner-worker") {
        gh::testing::QuitOwnerPause pause;
        if (mode == "after-publish" || mode == "owner-worker") pause.stage = gh::testing::QuitOwnerPauseStage::AfterPublication;
        gh::testing::PauseNextQuitOwner(&pause);
        if (mode == "early-allocation") gh::testing::FailNextQuitOwnerAllocation();
        sco::Result prepared = sco::Result::Ok;
        std::thread preparing([&] { prepared = gh::Prepare(options); });
        Until(pause.reached);
        if (mode == "competing") {
            auto other = std::make_shared<Backing>(); auto otherOptions = Options(other);
            CHECK(gh::Prepare(otherOptions) == sco::Result::BadArg);
            Quit(); CHECK(other->owner->open());
        } else if (mode == "after-publish") {
            auto frame = owner->try_read(); CHECK(frame);
            Quit(); CHECK(owner->closed() && !owner->ready() && frame && closedAtOriginal.load());
            CHECK(!owner->try_read());
        } else if (mode == "owner-worker") {
            auto returned = std::make_shared<std::promise<void>>();
            auto completed = returned->get_future();
            CHECK(owner->start_worker([returned](const auto&) { Quit(); returned->set_value(); }));
            CHECK(completed.wait_for(2s) == std::future_status::ready);
            CHECK(owner->closed() && !owner->ready() && closedAtOriginal.load());
        } else {
            Quit(); CHECK(owner->open() && !closedAtOriginal.load()); // owner not yet visible to detour
        }
        pause.release.store(true, std::memory_order_release);
        preparing.join();
        CHECK(prepared != sco::Result::Ok && owner->closed() && originals == 1);
    } else if (mode == "allocation") {
        gh::testing::FailNextQuitOwnerAllocation();
        CHECK(gh::Prepare(options) == sco::Result::Failed);
        Quit(); CHECK(originals == 1 && owner->open()); // no owner was published
    } else if (mode == "failed-prepare") {
        gh::testing::SetReportFault(gh::testing::ReportFault::BeforeOptionsCopy);
        CHECK(gh::Prepare(options) == sco::Result::Failed);
        std::weak_ptr<sco::OwnedShutdown> retained = owner;
        options = {}; backing.reset(); owner.reset();
        observedOwner = nullptr;
        CHECK(!retained.expired());
        Quit(); CHECK(originals == 1 && !retained.expired());
    } else if (mode == "inspect-reentry") {
        // A registry with this budget cannot register signatures. If startup
        // reaches registration after the reentrant quit it returns Failed.
        options.signatureRegistryBudgetBytes = 1;
        quitInInspect.store(true);
        CHECK(gh::Prepare(options) == sco::Result::Unavailable);
        CHECK(originals == 1 && owner->closed() && !owner->ready());
    } else if (mode == "published" || mode == "concurrent" || mode == "no-root-request" || mode == "notification-failure") {
        if (mode == "no-root-request") CHECK(gh::RequestStopDetailed().result == sco::Result::Unavailable);
        CHECK(gh::Prepare(options) == sco::Result::Ok);
        CHECK(!gh::Status().quitHookInstalled);
        // Existing native/required-hook gate remains in force.
        CHECK(gh::Activate() == sco::Result::Unavailable);
        CHECK(!gh::Status().quitHookInstalled);
        if (mode == "notification-failure") {
            // Model an owner-notification failure without executing foreign
            // code inside the production path. The original still runs.
            gh::testing::FailNextQuitOwnerNotification();
            Quit(); CHECK(originals == 1 && !owner->closed() && !closedAtOriginal.load());
            Quit(); CHECK(originals == 2);
        } else if (mode == "concurrent") {
            std::vector<std::thread> threads;
            for (int i = 0; i < 8; ++i) threads.emplace_back(Quit);
            for (auto& thread : threads) thread.join();
            CHECK(originals == 8);
        } else { Quit(); CHECK(originals == 1); }
        CHECK(owner->closed() && closedAtOriginal.load() && !owner->ready() && !owner->try_read());
    } else if (mode == "null-original") {
        Quit(); CHECK(originals == 0);
        CHECK(gh::Prepare(options) == sco::Result::Unavailable && owner->closed());
    } else return 2;
    std::puts("legacy quit owner: synthetic actual detour route passed; no engine barrier claimed");
}
