// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "../src/game/admission.h"
#include "../src/owned_shutdown_access.h"
#include "../src/api/internal.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <string>

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); std::abort(); } } while (0)
namespace gh = sco::game::host;
namespace sco::game::host::detail { void SetBeforeOwnerLoadForTest(void (*)()); }
static std::promise<void> entered, release;
static auto released = release.get_future().share();
static void Hold() { entered.set_value(); released.wait(); }
static gh::Admission InspectHeld() {
    Hold();
    return {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
}
static gh::Admission Inspect() {
    return {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
}
static std::shared_ptr<sco::OwnedShutdown> reentryOwner;
static bool reentryDrain = true;
static gh::Admission InspectReentrant() {
    reentryDrain = reentryOwner->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(1));
    return Inspect();
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    auto owner = std::make_shared<sco::OwnedShutdown>();
    if (std::string(argv[1]) == "prebind") {
        gh::detail::SetFreshInspector(Inspect);
        gh::detail::SetFreshAdmissionEnabled(true);
        gh::detail::SetBeforeOwnerLoadForTest(Hold);
        auto call = std::async(std::launch::async, [] { return gh::FreshAdmissionAllowed(); });
        CHECK(entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        CHECK(sco::OwnedShutdownHostAccess::begin_startup(*owner));
        auto transition = sco::OwnedShutdownHostAccess::transition(*owner);
        CHECK(transition && !sco::detail::BindRuntimeStartup(owner, transition));
        release.set_value();
        CHECK(call.get());
        return 0;
    }
    const bool reentry = std::string(argv[1]) == "reentry";
    CHECK(reentry || std::string(argv[1]) == "drain");
    CHECK(sco::OwnedShutdownHostAccess::begin_startup(*owner));
    {
        auto transition = sco::OwnedShutdownHostAccess::transition(*owner);
        CHECK(transition && sco::detail::BindRuntimeStartup(owner, transition));
        std::atomic<int> phase{0};
        CHECK(transition.publish_and_open(phase, 0, 1));
    }
    reentryOwner = owner;
    gh::detail::SetFreshInspector(reentry ? InspectReentrant : InspectHeld);
    gh::detail::SetFreshShutdownOwner(owner);
    gh::detail::SetFreshAdmissionEnabled(true);
    if (reentry) {
        CHECK(!gh::FreshAdmissionAllowed());
        CHECK(!reentryDrain && !owner->open() && !owner->ready());
        return 0;
    }
    auto call = std::async(std::launch::async, [] { return gh::FreshAdmissionAllowed(); });
    CHECK(entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    auto drain = std::async(std::launch::async, [&] {
        return owner->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(3));
    });
    CHECK(owner->wait_until_closed(std::chrono::steady_clock::now() + std::chrono::seconds(2)));
    CHECK(drain.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    CHECK(!gh::FreshAdmissionAllowed());
    release.set_value();
    CHECK(!call.get());
    CHECK(drain.get());
}
