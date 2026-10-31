// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/app.h"
#include "sco/log.h"
#include "sco/reserved_session.h"
#include "sco/runtime.h"
#include "sco/signature_registry.h"
#include "sco/status.h"
#include "../src/reserved_legacy_gate.h"
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <atomic>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
namespace rs = sco::reserved;
static sco::Result ReserveEmpty(rs::CoreBinding& binding, rs::Session& session) {
    auto seed = std::make_unique<rs::MockGraph>();
    return rs::ReserveSession(binding, *seed, session);
}
static int sinkCalls = 0;
static void Sink(const char*) { ++sinkCalls; }
static bool callbackCalled = false;
static void Callback() noexcept { callbackCalled = true; }
static unsigned guardedCalls = 0;
static uint32_t Guard(void (*fn)(void*), void* context) { ++guardedCalls; fn(context); return 0; }
static void Thunk(void*) { ++guardedCalls; }
static std::promise<void> openEntered;
static std::shared_future<void> openRelease;
static bool opened = false;
static std::atomic<bool> discoveryCalled{false};
static std::promise<void> discoveryEntered;
static std::shared_future<void> discoveryRelease;
static int DiscoveryCapability(const char*) {
    if (!discoveryCalled.exchange(true)) discoveryEntered.set_value();
    discoveryRelease.wait();
    return 1;
}
static void* FakeOpen(const std::filesystem::path&, std::string&) {
    opened = true; openEntered.set_value(); openRelease.wait(); return nullptr;
}
static void* FakeSymbol(void*, const char*) { return nullptr; }
static void FakeClose(void*) {}
int main(int argc, char** argv) {
    sco::diagnostics::Capture capture;
    int captureKey = 0;
    CHECK(capture.Start(1, true) && capture.AssignKey(&captureKey, "fixture"));
    rs::CoreBinding binding; rs::Session session;
    const auto* bootstrap = rs::CreateExecutableAuthority(); CHECK(bootstrap);
    CHECK(rs::BindCoreInstance(bootstrap, binding) == sco::Result::Ok);
    if (argc > 2 && (std::string(argv[1]) == "discover-first" || std::string(argv[1]) == "discover-reserved")) {
        CHECK(std::filesystem::is_regular_file(std::filesystem::path(argv[2]) / "needcap" / "plugin.ini"));
        sco::plugins::Options options; options.enabled = true; options.has = DiscoveryCapability;
        std::promise<void> release; discoveryRelease = release.get_future().share();
        if (std::string(argv[1]) == "discover-reserved") {
            CHECK(ReserveEmpty(binding, session) == sco::Result::Ok);
            CHECK(sco::plugins::Discover(argv[2], options).empty() && !discoveryCalled.load());
            return 0;
        }
        auto discover = std::async(std::launch::async, [&] { return sco::plugins::Discover(argv[2], options); });
        CHECK(discoveryEntered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        { std::lock_guard lock(sco::detail::g_authorityMutex); CHECK(sco::detail::g_legacyEntries == 1); }
        CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
        release.set_value(); CHECK(!discover.get().empty());
        { std::lock_guard lock(sco::detail::g_authorityMutex); CHECK(sco::detail::g_legacyEntries == 0); }
        CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "capture-first") {
        sco::diagnostics::SetLogCapture(&capture);
        CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
        sco::diagnostics::SetLogCapture(nullptr);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "startup-reentry") {
        CHECK(ReserveEmpty(binding, session) == sco::Result::Ok);
        rs::StartupControl control;
        CHECK(rs::BeginStartup(session, control) == sco::Result::Ok);
        // This simulates an external callback on the startup thread. It has no
        // publication capability despite running inside the startup frame.
        int owner = 0, candidate = 0;
        const void* out = reinterpret_cast<const void*>(1);
        CHECK(sco::ProvideService(&owner, nullptr, "fixture.reentry", 1, &candidate) == sco::Result::Unavailable);
        CHECK(sco::QueryService("fixture.reentry", 1, &out) == sco::Result::Unavailable && !out);
        CHECK(!sco::ServiceExists("fixture.reentry"));
        CHECK(rs::PublishTypedProvider(control) == sco::Result::Ok);
        CHECK(rs::FinishStartup(control) == sco::Result::Ok);
        CHECK(rs::Close(session) == sco::Result::Ok);
        CHECK(rs::RetireUntil(session, std::chrono::steady_clock::now() + std::chrono::seconds(2)) == rs::RetireResult::QuiescedRetained);
        return 0;
    }
    if (argc > 1 && (std::string(argv[1]) == "loader-first" || std::string(argv[1]) == "reserved-first")) {
        sco::plugins::Plugin plugin;
        plugin.state = sco::plugins::State::Ready;
        plugin.manifest.kind = sco::plugins::Kind::Native;
        plugin.manifest.entry = "fixture";
        sco_api api{}; sco::plugins::Options options;
        sco::plugins::ModuleOps ops{FakeOpen, FakeSymbol, FakeClose};
        auto* self = reinterpret_cast<sco_plugin*>(1);
        std::promise<void> release; openRelease = release.get_future().share();
        if (std::string(argv[1]) == "reserved-first") {
            CHECK(ReserveEmpty(binding, session) == sco::Result::Ok);
            CHECK(!sco::plugins::LoadNative(plugin, &api, self, options, ops));
            CHECK(!opened && plugin.state == sco::plugins::State::Ready && !plugin.module);
            return 0;
        }
        auto load = std::async(std::launch::async, [&] { return sco::plugins::LoadNative(plugin, &api, self, options, ops); });
        CHECK(openEntered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
        release.set_value(); CHECK(!load.get());
        CHECK(ReserveEmpty(binding, session) == sco::Result::Unavailable && !session);
        return 0;
    }
    CHECK(ReserveEmpty(binding, session) == sco::Result::Ok);
    int owner = 0, table = 0;
    CHECK(sco::ProvideService(&owner, nullptr, "fixture.test", 1, &table) == sco::Result::Unavailable);
    const void* out = reinterpret_cast<const void*>(1);
    CHECK(sco::QueryService("fixture.test", 1, &out) == sco::Result::Unavailable && !out);
    CHECK(!sco::ServiceExists("fixture.test"));
    sco::SetGameThread(); CHECK(!sco::OnGameThread()); CHECK(sco::QueuedTasks() == 0);
    sco::SetLogSink(Sink); sco::Log("must not call sink"); sco::Status("must not change status");
    sco::diagnostics::SetLogCapture(&capture);
    sco::diagnostics::CapturePluginLog(&captureKey, sco::diagnostics::LogLevel::Info, "must not record");
    CHECK(capture.Tail("fixture", 0, 10).entries.empty());
    sco::plugins::SetCallGuard(Guard);
    CHECK(sco::plugins::Guarded(Thunk, nullptr) != 0 && guardedCalls == 0);
    sco::plugins::ContainCallouts(nullptr);
    char status[4] = "xyz";
    CHECK(!sco::GetStatus(status, sizeof status) && status[0] == 'x' && sinkCalls == 0);
    CHECK(!sco::WithNoHostSignatureRegistry(Callback) && !callbackCalled);
    CHECK(!sco::PinHostSignatureRegistry({}));
    CHECK(!sco::AcquireHostSignatureRegistry());
    sco::app::Platform platform;
    CHECK(sco::app::StartDetailed(platform) == sco::app::StartResult::RefusedBeforeExposure);
    CHECK(rs::Status(session) == rs::Phase::Reserved);
}
