// Synthetic app lifecycle with an injected game-service boundary. No game is loaded.
#include "sco/app.h"
#include "sco/caps.h"
#include "sco/game/services.h"
#include "sco/log.h"
#include "sco/signature_registry.h"
#include "../src/owned_shutdown_access.h"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

static int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
namespace {
std::string mode;
std::vector<std::string> calls;
int token = 0, barrierCalls = 0, capabilityCalls = 0;
void ThrowingSink(const char* line) {
    if ((mode == "throw-log-start" && std::strstr(line, "game services not started")) ||
        (mode == "throw-log-summary" && std::strstr(line, "game services left retained state")) ||
        (mode == "throw-log-stop" && std::strstr(line, "dependent teardown refused")))
        throw std::runtime_error("synthetic log sink fault");
}
void SetCapabilities() {
    ++capabilityCalls;
    CHECK(sco::caps::Set("fixture.game", true) == sco::Result::Ok);
}
bool ReadersQuiesced(void* context) {
    calls.emplace_back("quiesced");
    ++barrierCalls;
    CHECK(context == &token);
    if (mode == "throw" || (mode == "stop-throw" && barrierCalls > 1))
        throw std::runtime_error("synthetic quiescence failure");
    return mode != "false" && !(mode == "stop-false" && barrierCalls > 1);
}
bool BeforeDependentStop(void* context) {
    calls.emplace_back("dependent");
    CHECK(context == &token);
    if (mode == "dependent-throw") throw std::runtime_error("synthetic consumer retirement fault");
    return false;
}
const sco_plugin_info* Query() {
    static const sco_plugin_info info{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
                                      "fixture", "1.0.0", "tests"};
    return &info;
}
sco_result Load(const sco_api*, sco_plugin*) { return SCO_OK; }
void Unload() { calls.emplace_back("unload"); }
const sco::plugins::Builtin builtin{"fixture", Query, Load, Unload};
}
namespace sco::game::services {
// Boundary fixture; actual mining adapter is exercised by test_mining.
bool MiningHookRetained() noexcept { return false; }
Result Start() {
    calls.emplace_back("start");
    if (mode == "throw-stage-1" || mode == "throw-stage-2") {
        calls.emplace_back("spawn-started");
        if (mode == "throw-stage-2") calls.emplace_back("actors-started");
        throw std::bad_alloc();
    }
    return mode == "partial-fail" || mode == "partial-clean" || mode.rfind("throw-log-", 0) == 0 ? Result::Failed : Result::Ok;
}
Result Stop() {
    calls.emplace_back("stop");
    return mode == "stop-fail" || mode == "partial-fail" ? Result::Failed : Result::Ok;
}
Result FinalizeStopAfterQuiescence() {
    calls.emplace_back("finalize");
    return mode == "finalize-fail" ? Result::Failed : Result::Ok;
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    mode = argv[1];
    if (mode == "production-refusal") {
        sco::app::Platform platform;
        platform.gameServices = true;
        platform.gameReadersQuiesced = ReadersQuiesced;
        platform.gameReadersContext = &token;
        CHECK(!sco::app::Start(platform));
        CHECK(calls.empty() && barrierCalls == 0 && capabilityCalls == 0);
        CHECK(!sco::AcquireHostSignatureRegistry());
        return failures ? 1 : 0;
    }
    if (mode != "phase-owner" && mode != "true" && mode != "false" && mode != "null" && mode != "throw" &&
        mode != "stop-false" && mode != "stop-throw" && mode != "stop-fail" && mode != "finalize-fail" &&
        mode != "partial-fail" && mode != "partial-clean" && mode != "throw-stage-1" && mode != "throw-stage-2" &&
        mode != "throw-log-start" && mode != "throw-log-summary" && mode != "throw-log-stop" && mode != "dependent-retained" && mode != "dependent-throw") return 2;
    if (mode.rfind("throw-log-", 0) == 0) sco::SetLogSink(ThrowingSink);
    sco::SetGameThread();
    sco::Image image{}; // empty registry and synthetic image; no image address is dereferenced
    sco::app::Platform platform;
    platform.image = &image;
    platform.imageOwner = platform.resolverOwner = std::make_shared<int>(1);
    platform.gameServices = true;
    platform.gameReadersQuiesced = mode == "null" ? nullptr : ReadersQuiesced;
    platform.gameReadersContext = &token;
    platform.setCapabilities = SetCapabilities;
    const bool dependentFailure = mode == "dependent-retained" || mode == "dependent-throw";
    if (dependentFailure) {
        platform.beforeDependentStop = BeforeDependentStop;
        platform.beforeDependentStopContext = &token;
        platform.builtins = &builtin;
        platform.nBuiltins = 1;
    }
    if (mode == "phase-owner") {
        platform.pluginsEnabled = false;
        platform.privateSession.enabled = false;
        platform.runtimeInspection.enabled = false;
        sco::OwnedShutdown gate;
        struct Barrier {
            sco::OwnedShutdown* gate; sco::OwnedShutdown::Transition* transition = nullptr;
            std::thread::id control; bool stopping = false; unsigned calls = 0;
        } barrier{&gate, nullptr, std::this_thread::get_id()};
        platform.gameReadersContext = &barrier;
        platform.gameReadersQuiesced = [](void* context) {
            auto* b = static_cast<Barrier*>(context);
            if (!b || b->control != std::this_thread::get_id() || b->calls > 1 ||
                b->stopping != (b->calls == 1)) return false;
            if (b->stopping ? !b->gate->ready() :
                (!b->transition || !b->transition->valid_for(*b->gate))) return false;
            bool admitted = true;
            std::thread probe([&] { admitted = static_cast<bool>(b->gate->try_read()); });
            probe.join();
            if (admitted) return false;
            ++b->calls;
            return true;
        };
        CHECK(sco::OwnedShutdownHostAccess::begin_startup(gate));
        std::atomic<int> phase{0};
        {
            auto transition = sco::OwnedShutdownHostAccess::transition(gate);
            CHECK(transition);
            if (!transition) return 1;
            barrier.transition = &transition;
            const bool started = sco::app::Start(platform);
            CHECK(started && barrier.calls == 1);
            if (!started || barrier.calls != 1 || !transition.publish_and_open(phase, 0, 1)) std::abort();
        }
        barrier.transition = nullptr;
        std::promise<void> entered, release;
        auto proceed = release.get_future().share();
        std::thread reader([&] {
            auto lease = gate.try_read();
            if (!lease) { entered.set_exception(std::make_exception_ptr(std::runtime_error("reader refused"))); return; }
            entered.set_value(); proceed.wait();
        });
        auto readerEntered = entered.get_future();
        const bool enteredInTime = readerEntered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        CHECK(enteredInTime);
        if (!enteredInTime) std::abort();
        readerEntered.get();
        auto drain = std::async(std::launch::async, [&] {
            return gate.drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(3));
        });
        CHECK(gate.wait_until_closed(std::chrono::steady_clock::now() + std::chrono::seconds(2)));
        CHECK(drain.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
        release.set_value(); reader.join();
        const bool drained = drain.get();
        CHECK(drained);
        if (!drained) std::abort();
        barrier.stopping = true;
        const auto stopped = sco::app::Stop();
        CHECK(stopped == sco::Result::Ok && barrier.calls == 2 && !gate.try_read());
        if (stopped != sco::Result::Ok || barrier.calls != 2) std::abort();
        CHECK((calls == std::vector<std::string>{"start", "stop", "finalize"}));
        return failures ? 1 : 0;
    }
    const bool started = sco::app::Start(platform);
    CHECK(capabilityCalls == 1);
    if (mode == "false" || mode == "null" || mode == "throw") {
        CHECK(!started);
        CHECK(!sco::AcquireHostSignatureRegistry() && !sco::caps::Has("fixture.game"));
        if (mode == "null") CHECK(calls.empty());
        else if (mode == "false" || mode == "throw") CHECK((calls == std::vector<std::string>{"quiesced"}));
        CHECK(sco::app::Stop() == sco::Result::Ok);
        std::printf("game-service shutdown %s: %d checks, %d failures\n", mode.c_str(), checks, failures);
        return failures ? 1 : 0;
    }
    const auto owner = sco::AcquireHostSignatureRegistry();
    CHECK(owner && owner->Snapshot());
    if (mode == "partial-fail" || mode == "partial-clean" || mode == "throw-stage-1" || mode == "throw-stage-2" || mode.rfind("throw-log-", 0) == 0) {
        CHECK(!started);
        if (mode == "throw-log-start") CHECK((calls == std::vector<std::string>{"quiesced", "start"}));
        else if (mode == "throw-stage-1") CHECK((calls == std::vector<std::string>{"quiesced", "start", "spawn-started", "quiesced"}));
        else if (mode == "throw-stage-2") CHECK((calls == std::vector<std::string>{"quiesced", "start", "spawn-started", "actors-started", "quiesced"}));
        else CHECK((calls == std::vector<std::string>{"quiesced", "start", "quiesced"}));
    } else {
        CHECK(started && sco::caps::Has("fixture.game"));
        CHECK((calls == std::vector<std::string>{"quiesced", "start"}));
        const auto result = sco::app::Stop();
        if (mode == "true") {
            CHECK(result == sco::Result::Ok);
            CHECK((calls == std::vector<std::string>{"quiesced", "start", "quiesced", "stop", "finalize"}));
            CHECK(!sco::AcquireHostSignatureRegistry() && !sco::caps::Has("fixture.game"));
            CHECK(sco::app::Start(platform));
            CHECK(sco::AcquireHostSignatureRegistry() && sco::AcquireHostSignatureRegistry() != owner);
            CHECK(capabilityCalls == 2 && sco::caps::Has("fixture.game"));
            CHECK(sco::app::Stop() == sco::Result::Ok);
            CHECK(!sco::AcquireHostSignatureRegistry() && !sco::caps::Has("fixture.game"));
            CHECK((calls == std::vector<std::string>{"quiesced", "start", "quiesced", "stop", "finalize",
                                                     "quiesced", "start", "quiesced", "stop", "finalize"}));
            std::printf("game-service shutdown %s: %d checks, %d failures\n", mode.c_str(), checks, failures);
            return failures ? 1 : 0;
        }
        CHECK(result == sco::Result::Failed);
        if (mode == "stop-false" || mode == "stop-throw") CHECK((calls == std::vector<std::string>{"quiesced", "start", "quiesced"}));
        if (mode == "stop-fail") CHECK((calls == std::vector<std::string>{"quiesced", "start", "quiesced", "stop"}));
        if (mode == "finalize-fail") CHECK((calls == std::vector<std::string>{"quiesced", "start", "quiesced", "stop", "finalize"}));
        if (dependentFailure) CHECK((calls == std::vector<std::string>{"quiesced", "start", "dependent", "quiesced"}));
    }
    const auto before = calls;
    CHECK(sco::AcquireHostSignatureRegistry() == owner && !sco::caps::Has("fixture.game"));
    auto hostileOwner = std::const_pointer_cast<sco::SignatureRegistry>(owner);
    CHECK(!sco::ClearHostSignatureRegistry(hostileOwner, nullptr)); // retained and still pinned
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(!sco::app::Start(platform));
    CHECK(calls == before && sco::AcquireHostSignatureRegistry() == owner);
    std::printf("game-service shutdown %s: %d checks, %d failures\n", mode.c_str(), checks, failures);
    return failures ? 1 : 0;
}
