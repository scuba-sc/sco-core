// Synthetic offline host composition; no game image, data, process or network.
#include "sco/game/host.h"
#include "../src/game/host_testing.h"
#include "sco/runtime.h"
#include "sco/caps.h"
#include "sco/signature_registry.h"
#include "sco/log.h"
#include "sco/net/session.h"
#include "../src/api/internal.h"
#include <cstdio>
#include <thread>
#include <stdexcept>
#include <cstring>
#include <string>
#include <future>

namespace gh = sco::game::host;
namespace sco::net::testing { bool RawStarted(); }
static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static gh::Admission evidence;
static int inspections = 0;
static std::shared_ptr<sco::OwnedShutdown> faultOwner;
static int faultAfter = 0;
static bool reenterStop = false;
static sco::Result reentryResult = sco::Result::Ok;
static bool faultSinkFired = false;
static bool reentrantSinkFired = false;
static bool sinkEmptyPlugins = false;
static sco::Result sinkProvide = sco::Result::Ok, sinkQuery = sco::Result::Ok, sinkSubscribe = sco::Result::Ok, sinkCapability = sco::Result::Ok;
static void SinkEvent(const char*, const void*, void*) {}
static void ReentrantStartupSink(const char* line) {
    if (!std::strstr(line, "[plugin]")) return;
    reentrantSinkFired = true;
    static int owner, table;
    const void* found = nullptr;
    sinkProvide = sco::ProvideService(&owner, "rogue", "rogue.table", 1u << 16, &table);
    sinkQuery = sco::QueryService("sco.ui", 1u << 16, &found);
    sinkSubscribe = sco::Subscribe(&owner, "test", SinkEvent, nullptr);
    sinkCapability = sco::caps::Set("game.unsupported", true);
    sinkEmptyPlugins = sco::app::Plugins().empty();
}
static std::promise<void>* tickEntered = nullptr;
static std::shared_future<void> tickContinue;
static void BlockingTask(void*) { tickEntered->set_value(); tickContinue.wait(); }
static void FaultTask(void*) { faultOwner->fault(); }
static void FaultDuringAppStart(const char* line) {
    if (std::strstr(line, "[plugin]")) { faultSinkFired = true; faultOwner->fault(); }
}
static void ThrowingSink(const char*) { throw std::runtime_error("synthetic log sink failure"); }
static sco::Result registryReentryResult = sco::Result::Ok;
static void RegistryReentry(void* context) noexcept {
    registryReentryResult = gh::Prepare(*static_cast<gh::Options*>(context));
}
static gh::Admission Inspect() {
    ++inspections;
    if (reenterStop) { reenterStop = false; reentryResult = gh::Stop(); }
    if (faultAfter && --faultAfter == 0) {
        std::thread other([] { faultOwner->fault(); });
        other.join();
    }
    return evidence;
}
static int ticks = 0, tasks = 0;
static int shutdownCalls = 0, exitCalls = 0, serviceStops = 0;
static bool productClosed = false, controlTick = false, partialStartup = false;
static int shutdownContext;
static bool barrierEnabled = true;
static std::string barrierPolicy;
static int barrierCalls = 0;
static bool NativeBarrier(void* ctx) {
    CHECK(ctx == &shutdownContext);
    ++barrierCalls;
    CHECK(gh::Prepare(gh::Options{}) == sco::Result::BadArg);
    CHECK(gh::Activate() == sco::Result::BadArg);
    CHECK(gh::Start() == sco::Result::BadArg);
    if (!barrierEnabled && barrierPolicy == "throw") throw std::runtime_error("synthetic barrier fault");
    return barrierEnabled;
}
static std::string shutdownPolicy = "true";
static gh::ConsumerRetirement RetireConsumer(void* ctx) {
    ++shutdownCalls;
    CHECK(ctx == &shutdownContext && gh::Status().phase == (partialStartup ? gh::Phase::Starting : gh::Phase::Stopping));
    CHECK(!gh::FreshAdmissionAllowed() && exitCalls == 0 && serviceStops == 0);
    CHECK(gh::Activate() == sco::Result::BadArg); // lifecycle reentry cannot activate during retirement
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
    const bool ownedMissing = mode == "owned-missing";
    const bool ownedLegacy = mode == "owned-legacy";
    const bool ownedTimeout = mode == "owned-timeout";
    const bool offthreadTimeout = mode == "owned-offthread-timeout";
    const bool offthreadDrain = mode == "owned-offthread-drain";
    const bool legacyCapability = mode == "owned-legacy-capability";
    const bool legacyAppReference = mode == "owned-legacy-app-reference";
    const bool faultPrepare = mode == "owned-prepare-fault";
    const bool faultActivate = mode == "owned-activation-fault";
    const bool faultStart = mode == "owned-start-fault";
    const bool tickReentry = mode == "owned-tick-reentry";
    const bool appStartFault = mode == "owned-app-start-fault";
    const bool afterStartThrow = mode == "owned-after-start-throw";
    const bool activationLogThrow = mode == "owned-activation-log-throw";
    const bool admissionLoss = mode == "owned-admission-loss";
    const bool runningFault = mode == "owned-running-fault";
    const bool budgetFailure = mode == "owned-prepare-budget";
    const bool tickFault = mode == "owned-tick-fault";
    const bool taskFault = mode == "owned-task-fault";
    const bool startupReentry = mode == "owned-startup-reentry";
    const bool registryReentry = mode == "owned-registry-reentry";
    const bool existingRegistry = mode == "owned-existing-registry";
    const bool legacyRuntime = mode == "owned-legacy-runtime";
    const bool ownedGood = mode == "owned-good" || ownedTimeout || offthreadTimeout || offthreadDrain || legacyCapability || legacyAppReference || faultPrepare || faultActivate || faultStart ||
        tickReentry || appStartFault || afterStartThrow || activationLogThrow || admissionLoss || runningFault || budgetFailure || tickFault || taskFault || startupReentry || registryReentry || existingRegistry || legacyRuntime;
    const bool failureStop = mode.rfind("shutdown-", 0) == 0;
    const bool failureLoss = mode.rfind("loss-", 0) == 0;
    const bool failureQuit = mode.rfind("quit-", 0) == 0;
    const bool barrierFailure = mode.rfind("barrier-", 0) == 0;
    if (barrierFailure) {
        barrierPolicy = mode.substr(8);
        if (barrierPolicy != "null" && barrierPolicy != "false" && barrierPolicy != "throw") return 2;
    }
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
    if (barrierFailure) {
        o.platform.gameServices = true;
        o.platform.gameReadersQuiesced = barrierPolicy == "null" ? nullptr : NativeBarrier;
        o.platform.gameReadersContext = &shutdownContext;
    }
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
    auto ownerToken = std::make_shared<int>(1); o.imageOwner = ownerToken; o.resolverOwner = ownerToken;
    if (!ownedLegacy && !ownedGood) {
        CHECK(gh::Prepare(o) == sco::Result::Ok);
        CHECK(gh::Activate() == sco::Result::Unavailable);
        CHECK(!sco::AcquireHostSignatureRegistry() && loads == 0);
        std::printf("legacy host scenario %s: activation refused (unowned consumers)\n", mode.c_str());
        // Named legacy lifecycle scenarios below did not execute their original
        // assertions. Only the dedicated refusal case is a pass.
        return failures ? 1 : (mode == "refusal" || ownedMissing ? 0 : 77);
    }
    if (missingQuit) {
        o.platform.gameReadersQuiesced = NativeBarrier; o.platform.gameReadersContext = &shutdownContext;
        CHECK(gh::Prepare(o) == sco::Result::Ok);
        CHECK(!gh::Status().quitHookInstalled && loads == 0);
        CHECK(gh::Activate() == sco::Result::Unavailable);
        CHECK(gh::Status().phase == gh::Phase::Failed && !gh::Status().quitHookInstalled);
        CHECK(gh::Start() == sco::Result::BadArg && loads == 0);
        return failures ? 1 : 0;
    }
    if (ownedLegacy || ownedGood) o.shutdownOwner = std::make_shared<sco::OwnedShutdown>();
    if (ownedGood) {
        if (offthreadTimeout) o.drainTimeoutMs = 120;
        o.platform.pluginsEnabled = false; o.platform.nBuiltins = 0;
        o.platform.privateSession.enabled = false;
        o.platform.hostService = {}; o.dataCoreHook = gh::DataCoreHook::External;
    }
    if (ownedMissing || ownedLegacy || ownedGood) {
        faultOwner = o.shutdownOwner;
        if (registryReentry) {
            const auto start = std::chrono::steady_clock::now();
            CHECK(sco::WithNoHostSignatureRegistry(RegistryReentry, &o));
            CHECK(registryReentryResult == sco::Result::Unavailable && !faultOwner->open());
            CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
            CHECK(gh::Status().phase == gh::Phase::Failed && !sco::AcquireHostSignatureRegistry());
            return failures ? 1 : 0;
        }
        if (existingRegistry) {
            auto existing = std::make_shared<sco::SignatureRegistry>();
            CHECK(existing->Freeze() == sco::SigRegistryStatus::Ok);
            CHECK(existing->Resolve(image) == sco::SigRegistryStatus::Ok);
            CHECK(sco::InstallHostSignatureRegistry(existing, nullptr));
            CHECK(gh::Prepare(o) == sco::Result::Unavailable);
            CHECK(!faultOwner->open() && sco::AcquireHostSignatureRegistry() == existing);
            CHECK(!sco::detail::RuntimeStopping());
            CHECK(gh::Status().phase == gh::Phase::Failed);
            return failures ? 1 : 0;
        }
        if (legacyAppReference) {
            const auto& existing = sco::app::Plugins();
            CHECK(existing.empty());
            CHECK(gh::Prepare(o) == sco::Result::Unavailable && !faultOwner->open());
            CHECK(gh::Status().phase == gh::Phase::Failed && existing.empty());
            return failures ? 1 : 0;
        }
        if (legacyCapability) {
            CHECK(sco::caps::Set("fixture.early", true) == sco::Result::Ok);
            CHECK(gh::Prepare(o) == sco::Result::Unavailable && !faultOwner->open());
            CHECK(sco::caps::Has("fixture.early"));
            CHECK(gh::Status().phase == gh::Phase::Failed);
            return failures ? 1 : 0;
        }
        if (legacyRuntime) {
            static int legacyOwner, legacyTable;
            CHECK(sco::ProvideService(&legacyOwner, "fixture", "fixture.legacy", 1u << 16, &legacyTable) == sco::Result::Ok);
            CHECK(gh::Prepare(o) == sco::Result::Unavailable && !faultOwner->open());
            const void* table = nullptr;
            CHECK(sco::QueryService("fixture.legacy", 1u << 16, &table) == sco::Result::Ok && table == &legacyTable);
            auto existing = std::make_shared<sco::SignatureRegistry>();
            CHECK(existing->Freeze() == sco::SigRegistryStatus::Ok && existing->Resolve(image) == sco::SigRegistryStatus::Ok);
            CHECK(sco::InstallHostSignatureRegistry(existing, nullptr)); // unused reservation was released
            return failures ? 1 : 0;
        }
        if (budgetFailure) {
            std::weak_ptr<int> retained = ownerToken;
            o.signatureRegistryBudgetBytes = 1;
            CHECK(gh::Prepare(o) == sco::Result::Failed);
            o.imageOwner.reset(); o.resolverOwner.reset(); ownerToken.reset();
            CHECK(!retained.expired() && !faultOwner->open());
            CHECK(gh::Status().phase == gh::Phase::Failed && gh::Start() == sco::Result::BadArg);
            return failures ? 1 : 0;
        }
        if (faultPrepare) {
            faultAfter = 1;
            CHECK(gh::Prepare(o) == sco::Result::Unavailable);
            CHECK(gh::Status().phase == gh::Phase::Failed && !faultOwner->open());
            CHECK(gh::Prepare(o) == sco::Result::BadArg && !sco::AcquireHostSignatureRegistry());
            return failures ? 1 : 0;
        }
        CHECK(gh::Prepare(o) == sco::Result::Ok);
        if (ownedGood) {
            if (activationLogThrow) {
                gh::testing::LogAfterRegistryInstall(true);
                sco::SetLogSink(ThrowingSink);
                const auto result = gh::Activate();
                sco::SetLogSink(nullptr);
                CHECK(result == sco::Result::Failed && !faultOwner->open());
                CHECK(gh::Status().phase == gh::Phase::Failed && gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                CHECK(gh::Start() == sco::Result::BadArg);
                return failures ? 1 : 0;
            }
            if (faultActivate) {
                faultAfter = 2;
                CHECK(gh::Activate() == sco::Result::Unavailable);
                CHECK(gh::Status().phase == gh::Phase::Failed && !faultOwner->open());
                CHECK(gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                CHECK(gh::Start() == sco::Result::BadArg);
                return failures ? 1 : 0;
            }
            CHECK(gh::Activate() == sco::Result::Ok);
            if (appStartFault || afterStartThrow) {
                CHECK(gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                if (appStartFault) sco::SetLogSink(FaultDuringAppStart);
                if (afterStartThrow) gh::testing::FailAfterAppStart(true);
                const auto result = gh::Start();
                sco::SetLogSink(nullptr);
                CHECK(result == sco::Result::Failed);
                CHECK(!appStartFault || faultSinkFired);
                CHECK(!faultOwner->open() && sco::detail::RuntimeStopping());
                CHECK(!sco::net::Started() && sco::net::testing::RawStarted() && gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                CHECK(gh::Status().phase == gh::Phase::Failed && gh::Start() == sco::Result::BadArg);
                return failures ? 1 : 0;
            }
            if (faultStart) {
                CHECK(gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                faultAfter = 1;
                CHECK(gh::Start() == sco::Result::Failed);
                CHECK(gh::Status().phase == gh::Phase::Failed && !faultOwner->open());
                CHECK(gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                CHECK(gh::Start() == sco::Result::BadArg);
                return failures ? 1 : 0;
            }
            if (startupReentry) sco::SetLogSink(ReentrantStartupSink);
            CHECK(gh::Start() == sco::Result::Ok);
            if (startupReentry) {
                sco::SetLogSink(nullptr);
                CHECK(reentrantSinkFired && sinkProvide == sco::Result::Unavailable &&
                      sinkQuery == sco::Result::Unavailable && sinkSubscribe == sco::Result::Unavailable &&
                      sinkCapability == sco::Result::Unavailable);
                CHECK(!sco::caps::Has("game.unsupported") && sinkEmptyPlugins);
                const void* ui = nullptr;
                CHECK(sco::QueryService("sco.ui", 1u << 16, &ui) == sco::Result::Ok && ui);
            }
            if (taskFault) {
                CHECK(gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                CHECK(sco::detail::PostOwned(FaultTask, nullptr, nullptr, nullptr) == sco::Result::Ok);
                CHECK(sco::detail::PostOwned(Task, nullptr, nullptr, nullptr) == sco::Result::Ok);
                CHECK(gh::Tick(1) == sco::Result::Unavailable);
                CHECK(tasks == 0 && sco::QueuedTasks() == 1 && sco::detail::RuntimeStopping());
                CHECK(gh::Status().phase == gh::Phase::Failed && gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                return failures ? 1 : 0;
            }
            if (tickFault) {
                std::promise<void> entered, resume;
                tickEntered = &entered; tickContinue = resume.get_future().share();
                CHECK(sco::Post(BlockingTask, nullptr) == sco::Result::Ok);
                std::thread fault([&] { entered.get_future().wait(); faultOwner->fault(); resume.set_value(); });
                CHECK(gh::Tick(1) == sco::Result::Unavailable);
                fault.join();
                CHECK(sco::detail::RuntimeStopping() && gh::Status().phase == gh::Phase::Failed);
                CHECK(!sco::net::Started() && sco::net::testing::RawStarted() && gh::Start() == sco::Result::BadArg);
                return failures ? 1 : 0;
            }
            if (runningFault) {
                CHECK(gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                CHECK(gh::FreshAdmissionAllowed());
                CHECK(sco::Post(Task, nullptr) == sco::Result::Ok);
                CHECK(!sco::app::Start(sco::app::Platform{}));
                std::thread fault([] { faultOwner->fault(); }); fault.join();
                CHECK(sco::detail::RuntimeStopping());
                sco::app::Tick(1);
                CHECK(tasks == 0 && sco::QueuedTasks() == 1);
                CHECK(!sco::app::Start(sco::app::Platform{}) && sco::detail::RuntimeStopping());
                CHECK(!gh::FreshAdmissionAllowed() && gh::Tick(1) == sco::Result::Unavailable);
                CHECK(gh::Status().phase == gh::Phase::Failed && gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                return failures ? 1 : 0;
            }
            if (admissionLoss) {
                CHECK(gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                evidence.session = gh::Session::Unknown;
                const auto begin = std::chrono::steady_clock::now();
                CHECK(gh::Tick(1) == sco::Result::Unavailable);
                CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(1));
                CHECK(gh::Status().phase == gh::Phase::Failed && gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                return failures ? 1 : 0;
            }
            if (tickReentry) {
                reenterStop = true;
                const auto begin = std::chrono::steady_clock::now();
                CHECK(gh::Tick(1) == sco::Result::Ok);
                CHECK(reentryResult == sco::Result::Unavailable);
                CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(1));
                CHECK(gh::Status().phase == gh::Phase::Running);
            }
            if (offthreadDrain) {
                std::promise<void> entered;
                std::atomic<bool> waited{false};
                std::thread reader([&] {
                    auto lease = o.shutdownOwner->try_read();
                    if (!lease) { entered.set_exception(std::make_exception_ptr(std::runtime_error("reader was not admitted"))); return; }
                    entered.set_value();
                    if (o.shutdownOwner->wait_until_closed(std::chrono::steady_clock::now() + std::chrono::seconds(2))) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        waited = gh::Status().phase == gh::Phase::Stopping;
                    }
                });
                entered.get_future().get();
                CHECK(gh::Stop() == sco::Result::Failed);
                reader.join();
                CHECK(waited && gh::Status().phase == gh::Phase::Failed);
                CHECK(gh::testing::HasPrivateRegistry() && !o.shutdownOwner->try_read());
                CHECK(gh::Start() == sco::Result::BadArg);
                return failures ? 1 : 0;
            }
            if (offthreadTimeout) {
                std::promise<void> entered, release;
                auto proceed = release.get_future().share();
                std::thread reader([&] {
                    auto lease = o.shutdownOwner->try_read();
                    if (!lease) { entered.set_exception(std::make_exception_ptr(std::runtime_error("reader was not admitted"))); return; }
                    entered.set_value();
                    proceed.wait();
                });
                entered.get_future().get();
                CHECK(sco::Post(Task, nullptr) == sco::Result::Ok);
                const auto began = std::chrono::steady_clock::now();
                CHECK(gh::Stop() == sco::Result::Failed);
                const auto elapsed = std::chrono::steady_clock::now() - began;
                CHECK(elapsed >= std::chrono::milliseconds(90) && elapsed < std::chrono::seconds(10));
                CHECK(sco::detail::RuntimeStopping() && tasks == 0 && sco::QueuedTasks() == 1);
                CHECK(!o.shutdownOwner->ready() && !o.shutdownOwner->try_read());
                CHECK(gh::testing::HasPrivateRegistry() && gh::Status().phase == gh::Phase::Failed);
                release.set_value();
                reader.join();
                CHECK(!o.shutdownOwner->ready() && gh::Start() == sco::Result::BadArg);
                return failures ? 1 : 0;
            }
            if (ownedTimeout) {
                auto read = o.shutdownOwner->try_read();
                CHECK(read);
                CHECK(sco::Post(Task, nullptr) == sco::Result::Ok);
                CHECK(gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                CHECK(gh::Stop() == sco::Result::Failed);
                CHECK(sco::detail::RuntimeStopping() && tasks == 0 && sco::QueuedTasks() == 1);
                CHECK(!o.shutdownOwner->ready() && !o.shutdownOwner->try_read());
                CHECK(gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
                CHECK(gh::Status().phase == gh::Phase::Failed && shutdownCalls == 0);
                return failures ? 1 : 0;
            }
            CHECK(gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
            CHECK(gh::Stop() == sco::Result::Failed);
            CHECK(!o.shutdownOwner->ready() && !o.shutdownOwner->try_read());
            CHECK(shutdownCalls == 0 && gh::testing::HasPrivateRegistry() && !sco::AcquireHostSignatureRegistry());
            CHECK(gh::Status().phase == gh::Phase::Failed && gh::Start() == sco::Result::BadArg);
            return failures ? 1 : 0;
        }
        CHECK(gh::Activate() == sco::Result::Unavailable);
        CHECK(!sco::AcquireHostSignatureRegistry() && loads == 0);
        CHECK(gh::Status().phase == gh::Phase::Prepared);
        return failures ? 1 : 0;
    }
    CHECK(gh::Prepare(o) == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Prepared);
    CHECK(gh::Prepare(o) == sco::Result::BadArg);
    CHECK(loads == 0 && !gh::Status().quitHookInstalled && !gh::Status().dataCoreActive);
    if (barrierFailure) {
        barrierEnabled = false;
        CHECK(!sco::AcquireHostSignatureRegistry());
        CHECK(gh::Activate() == sco::Result::Unavailable);
        CHECK(gh::Status().phase == gh::Phase::Prepared);
        CHECK(!sco::AcquireHostSignatureRegistry() && loads == 0 && !gh::Status().dataCoreActive);
        if (barrierPolicy != "null") {
            barrierEnabled = true;
            CHECK(gh::Activate() == sco::Result::Ok);
            CHECK(gh::Status().phase == gh::Phase::Activated && sco::AcquireHostSignatureRegistry());
        }
        return failures ? 1 : 0;
    }
    CHECK(!sco::AcquireHostSignatureRegistry());
    CHECK(gh::Activate() == sco::Result::Ok);
    CHECK(gh::Activate() == sco::Result::BadArg);
    evidence.session = gh::Session::Online; // revalidate before publication
    CHECK(gh::Start() == sco::Result::Unavailable);
    CHECK(gh::Status().phase == gh::Phase::Activated);
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
    if (barrierFailure) {
        barrierEnabled = false;
        CHECK(gh::Stop() == sco::Result::Failed);
        CHECK(gh::Status().phase == gh::Phase::Failed && sco::AcquireHostSignatureRegistry() == owner);
        CHECK(!sco::caps::Has("fixture.shutdown") && !gh::FreshAdmissionAllowed());
        CHECK(productClosed && shutdownCalls == 1 && exitCalls == 0 && serviceStops == 0);
        CHECK(sco::QueuedTasks() == 1);
        CHECK(barrierPolicy == "null" ? barrierCalls == 0 : barrierCalls >= 1);
        CHECK(gh::Stop() == sco::Result::Unavailable);
        std::printf("game_host %s: %d checks, %d failures\n", mode.c_str(), checks, failures);
        return failures ? 1 : 0;
    }
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
