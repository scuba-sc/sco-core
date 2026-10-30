// Synthetic offline host composition; no game image, data, process or network.
#include "sco/game/host.h"
#include "../src/game/host_testing.h"
#include "sco/runtime.h"
#include "sco/caps.h"
#include "sco/signature_registry.h"
#include <cstdio>
#include <thread>
#include <stdexcept>
#include <cstring>
#include <string>

namespace gh = sco::game::host;
static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static gh::Admission evidence;
static int inspections = 0;
static gh::Admission Inspect() { ++inspections; return evidence; }
static int ticks = 0, tasks = 0;
static int shutdownCalls = 0, exitCalls = 0, serviceStops = 0;
static bool productClosed = false, controlTick = false, partialStartup = false;
static std::string shutdownPolicy = "true";
static int shutdownContext;
static gh::ConsumerRetirement RetireConsumer(void* ctx) {
    ++shutdownCalls;
    CHECK(ctx == &shutdownContext && gh::Status().phase == (partialStartup ? gh::Phase::Starting : gh::Phase::Stopping));
    CHECK(!gh::FreshAdmissionAllowed() && exitCalls == 0 && serviceStops == 0);
    if (evidence.session == gh::Session::Unknown) CHECK(controlTick); // the Tick control frame must not wait on itself
    if (shutdownPolicy == "throw") throw std::runtime_error("synthetic product shutdown fault");
    if (shutdownPolicy == "retained") return gh::ConsumerRetirement::Retained;
    productClosed = true; // synthetic: subsequent product readers are denied
    return gh::ConsumerRetirement::Quiesced;
}
static void FailCapabilities() {
    CHECK(sco::caps::Set("fixture.partial", true) == sco::Result::Ok);
    throw std::runtime_error("synthetic capability startup failure");
}
static sco::Result HostServiceStart(void*) { return sco::Result::Ok; }
static sco::Result HostServiceTick(void*) { return sco::Result::Ok; }
static sco::Result HostServiceStop(void*) {
    CHECK(productClosed && shutdownCalls == 1 && exitCalls == 1);
    ++serviceStops;
    return sco::Result::Ok;
}
static void ExitEvent(const char*, const void*, void*) {
    CHECK(productClosed && shutdownCalls == 1 && serviceStops == 0);
    ++exitCalls;
}
static void TickEvent(const char*, const void*, void*) { ++ticks; }
static void Task(void*) { ++tasks; }
static int crashedTicks = 0;
static void CrashTick(const char*, const void*, void*) { ++crashedTicks; throw std::runtime_error("synthetic tick fault"); }
static const sco_plugin_info* Query() {
    static const sco_plugin_info info{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "synthetic", "1.0.0", "tests"};
    return &info;
}
static int loads = 0;
static sco_result Load(const sco_api* api, sco_plugin* self) { ++loads; return api->subscribe(self, "tick", CrashTick, nullptr); }
static void Unload() {}
static const sco::plugins::Builtin builtin{"synthetic", Query, Load, Unload};
static int originals = 0;
static void* passedSystem = nullptr;
static void* passedInfo = nullptr;
static int passedCode = 0;
static void OriginalQuit(void* system, void* info, int code) {
    ++originals; passedSystem = system; passedInfo = info; passedCode = code;
    CHECK(gh::Status().phase == gh::Phase::Stopped);
    CHECK(sco::QueuedTasks() == 0);
    CHECK(gh::Stop() == sco::Result::Ok); // repeat from original remains harmless
}
static uint32_t ExceptionGuard(void (*thunk)(void*), void* ctx) {
    try { thunk(ctx); return 0; } catch (const std::runtime_error&) { return 0xe06d7363u; }
}
int main(int argc, char** argv) {
    sco::plugins::SetCallGuard(ExceptionGuard);
    const bool loss = argc > 1 && std::strcmp(argv[1], "loss") == 0;
    const bool quit = argc > 1 && std::strcmp(argv[1], "quit") == 0;
    const bool missingQuit = argc > 1 && std::strcmp(argv[1], "missing-quit") == 0;
    const bool extraPin = argc > 1 && std::strcmp(argv[1], "extra-pin") == 0;
    const std::string mode = argc > 1 ? argv[1] : "";
    const bool failureStop = mode.rfind("shutdown-", 0) == 0;
    const bool failureLoss = mode.rfind("loss-", 0) == 0;
    const bool failureQuit = mode.rfind("quit-", 0) == 0;
    partialStartup = mode.rfind("partial-", 0) == 0;
    if (failureStop || failureLoss || failureQuit || partialStartup) {
        shutdownPolicy = mode.substr(mode.find('-') + 1);
        if (shutdownPolicy != "retained" && shutdownPolicy != "null" && shutdownPolicy != "throw" &&
            !(partialStartup && shutdownPolicy == "quiesced")) return 2;
    }
    gh::Options o;
    CHECK(!o.nativeDataCoreReads && !o.preEngineQuiesced);
    auto native=o;
    native.nativeDataCoreReads=true;
    CHECK(gh::Prepare(native)==sco::Result::BadArg);
    CHECK(gh::Status().phase==gh::Phase::Idle);
    o.inspect = Inspect;
    o.quit = missingQuit ? gh::QuitPolicy::Required : gh::QuitPolicy::External; // synthetic host supplies the exact BeforeQuit path
    o.platform.builtins = &builtin;
    o.platform.nBuiltins = 1;
    o.retireConsumer = shutdownPolicy == "null" ? nullptr : RetireConsumer;
    o.consumerContext = &shutdownContext;
    o.platform.hostService = {&shutdownContext, HostServiceStart, HostServiceTick, HostServiceStop};
    if (partialStartup) o.platform.setCapabilities = FailCapabilities;
    // Unknown evidence refuses before signature resolution or host startup.
    CHECK(gh::Prepare(o) == sco::Result::Unavailable);
    CHECK(gh::Status().phase == gh::Phase::Idle);
    evidence = {gh::Session::Online, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
    CHECK(gh::Prepare(o) == sco::Result::Unavailable);
    evidence.session = gh::Session::Offline;
    evidence.antiCheat = gh::Check::Present;
    CHECK(gh::Prepare(o) == sco::Result::Unavailable);
    evidence.antiCheat = gh::Check::Unknown;
    CHECK(gh::Prepare(o) == sco::Result::Unavailable);
    evidence.antiCheat = gh::Check::Clear;
    evidence.officialNetwork = gh::Check::Unknown;
    CHECK(gh::Prepare(o) == sco::Result::Unavailable);
    evidence.officialNetwork = gh::Check::Clear;
    evidence.targetProcess = gh::Check::Unknown;
    CHECK(gh::Prepare(o) == sco::Result::Unavailable);
    evidence.targetProcess = gh::Check::Clear;
    CHECK(gh::Prepare(o) == sco::Result::BadArg); // no readable image
    CHECK(!gh::Status().signaturesResolved && loads == 0);
    uint8_t bytes[8192]{};
    sco::Image image{bytes, {bytes, 4096}, {bytes + 4096, 4096}, {}, 0, sizeof(bytes)};
    o.image = &image;
    if (missingQuit) {
        CHECK(gh::Prepare(o) == sco::Result::Unavailable);
        CHECK(gh::Status().phase == gh::Phase::Failed && !gh::Status().quitHookInstalled);
        CHECK(gh::Start() == sco::Result::BadArg && loads == 0);
        return failures ? 1 : 0;
    }
    CHECK(gh::Prepare(o) == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Prepared);
    CHECK(gh::Prepare(o) == sco::Result::BadArg);
    evidence.session = gh::Session::Online; // revalidate before publication
    CHECK(gh::Start() == sco::Result::Unavailable);
    CHECK(gh::Status().phase == gh::Phase::Prepared);
    evidence.session = gh::Session::Offline;
    if (partialStartup) {
        const auto owner = sco::AcquireHostSignatureRegistry();
        CHECK(gh::Start() == sco::Result::Failed);
        CHECK(gh::Status().phase == gh::Phase::Failed && sco::AcquireHostSignatureRegistry() == owner);
        CHECK(!gh::FreshAdmissionAllowed() && !sco::caps::Has("fixture.partial"));
        CHECK(shutdownCalls == (shutdownPolicy == "null" ? 0 : 1));
        CHECK(productClosed == (shutdownPolicy == "quiesced") && exitCalls == 0 && serviceStops == 0);
        CHECK(gh::Start() == sco::Result::BadArg && gh::Stop() == sco::Result::Unavailable);
        auto hostileOwner = std::const_pointer_cast<sco::SignatureRegistry>(owner);
        CHECK(!sco::ClearHostSignatureRegistry(hostileOwner, nullptr));
        std::printf("game_host %s: %d checks, %d failures\n", mode.c_str(), checks, failures);
        return failures ? 1 : 0;
    }
    CHECK(gh::Start() == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Running);
    CHECK(gh::Start() == sco::Result::BadArg);
    CHECK(sco::Subscribe(&ticks, "tick", TickEvent, nullptr) == sco::Result::Ok);
    CHECK(sco::Subscribe(&ticks, "game.exit", ExitEvent, nullptr) == sco::Result::Ok);
    CHECK(sco::Post(Task, nullptr) == sco::Result::Ok);
    CHECK(gh::Tick(0xfffffff0u) == sco::Result::Ok); // first tick, even near wrap
    CHECK(tasks == 1 && ticks == 1);
    CHECK(sco::Post(Task, nullptr) == sco::Result::Ok);
    CHECK(gh::Tick(0x20u) == sco::Result::Ok); // 48 ms, throttled across wrap
    CHECK(tasks == 1 && ticks == 1);
    CHECK(gh::Tick(0x54u) == sco::Result::Ok); // 100 ms
    CHECK(tasks == 2 && ticks == 2);
    CHECK(crashedTicks == 1); // faulted plugin is not resurrected by a host fallback
    CHECK(sco::app::Plugins()[0].state == sco::plugins::State::Crashed);
    std::thread tickThread([&] { CHECK(gh::Tick(200) == sco::Result::WrongThread); }); tickThread.join();
    CHECK(tasks == 2 && ticks == 2);
    sco::Result wrong = sco::Result::Ok;
    std::thread t([&] { wrong = gh::Stop(); }); t.join();
    CHECK(wrong == sco::Result::WrongThread);
    CHECK(gh::Status().phase == gh::Phase::Running);
    CHECK(sco::Post(Task, nullptr) == sco::Result::Ok);
    const auto owner = sco::AcquireHostSignatureRegistry();
    CHECK(owner && sco::caps::Set("fixture.shutdown", true) == sco::Result::Ok);
    if (failureStop || failureLoss || failureQuit) {
        if (failureLoss) {
            evidence.session = gh::Session::Unknown;
            controlTick = true;
            CHECK(gh::Tick(0x55u) == sco::Result::Unavailable);
            controlTick = false;
        } else if (failureQuit) CHECK(gh::BeforeQuit() == sco::Result::Failed);
        else CHECK(gh::Stop() == sco::Result::Failed);
        CHECK(gh::Status().phase == gh::Phase::Failed && sco::AcquireHostSignatureRegistry() == owner);
        CHECK(!sco::caps::Has("fixture.shutdown") && !gh::FreshAdmissionAllowed());
        CHECK(shutdownCalls == (shutdownPolicy == "null" ? 0 : 1));
        CHECK(!productClosed && exitCalls == 0 && serviceStops == 0 && sco::QueuedTasks() == 1);
        CHECK(gh::Start() == sco::Result::BadArg && gh::Stop() == sco::Result::Unavailable);
        auto hostileOwner = std::const_pointer_cast<sco::SignatureRegistry>(owner);
        CHECK(!sco::ClearHostSignatureRegistry(hostileOwner, nullptr));
        std::printf("game_host %s: %d checks, %d failures\n", mode.c_str(), checks, failures);
        return failures ? 1 : 0;
    }
    if (extraPin) {
        // A second live pin prevents the host from discarding its registry at teardown.
        CHECK(owner && sco::PinHostSignatureRegistry(owner));
        CHECK(sco::caps::Set("fixture.extra_pin", true) == sco::Result::Ok);
        CHECK(gh::Stop() == sco::Result::Failed);
        CHECK(gh::Status().phase == gh::Phase::Failed && sco::AcquireHostSignatureRegistry() == owner);
        CHECK(!sco::caps::Has("fixture.extra_pin"));
        CHECK(productClosed && shutdownCalls == 1 && exitCalls == 1 && serviceStops == 1);
        CHECK(sco::UnpinHostSignatureRegistry(owner));
        CHECK(sco::AcquireHostSignatureRegistry() == owner);
        std::printf("game_host extra-pin: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    if (loss) {
        evidence.session = gh::Session::Unknown;
        controlTick = true;
        CHECK(gh::Tick(0x55u) == sco::Result::Unavailable); // checked even when throttled
        controlTick = false;
    } else if (quit) {
        gh::testing::ForwardQuit(OriginalQuit, &ticks, &tasks, 73);
        CHECK(originals == 1 && passedSystem == &ticks && passedInfo == &tasks && passedCode == 73);
        gh::testing::ForwardQuit(OriginalQuit, &tasks, &ticks, -7);
        CHECK(originals == 2 && passedSystem == &tasks && passedInfo == &ticks && passedCode == -7);
    } else {
        CHECK(gh::Stop() == sco::Result::Ok);
    }
    CHECK(gh::Stop() == sco::Result::Ok);
    CHECK(gh::Tick(500) == sco::Result::Unavailable);
    CHECK(tasks == 2 && ticks == 2 && sco::QueuedTasks() == 0);
    CHECK(gh::Status().phase == gh::Phase::Stopped);
    CHECK(productClosed && shutdownCalls == 1 && exitCalls == 1 && serviceStops == 1);
    CHECK(inspections >= 8);
    std::printf("game_host: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
