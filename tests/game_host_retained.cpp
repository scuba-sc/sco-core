// Synthetic process-retained host composition; no game image, data, process, hook or network.
// One process per mode (see CMakeLists.txt). Nothing here proves behavior inside the game.
#include "sco/game/host.h"
#include "../src/game/retained_host.h"
#include "sco/runtime.h"
#include "sco/caps.h"
#include "sco/signature_registry.h"
#include "sco/log.h"
#include "../src/api/internal.h"
#include "../src/api/managed_service_gate.h"
#include "../src/app/process_retained.h"
#include "../src/owned_shutdown_access.h"
#include "sc_private_session.h"
#include "sco_diagnostics_publisher.h"
#include "sco/hook.h"
#include "sco/game/pak.h"
#include "sco/net/session.h"
#ifdef SCO_GAME_SERVICES
#include "sco/game/services.h"
#include "../src/game/services/process_retained.h"
#endif
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace gh = sco::game::host;
static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

static gh::Admission evidence{gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
static gh::Admission Inspect() { return evidence; }

static std::string barrierPolicy = "true";
static int barrierCalls = 0, barrierContext = 0, barrierFalseFrom = 0;
static bool Barrier(void* context) {
    CHECK(context == &barrierContext);
    ++barrierCalls;
    if (barrierFalseFrom && barrierCalls >= barrierFalseFrom) return false;
    if (barrierPolicy == "throw") throw std::runtime_error("synthetic barrier fault");
    return barrierPolicy == "true";
}

static std::vector<std::string> logLines;
static void Sink(const char* line) { logLines.emplace_back(line); }
static bool Logged(const char* text) {
    for (const auto& l : logLines) if (l.find(text) != std::string::npos) return true;
    return false;
}

static int ticks = 0, tasks = 0, exitCalls = 0, loads = 0, unloads = 0;
static int hostStarts = 0, hostTicks = 0, hostStops = 0;
static bool requestStopInTick = false, quitInExit = false;
static gh::HostReport calloutReport;
static int quitOriginals = 0;
static void* quitSystem = nullptr;
static void* quitInfo = nullptr;
static int quitCode = 0;
static int exitsSeenByOriginal = 0;
static bool quitInTick = false;
static void TickEvent(const char*, const void*, void*) {
    ++ticks;
    if (quitInTick) { quitInTick = false; gh::retained::testing::InvokeQuitDetourForTest(nullptr, nullptr, 5); }
    if (requestStopInTick) { requestStopInTick = false; calloutReport = gh::RequestStopDetailed(); }
}
static void ExitEvent(const char*, const void*, void*) {
    ++exitCalls;
    if (quitInExit) {
        quitInExit = false;
        gh::retained::testing::InvokeQuitDetourForTest(nullptr, nullptr, 1); // reentrant quit inside the closure
    }
}
static void Task(void*) { ++tasks; }
static const sco_plugin_info* Query() {
    static const sco_plugin_info info{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "synthetic", "1.0.0", "tests"};
    return &info;
}
static sco_plugin* gSelf = nullptr;
static const sc_private_session_v1* gSession = nullptr;
static const sco_diagnostics_publisher_v1* gPublisher = nullptr;
static sco_result Load(const sco_api* api, sco_plugin* self) {
    ++loads;
    gSelf = self;
    const void* table = nullptr;
    if (api->query_service(SC_PRIVATE_SESSION_NAME, SC_PRIVATE_SESSION_VERSION, &table) == SCO_OK)
        gSession = static_cast<const sc_private_session_v1*>(table);
    table = nullptr;
    if (api->query_service(SCO_DIAGNOSTICS_PUBLISHER_NAME, SCO_DIAGNOSTICS_PUBLISHER_VERSION_1_0, &table) == SCO_OK)
        gPublisher = static_cast<const sco_diagnostics_publisher_v1*>(table);
    if (api->subscribe(self, "tick", TickEvent, nullptr) != SCO_OK) return SCO_FAILED;
    return api->subscribe(self, "game.exit", ExitEvent, nullptr);
}
static void Unload() { ++unloads; }
static const sco::plugins::Builtin builtin{"synthetic", Query, Load, Unload};
static sco::Result HostServiceStart(void*) { ++hostStarts; return sco::Result::Ok; }
static sco::Result HostServiceTick(void*) { ++hostTicks; return sco::Result::Ok; }
static sco::Result HostServiceStop(void*) { ++hostStops; return sco::Result::Ok; }
static void SetCaps() { CHECK(sco::caps::Set("fixture.retained", true) == sco::Result::Ok); }

static uint8_t bytes[8192]{};
static sco::Image image{bytes, {bytes, 4096}, {bytes + 4096, 4096}, {}, 0, sizeof(bytes)};
static std::shared_ptr<int> backing = std::make_shared<int>(1);

static gh::Options Retained(std::shared_ptr<sco::OwnedShutdown> owner = nullptr) {
    gh::Options o;
    o.composition = gh::Composition::ProcessRetained;
    o.inspect = Inspect;
    o.quit = gh::QuitPolicy::External;
    o.dataCoreHook = gh::DataCoreHook::External;
    o.image = &image; o.imageOwner = backing; o.resolverOwner = backing;
    o.shutdownOwner = std::move(owner);
    o.platform.pluginsEnabled = false;
    o.platform.privateSession.enabled = false;
    o.platform.builtins = &builtin; o.platform.nBuiltins = 1;
    o.platform.hostService = {&barrierContext, HostServiceStart, HostServiceTick, HostServiceStop};
    o.platform.setCapabilities = SetCaps;
    o.platform.gameReadersQuiesced = Barrier;
    o.platform.gameReadersContext = &barrierContext;
    return o;
}
static gh::Options Strict() {
    gh::Options o;
    o.inspect = Inspect;
    o.quit = gh::QuitPolicy::External;
    o.image = &image; o.imageOwner = backing; o.resolverOwner = backing;
    o.platform.pluginsEnabled = false;
    return o;
}
static bool Untouched() {
    return !sco::AcquireHostSignatureRegistry() && gh::Status().phase == gh::Phase::Idle &&
           !gh::Status().signaturesResolved;
}

static void OriginalQuit(void* system, void* info, int code) {
    ++quitOriginals; quitSystem = system; quitInfo = info; quitCode = code;
    exitsSeenByOriginal = exitCalls;
    CHECK(gh::Status().phase == gh::Phase::Stopped);
}

// Prepare + Start with the shared options; the test thread is the game thread.
static bool Up(const gh::Options& o) {
    CHECK(gh::Prepare(o) == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Prepared && gh::Status().signaturesResolved);
    CHECK(gh::Start() == sco::Result::Ok);
    return gh::Status().phase == gh::Phase::Running;
}

static int Admission() {
    auto o = Retained();
    evidence.session = gh::Session::Unknown;
    CHECK(gh::Prepare(o) == sco::Result::Unavailable && Untouched()); // Unknown
    evidence = {gh::Session::Online, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
    CHECK(gh::Prepare(o) == sco::Result::Unavailable && Untouched());
    evidence.session = gh::Session::Offline;
    evidence.antiCheat = gh::Check::Present;
    CHECK(gh::Prepare(o) == sco::Result::Unavailable && Untouched());
    evidence.antiCheat = gh::Check::Clear;
    evidence.officialNetwork = gh::Check::Unknown;
    CHECK(gh::Prepare(o) == sco::Result::Unavailable && Untouched());
    evidence.officialNetwork = gh::Check::Clear;
    evidence.targetProcess = gh::Check::Unknown;
    CHECK(gh::Prepare(o) == sco::Result::Unavailable && Untouched());
    evidence.targetProcess = gh::Check::Present;
    CHECK(gh::Prepare(o) == sco::Result::Unavailable && Untouched());
    CHECK(barrierCalls == 0 && loads == 0 && hostStarts == 0); // refused before any hook-side step
    // Invalid retained options also leave the host Idle.
    evidence.targetProcess = gh::Check::Clear;
    auto bad = Retained(); bad.platform.shutdownOwner = std::make_shared<sco::OwnedShutdown>();
    CHECK(gh::Prepare(bad) == sco::Result::BadArg && Untouched());
    bad = Retained(); bad.retireConsumer = [](void*) { return gh::ConsumerRetirement::Quiesced; };
    CHECK(gh::Prepare(bad) == sco::Result::BadArg && Untouched());
    bad = Retained(); bad.imageOwner.reset();
    CHECK(gh::Prepare(bad) == sco::Result::BadArg && Untouched());
    CHECK(gh::Prepare(o) == sco::Result::Ok); // refusals did not poison a later attempt
    return 0;
}

static int Barrier_(const std::string& policy) {
    barrierPolicy = policy;
    auto owner = std::make_shared<sco::OwnedShutdown>();
    auto o = Retained(owner);
    if (policy == "null") o.platform.gameReadersQuiesced = nullptr;
    CHECK(gh::Prepare(o) == (policy == "null" ? sco::Result::BadArg : sco::Result::Unavailable));
    CHECK(Untouched() && loads == 0 && hostStarts == 0);
    CHECK(barrierCalls == (policy == "null" ? 0 : 1));
    { auto read = owner->try_read(); CHECK(read); } // the owner was not touched by the refusal
    CHECK(gh::Prepare(Strict()) == sco::Result::BadArg); // the attempt selected the retained composition
    CHECK(Untouched());
    barrierPolicy = "true";
    CHECK(gh::Prepare(Retained(owner)) == sco::Result::Ok); // same composition may retry
    CHECK(sco::AcquireHostSignatureRegistry() && !owner->try_read());
    return 0;
}

static int Lifecycle() {
    const auto none = Retained(); // no shutdownOwner: works end to end
    CHECK(Up(none));
    CHECK(gh::Prepare(none) == sco::Result::BadArg && gh::Start() == sco::Result::BadArg);
    const auto registry = sco::AcquireHostSignatureRegistry();
    CHECK(registry && gh::FreshAdmissionAllowed());
    CHECK(loads == 1 && hostStarts == 1 && sco::caps::Has("fixture.retained"));
    // The runtime is the normal one: what an owned runtime refuses works from here.
    static int owner, table;
    CHECK(sco::Subscribe(&ticks, "tick", TickEvent, nullptr) == sco::Result::Ok);
    CHECK(sco::ProvideService(&owner, "fixture", "fixture.retained", 1u << 16, &table) == sco::Result::Ok);
    CHECK(sco::app::Plugins().size() == 1 && sco::app::Plugins()[0].state == sco::plugins::State::Loaded);
    CHECK(sco::Post(Task, nullptr) == sco::Result::Ok);
    CHECK(gh::Tick(0xfffffff0u) == sco::Result::Ok); // first tick, even near wrap
    CHECK(tasks == 1 && ticks == 2 && hostTicks == 1);
    CHECK(sco::Post(Task, nullptr) == sco::Result::Ok);
    CHECK(gh::Tick(0x20u) == sco::Result::Ok); // throttled across wrap
    CHECK(tasks == 1 && ticks == 2);
    CHECK(gh::Tick(0x54u) == sco::Result::Ok);
    CHECK(tasks == 2 && ticks == 4);
    sco::Result wrongTick = sco::Result::Ok, wrongStop = sco::Result::Ok;
    std::thread other([&] { wrongTick = gh::Tick(500); wrongStop = gh::Stop(); });
    other.join();
    CHECK(wrongTick == sco::Result::WrongThread && wrongStop == sco::Result::WrongThread);
    CHECK(gh::Status().phase == gh::Phase::Running);
    CHECK(sco::Post(Task, nullptr) == sco::Result::Ok);

    CHECK(gh::Stop() == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Stopped && exitCalls == 1);
    // Closure only: nothing was unloaded, withdrawn or released.
    CHECK(unloads == 0 && hostStops == 0);
    CHECK(sco::app::Plugins().size() == 1 && sco::app::Plugins()[0].state == sco::plugins::State::Loaded);
    CHECK(sco::AcquireHostSignatureRegistry() == registry && registry->Snapshot());
    CHECK(sco::QueuedTasks() == 1 && tasks == 2); // queued work is neither run nor dropped
    CHECK(!gh::FreshAdmissionAllowed() && !sco::caps::Has("fixture.retained"));
    CHECK(sco::Subscribe(&ticks, "tick", TickEvent, nullptr) == sco::Result::Unavailable);
    CHECK(sco::detail::RuntimeStopping());
    CHECK(gh::Stop() == sco::Result::Ok && exitCalls == 1); // second Stop: Ok, no second game.exit
    CHECK(gh::BeforeQuit() == sco::Result::Ok && exitCalls == 1);
    CHECK(gh::Tick(900) == sco::Result::Unavailable && ticks == 4);
    CHECK(gh::Start() == sco::Result::BadArg && gh::Prepare(none) == sco::Result::BadArg);
    CHECK(!sco::app::Start(sco::app::Platform{})); // no restart through the app either
    CHECK(Logged("process-retained stop: admission closed, backing retained"));
    return 0;
}

static int Quit() {
    gh::retained::testing::SetQuitOriginalForTest(OriginalQuit);
    int a = 0, b = 0;
    CHECK(Up(Retained()));
    CHECK(sco::Subscribe(&a, "game.exit", ExitEvent, nullptr) == sco::Result::Ok);
    quitInExit = true; // the reentrant quit comes from a game.exit subscriber, during closure
    gh::retained::testing::InvokeQuitDetourForTest(&a, &b, 73);
    CHECK(quitOriginals == 1 && quitSystem == &a && quitInfo == &b && quitCode == 73);
    CHECK(exitsSeenByOriginal == 2 && exitCalls == 2); // two subscribers, one dispatch of game.exit
    CHECK(gh::Status().phase == gh::Phase::Stopped);
    gh::retained::testing::InvokeQuitDetourForTest(&b, &a, -7); // after closure: forwards again, no second closure
    CHECK(quitOriginals == 2 && quitSystem == &b && quitInfo == &a && quitCode == -7 && exitCalls == 2);
    return 0;
}

static int QuitRequired() {
    auto o = Retained();
    o.quit = gh::QuitPolicy::Required;
    // The synthetic image has no quit row: the hook cannot be installed, and no hook is.
    CHECK(gh::Prepare(o) == sco::Result::Unavailable);
    CHECK(gh::Status().phase == gh::Phase::Failed && !gh::Status().quitHookInstalled);
    CHECK(barrierCalls == 1); // checked before the first native mutation
    CHECK(sco::AcquireHostSignatureRegistry()); // exposed state is retained, not rolled back
    CHECK(gh::Start() == sco::Result::BadArg && gh::Prepare(o) == sco::Result::BadArg);
    return 0;
}

static int AdmissionLoss() {
    auto owner = std::make_shared<sco::OwnedShutdown>();
    CHECK(Up(Retained(owner)));
    const auto registry = sco::AcquireHostSignatureRegistry();
    { auto read = owner->try_read(); CHECK(read); }
    evidence.session = gh::Session::Unknown;
    CHECK(gh::Tick(1) == sco::Result::Unavailable);
    CHECK(gh::Status().phase == gh::Phase::Stopped && exitCalls == 1 && ticks == 0 && hostTicks == 0);
    CHECK(sco::AcquireHostSignatureRegistry() == registry && unloads == 0 && hostStops == 0);
    CHECK(sco::app::Plugins().size() == 1);
    CHECK(!owner->try_read() && owner->drain_state() == sco::OwnedShutdown::DrainState::Requested);
    CHECK(!gh::FreshAdmissionAllowed());
    return 0;
}

static int Exclusive() {
    CHECK(gh::Prepare(Retained()) == sco::Result::Ok);
    CHECK(gh::Prepare(Strict()) == sco::Result::BadArg);
    auto detailed = Strict();
    detailed.shutdownOwner = std::make_shared<sco::OwnedShutdown>();
    detailed.dataCoreHook = gh::DataCoreHook::External;
    detailed.platform.pluginsEnabled = false;
    detailed.platform.privateSession.enabled = false;
    auto capsule = std::make_shared<const gh::HostCapsule>(detailed, backing);
    const auto report = gh::PrepareDetailed(capsule);
    CHECK(report.result == sco::Result::BadArg);
    // The refusal is the composition claim, not a malformed capsule: it is reported as retained.
    CHECK(report.dependencies == gh::DependencyDisposition::ProcessRetained && report.sessionId == 0);
    CHECK(gh::Status().phase == gh::Phase::Prepared);
    CHECK(gh::Prepare(Retained()) == sco::Result::BadArg); // already prepared
    return 0;
}

static int ExclusiveStrictFirst() {
    CHECK(gh::Prepare(Strict()) == sco::Result::Ok); // strict legacy Prepare selects the strict composition
    CHECK(gh::Status().phase == gh::Phase::Prepared);
    CHECK(gh::Prepare(Retained()) == sco::Result::BadArg);
    CHECK(gh::Status().phase == gh::Phase::Prepared && barrierCalls == 0 && loads == 0);
    CHECK(gh::Start() == sco::Result::Unavailable); // the strict machine answers (activation needs an owner)
    CHECK(gh::Activate() == sco::Result::Unavailable);
    return 0;
}

#ifdef SCO_GAME_SERVICES
// gameServices through the retained composition: the internal start entry runs (the public one still
// refuses), with the shell's barrier checked again at Start. On the synthetic image the rows are
// unresolved; the real start logs that and publishes its services anyway.
static int ServicesStart() {
    auto o = Retained();
    o.platform.gameServices = true;
    CHECK(sco::game::services::Start() == sco::Result::Unavailable); // the public entry is unchanged
    CHECK(gh::Prepare(o) == sco::Result::Ok);
    const int atPrepare = barrierCalls;
    CHECK(gh::Start() == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Running);
    CHECK(barrierCalls == atPrepare + 2); // StartSubsystems, then the entry itself, before the start
    CHECK(sco::game::services::Started());
    CHECK(Logged("[game] game services published"));
    const void* table = nullptr;
    CHECK(sco::QueryService("teleport.spatial", 1u << 16, &table) == sco::Result::Ok && table);
    CHECK(sco::game::services::Start() == sco::Result::Unavailable); // still refused for everyone else
    CHECK(gh::Tick(0) == sco::Result::Ok); // the services' tick subscriber runs on the normal runtime
    CHECK(gh::Stop() == sco::Result::Ok && gh::Status().phase == gh::Phase::Stopped);
    // Closure only: the services are neither stopped nor finalized.
    CHECK(sco::game::services::Started() && hostStops == 0 && unloads == 0);
    CHECK(sco::QueryService("teleport.spatial", 1u << 16, &table) == sco::Result::Unavailable);
    return 0;
}
static int ServicesBarrierFalse() {
    auto o = Retained();
    o.platform.gameServices = true;
    CHECK(gh::Prepare(o) == sco::Result::Ok);
    barrierPolicy = "false"; // quiescence is no longer attested when the services would start
    const int atPrepare = barrierCalls;
    CHECK(gh::Start() == sco::Result::Failed);
    CHECK(barrierCalls == atPrepare + 1);
    CHECK(!sco::game::services::Started() && loads == 0 && hostStarts == 0);
    CHECK(Logged("game-service startup refused: native reader barrier unavailable"));
    CHECK(gh::Status().phase == gh::Phase::Failed && !gh::FreshAdmissionAllowed());
    CHECK(gh::Start() == sco::Result::BadArg); // no restart
    CHECK(gh::Stop() == sco::Result::Ok); // the failed start was already closed by closure
    return 0;
}
#endif

static int ManagedPinLive() {
    using namespace sco::detail;
    CHECK(Up(Retained()));
    static int owner, other, table;
    constexpr uint32_t version = 0x10000;
    CHECK(sco::ProvideService(&owner, nullptr, "retained.external", version, &table) == sco::Result::Ok);
    ServiceGeneration generation = 0;
    CHECK(FindManagedService("retained.external", version, generation) == sco::Result::Ok);
    ManagedServicePin pin;
    CHECK(pin.acquire(generation) == sco::Result::Ok);
    CHECK(gh::Stop() == sco::Result::Ok); // closure succeeds even with the pin live
    CHECK(gh::Status().phase == gh::Phase::Stopped && exitCalls == 1);
    CHECK(Logged("process-retained stop:") && Logged("managed service pins"));
    CHECK(Logged("admission closed, backing retained"));
    ManagedServicePin later;
    CHECK(later.acquire(generation) != sco::Result::Ok); // later acquisition is refused
    ServiceGeneration regen = 0;
    CHECK(FindManagedService("retained.external", version, regen) != sco::Result::Ok);
    CHECK(sco::ProvideService(&other, nullptr, "retained.new", version, &table) == sco::Result::Unavailable);
    pin.reset(); // the pin simply stays retained until the holder releases it
    return 0;
}

static int OwnerReads() {
    auto owner = std::make_shared<sco::OwnedShutdown>();
    auto o = Retained(owner);
    CHECK(gh::Prepare(o) == sco::Result::Ok);
    CHECK(!owner->try_read()); // refused before Start
    CHECK(gh::Start() == sco::Result::Ok);
    CHECK(!sco::detail::RuntimeBoundTo(owner)); // the runtime is not owned
    static int sub;
    CHECK(sco::Subscribe(&sub, "tick", TickEvent, nullptr) == sco::Result::Ok);
    CHECK(owner->open());
    std::atomic<bool> workerRan{false}, workerStopped{false};
    CHECK(owner->start_worker([&](auto stop) {
        workerRan = true;
        while (!stop()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        workerStopped = true;
    }));
    { auto read = owner->try_read(); CHECK(read); CHECK(gh::Tick(0) == sco::Result::Ok); }
    // A product reader on another thread, alive across Stop.
    std::atomic<bool> held{false}, release{false};
    std::thread reader([&] {
        auto read = owner->try_read();
        held = static_cast<bool>(read);
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    while (!held.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(gh::Stop() == sco::Result::Ok); // closure: no drain, no wait for the reader
    CHECK(gh::Status().phase == gh::Phase::Stopped && exitCalls == 1);
    CHECK(!owner->try_read() && !owner->ready());
    CHECK(owner->drain_state() == sco::OwnedShutdown::DrainState::Requested);
    CHECK(!owner->start_worker([](auto) {}));
    bool drained = false;
    std::atomic<bool> drainReturned{false};
    std::thread control([&] {
        drained = owner->drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(10));
        drainReturned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(!drainReturned.load()); // waits for the reader
    release = true;
    reader.join();
    control.join();
    CHECK(drained && owner->ready() && workerRan.load() && workerStopped.load());
    CHECK(owner->drain_state() == sco::OwnedShutdown::DrainState::Drained);
    CHECK(unloads == 0); // Drained is cooperative progress, never unload authority
    return 0;
}

static int RequestStopScenario(bool inCallout) {
    auto owner = std::make_shared<sco::OwnedShutdown>();
    CHECK(Up(Retained(owner)));
    gh::HostReport report;
    if (inCallout) {
        requestStopInTick = true;
        CHECK(gh::Tick(0) == sco::Result::Ok); // the subscriber requests stop inside the callout
        report = calloutReport;
    } else {
        std::thread control([&] { report = gh::RequestStopDetailed(); });
        control.join();
    }
    CHECK(report.result == sco::Result::Ok && report.sessionId != 0);
    CHECK(report.dependencies == gh::DependencyDisposition::ProcessRetained);
    CHECK(report.drain == gh::DrainDisposition::Requested);
    CHECK(!owner->try_read());
    CHECK(gh::Status().phase == gh::Phase::Running && exitCalls == 0); // an observation; no stop yet
    const auto status = gh::StatusDetailed();
    CHECK(status.result == sco::Result::Ok && status.phase == gh::Phase::Running &&
          status.dependencies == gh::DependencyDisposition::ProcessRetained);
    CHECK(gh::Tick(1000) == sco::Result::Unavailable); // the next Tick completes the stop
    CHECK(gh::Status().phase == gh::Phase::Stopped && exitCalls == 1);
    CHECK(unloads == 0 && hostStops == 0 && sco::AcquireHostSignatureRegistry());
    const auto again = gh::RequestStopDetailed();
    CHECK(again.result == sco::Result::Ok && again.phase == gh::Phase::Stopped && exitCalls == 1);
    CHECK(gh::StatusDetailed().phase == gh::Phase::Stopped);
    return 0;
}


// ---- closure: nothing joined, withdrawn or freed -------------------------------------------------

static int ClosurePrivateSession() {
    auto o = Retained();
    o.platform.privateSession.enabled = true;
    CHECK(Up(o));
    CHECK(gSession && gSelf && sco::ServiceExists(SC_PRIVATE_SESSION_NAME));
    sc_private_session_state_v1 st{sizeof(st), 0, 0, 0};
    CHECK(gSession && gSession->get_state(&st) == SCO_OK && st.state == SC_PRIVATE_SESSION_IDLE);
    static const char pass[] = "synthetic-passphrase", name[] = "tester";
    sc_private_session_host_request_v1 request{sizeof(request), 1, pass, sizeof(pass) - 1, 2, name, sizeof(name) - 1, 47311, 0};
    const sco_result hosted = gSession ? gSession->request_host(gSelf, &request) : SCO_FAILED;
    CHECK(hosted == SCO_OK); // a live loopback session whose network thread closure must not join
    const auto began = std::chrono::steady_clock::now();
    CHECK(gh::Stop() == sco::Result::Ok);
    CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(2));
    CHECK(gh::Status().phase == gh::Phase::Stopped && exitCalls == 1);
    // The table obtained before Stop is still safe to call; the broker was neither destroyed nor withdrawn.
    CHECK(gSession && gSession->get_state(&st) == SCO_UNAVAILABLE);
    CHECK(gSession && gSession->request_host(gSelf, &request) == SCO_UNAVAILABLE);
    CHECK(gSession && gSession->leave(gSelf) == SCO_UNAVAILABLE);
    CHECK(sco::ServiceExists(SC_PRIVATE_SESSION_NAME)); // still registered: not withdrawn
    CHECK(Logged("[private-session] closed: admission refused, broker retained"));
    CHECK(!Logged("[private-session] broker stopped"));
    if (hosted == SCO_OK) { // the network thread was asked to leave and ends by itself
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (sco::net::GetState() != sco::net::State::Idle && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(sco::net::GetState() == sco::net::State::Idle);
    }
    return 0;
}

// The retained runtime is left Running with a hosted session and the diagnostics worker alive, and the
// process exits with no closure at all (a game that calls ExitProcess without the quit hook).
static int ExitRunning() {
    auto admission = std::make_shared<std::atomic<sco::diagnostics::OfflineAdmission>>(sco::diagnostics::OfflineAdmission::Authorized);
    const auto dir = std::filesystem::temp_directory_path() / "sco-retained-exit";
    std::filesystem::create_directories(dir);
    auto o = Retained();
    o.platform.privateSession.enabled = true;
    o.platform.runtimeInspection.enabled = true;
    o.platform.runtimeInspection.disclosureConsent = true;
    o.platform.runtimeInspection.admission = admission;
    o.platform.runtimeInspection.credentialPath = (dir / "credential").string();
    CHECK(Up(o));
    static const char pass[] = "synthetic-passphrase", name[] = "tester";
    sc_private_session_host_request_v1 request{sizeof(request), 1, pass, sizeof(pass) - 1, 2, name, sizeof(name) - 1, 47312, 0};
    CHECK(gSession && gSession->request_host(gSelf, &request) == SCO_OK);
    CHECK(gh::Tick(0) == sco::Result::Ok && gh::Status().phase == gh::Phase::Running);
    return 0; // no Stop
}

// An owner with a started worker that nothing drains: the retained host's copy of the owner is immortal.
static std::atomic<bool> g_ownerWorkerRan{false};
static int ExitOwnerWorker() {
    auto owner = std::make_shared<sco::OwnedShutdown>();
    CHECK(Up(Retained(owner)));
    CHECK(owner->start_worker([](auto stop) {
        g_ownerWorkerRan = true;
        while (!stop()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }));
    while (!g_ownerWorkerRan.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return 0; // no Stop, no drain; the local owner reference ends here, the host's copy does not
}

static int ClosureRuntimeInspection() {
    auto admission = std::make_shared<std::atomic<sco::diagnostics::OfflineAdmission>>(sco::diagnostics::OfflineAdmission::Authorized);
    const auto dir = std::filesystem::temp_directory_path() / "sco-retained-closure";
    std::filesystem::create_directories(dir);
    auto o = Retained();
    o.platform.runtimeInspection.enabled = true;
    o.platform.runtimeInspection.disclosureConsent = true;
    o.platform.runtimeInspection.admission = admission;
    o.platform.runtimeInspection.credentialPath = (dir / "credential").string();
    o.platform.diagnosticSources.diagnosticsPlugin = true;
    o.platform.diagnosticSources.offlineAuthorized = true;
    o.platform.diagnosticSources.diagnosticsPluginId = "synthetic";
    CHECK(Up(o));
    CHECK(gPublisher);
    const sco_diagnostics_observation_v1 observation{sizeof(observation), SCO_DIAGNOSTICS_HEALTH_READY};
    CHECK(gPublisher && gPublisher->submit(gSelf, &observation) == SCO_OK);
    for (uint32_t t = 0; t < 400 && !sco::app::DiagnosticSnapshot(); ++t) // the snapshot completes over several ticks
        CHECK(gh::Tick(t * 200) == sco::Result::Ok);
    CHECK(sco::app::DiagnosticSnapshot());
    const auto began = std::chrono::steady_clock::now();
    CHECK(gh::Stop() == sco::Result::Ok);
    CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(2)); // no join of the worker
    CHECK(gh::Status().phase == gh::Phase::Stopped && exitCalls == 1);
    // The worker was signalled (its admission denied); the publisher and its service stay in place and
    // refuse through the runtime-stopping flag; the snapshot store is not reset but is no longer served.
    CHECK(admission->load() == sco::diagnostics::OfflineAdmission::Denied);
    CHECK(gPublisher && gPublisher->submit(gSelf, &observation) == SCO_UNAVAILABLE);
    CHECK(sco::ServiceExists(SCO_DIAGNOSTICS_PUBLISHER_NAME));
    CHECK(!sco::app::DiagnosticSnapshot());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return 0;
}

// ---- entries enforce their own preconditions ------------------------------------------------------

static sco::app::Platform ValidAppPlatform() {
    sco::app::Platform pf;
    pf.gameReadersQuiesced = Barrier; pf.gameReadersContext = &barrierContext;
    pf.pluginsEnabled = false; pf.privateSession.enabled = false;
    return pf;
}
static sco::SigRegistrationResult NoRegistration(sco::SignatureRegistry&) { return {}; }

static int AppEntryRefusals() {
    using R = sco::app::StartResult;
    CHECK(sco::app::detail::StartProcessRetained(ValidAppPlatform()) == R::RefusedBeforeExposure); // no registry
    auto registry = std::make_shared<sco::SignatureRegistry>();
    CHECK(registry->Freeze() == sco::SigRegistryStatus::Ok && registry->Resolve(image) == sco::SigRegistryStatus::Ok);
    CHECK(sco::InstallHostSignatureRegistry(registry, nullptr));
    auto pf = ValidAppPlatform(); pf.gameReadersQuiesced = nullptr;
    CHECK(sco::app::detail::StartProcessRetained(pf) == R::RefusedBeforeExposure);
    pf = ValidAppPlatform(); pf.shutdownOwner = std::make_shared<sco::OwnedShutdown>();
    CHECK(sco::app::detail::StartProcessRetained(pf) == R::RefusedBeforeExposure);
    pf = ValidAppPlatform(); pf.image = &image;
    CHECK(sco::app::detail::StartProcessRetained(pf) == R::RefusedBeforeExposure);
    pf = ValidAppPlatform(); pf.registerSignatures = NoRegistration;
    CHECK(sco::app::detail::StartProcessRetained(pf) == R::RefusedBeforeExposure);
    CHECK(barrierCalls == 0 && loads == 0); // every refusal came before any exposure
    return 0;
}

// Run before anything touches the runtime: it is bound to an owner here, so it is an owned runtime.
static int AppEntryOwned() {
    auto owner = std::make_shared<sco::OwnedShutdown>();
    CHECK(sco::OwnedShutdownHostAccess::begin_startup(*owner));
    auto transition = sco::OwnedShutdownHostAccess::transition(*owner);
    CHECK(transition && sco::detail::BindRuntimeStartup(owner, transition));
    auto registry = std::make_shared<sco::SignatureRegistry>();
    CHECK(registry->Freeze() == sco::SigRegistryStatus::Ok && registry->Resolve(image) == sco::SigRegistryStatus::Ok);
    CHECK(sco::InstallHostSignatureRegistry(registry, nullptr));
    CHECK(sco::app::detail::StartProcessRetained(ValidAppPlatform()) == sco::app::StartResult::RefusedBeforeExposure);
    CHECK(barrierCalls == 0 && loads == 0);
    return 0;
}

#ifdef SCO_GAME_SERVICES
static int ServicesEntryRefusals() {
    namespace gs = sco::game::services;
    gs::detail::ProcessRetainedStart start{Barrier, &barrierContext, true};
    auto refused = [](const gs::detail::ProcessRetainedStart& s) {
        return gs::detail::StartProcessRetained(s) == sco::Result::Unavailable && !gs::Started();
    };
    auto notRetained = start; notRetained.appProcessRetained = false;
    CHECK(refused(notRetained) && barrierCalls == 0); // refused before the barrier is even asked
    auto nullBarrier = start; nullBarrier.barrier = nullptr;
    CHECK(refused(nullBarrier));
    barrierPolicy = "false";
    CHECK(refused(start) && barrierCalls == 1);
    barrierPolicy = "throw";
    CHECK(refused(start) && barrierCalls == 2);
    return 0;
}
#endif

// ---- the pak hook step in the host ------------------------------------------------------------------

#if (defined(__x86_64__) || defined(_M_X64)) && !defined(__APPLE__)
#define SCO_TEST_PAK_HOST 1
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif
static uintptr_t FakeLoaderBody(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t) { return 1; }
static uint8_t* FakeLoaderStub() {
#if defined(_WIN32)
    uint8_t* p = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
#else
    void* m = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* p = m == MAP_FAILED ? nullptr : static_cast<uint8_t*>(m);
#endif
    if (!p) return nullptr;
    static const uint8_t kMovs[] = { 0x48, 0x89, 0xC0, 0x48, 0x89, 0xC0, 0x48, 0x89, 0xC0 };
    std::memcpy(p, kMovs, sizeof kMovs);
    uint8_t* j = p + sizeof kMovs;
    j[0] = 0xFF; j[1] = 0x25;
    std::memset(j + 2, 0, 4);
    uintptr_t body = reinterpret_cast<uintptr_t>(&FakeLoaderBody);
    std::memcpy(j + 6, &body, 8);
    return p;
}
static int PakHost(bool barrierOk) {
    static uintptr_t cryPak = 0;
    static sco::game::pak::Targets targets;
    targets.loader = FakeLoaderStub(); targets.cryPak = &cryPak;
    if (!targets.loader) return 77;
    gh::retained::testing::SetPakTargetsForTest(&targets);
    const auto dir = std::filesystem::temp_directory_path() / "sco-retained-pak";
    std::filesystem::create_directories(dir);
    auto o = Retained();
    o.dataCoreHook = gh::DataCoreHook::Required;
    o.platform.dataCore = true;
    o.platform.dataRoot = dir;
    o.platform.nBuiltins = 0;
    if (!barrierOk) barrierFalseFrom = 2; // call 1 attests Prepare's install step, call 2 the pak step
    const sco::Result r = gh::Prepare(o);
    if (!barrierOk) {
        CHECK(r == sco::Result::Unavailable);
        CHECK(gh::Status().phase == gh::Phase::Failed && barrierCalls == 2);
        CHECK(!sco::game::pak::Enabled() && sco::hook::DetourCount() == 0 && !sco::caps::Has("vfs.pak"));
    } else {
        CHECK(r == sco::Result::Ok && gh::Status().dataCoreActive);
        CHECK(sco::game::pak::Enabled() && sco::hook::DetourCount() == 1 && sco::caps::Has("vfs.pak"));
        CHECK(gh::Start() == sco::Result::Ok);
        CHECK(gh::Stop() == sco::Result::Ok); // closure never restores the hook
        CHECK(sco::game::pak::Enabled() && sco::hook::DetourCount() == 1);
        sco::game::pak::Disable();
        CHECK(sco::game::pak::Enabled() && sco::hook::DetourCount() == 1);
    }
    gh::retained::testing::SetPakTargetsForTest(nullptr);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return 0;
}
#endif

// ---- quit when closure cannot run -------------------------------------------------------------------

static void OpenQuit(void* system, void* info, int code) {
    ++quitOriginals; quitSystem = system; quitInfo = info; quitCode = code;
    CHECK(gh::Status().phase == gh::Phase::Running); // the original runs before any closure completed
}
static int QuitRefused(bool inTick) {
    auto owner = std::make_shared<sco::OwnedShutdown>();
    CHECK(Up(Retained(owner)));
    gh::retained::testing::SetQuitOriginalForTest(OpenQuit);
    if (inTick) {
        quitInTick = true;
        CHECK(gh::Tick(0) == sco::Result::Ok); // the quit detour runs inside the tick callout, gate held
    } else {
        std::thread other([] { gh::retained::testing::InvokeQuitDetourForTest(nullptr, nullptr, 9); });
        other.join();
    }
    CHECK(quitOriginals == 1 && exitCalls == 0);
    CHECK(Logged("admission closed, game.exit not guaranteed"));
    CHECK(!owner->try_read() && !gh::FreshAdmissionAllowed()); // admission closed without waiting
    CHECK(gh::Status().phase == gh::Phase::Running);
    CHECK(gh::Tick(1000) == sco::Result::Unavailable); // the next Tick runs the requested closure
    CHECK(gh::Status().phase == gh::Phase::Stopped && exitCalls == 1);
    return 0;
}

// ---- detailed capsule, retry, idle report -----------------------------------------------------------

static int DetailedRetainedRefused() {
    auto detailed = Strict();
    detailed.shutdownOwner = std::make_shared<sco::OwnedShutdown>();
    detailed.platform.privateSession.enabled = false;
    detailed.dataCoreHook = gh::DataCoreHook::External;
    detailed.composition = gh::Composition::ProcessRetained;
    auto capsule = std::make_shared<const gh::HostCapsule>(detailed, backing);
    const auto report = gh::PrepareDetailed(capsule);
    CHECK(report.result == sco::Result::BadArg && report.phase == gh::Phase::Idle);
    CHECK(report.dependencies == gh::DependencyDisposition::NeverExposed); // an invalid capsule, not the claim
    detailed.composition = gh::Composition::Strict; // the same capsule is otherwise valid
    auto strictCapsule = std::make_shared<const gh::HostCapsule>(detailed, backing);
    CHECK(gh::PrepareDetailed(strictCapsule).result != sco::Result::BadArg);
    return 0;
}

static int RetryAfterRefusal() {
    auto owner = std::make_shared<sco::OwnedShutdown>();
    auto o = Retained(owner);
    evidence.session = gh::Session::Unknown;
    for (int attempt = 0; attempt < 3; ++attempt) {
        CHECK(gh::Prepare(o) == sco::Result::Unavailable && Untouched());
        const auto status = gh::StatusDetailed();
        CHECK(status.phase == gh::Phase::Idle && status.sessionId == 0 &&
              status.dependencies == gh::DependencyDisposition::ProcessRetained);
        { auto read = owner->try_read(); CHECK(read); } // the refusals left the owner untouched
    }
    CHECK(gh::Prepare(Strict()) == sco::Result::BadArg); // the refused attempts still claimed the process
    evidence = {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
    CHECK(gh::Prepare(o) == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Prepared && gh::StatusDetailed().sessionId != 0);
    CHECK(gh::Start() == sco::Result::Ok);
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    if (mode == "app_entry_owned") { // before anything touches the runtime
        const int owned = AppEntryOwned();
        std::printf("game_host_retained %s: %d checks, %d failures\n", mode.c_str(), checks, failures);
        return owned || failures ? 1 : 0;
    }
    sco::SetLogSink(Sink);
    int rc = 0;
    if (mode == "admission") rc = Admission();
    else if (mode == "barrier_null") rc = Barrier_("null");
    else if (mode == "barrier_false") rc = Barrier_("false");
    else if (mode == "barrier_throw") rc = Barrier_("throw");
    else if (mode == "lifecycle") rc = Lifecycle();
    else if (mode == "quit") rc = Quit();
    else if (mode == "quit_required") rc = QuitRequired();
    else if (mode == "closure_private_session") rc = ClosurePrivateSession();
    else if (mode == "closure_runtime_inspection") rc = ClosureRuntimeInspection();
    else if (mode == "exit_running") rc = ExitRunning();
    else if (mode == "exit_owner_worker") rc = ExitOwnerWorker();
    else if (mode == "app_entry_refusals") rc = AppEntryRefusals();
    else if (mode == "quit_in_tick") rc = QuitRefused(true);
    else if (mode == "quit_off_thread") rc = QuitRefused(false);
    else if (mode == "detailed_retained_refused") rc = DetailedRetainedRefused();
    else if (mode == "retry_after_refusal") rc = RetryAfterRefusal();
#ifdef SCO_TEST_PAK_HOST
    else if (mode == "pak_barrier_false") { rc = PakHost(false); if (rc == 77) return 77; }
    else if (mode == "pak_barrier_true") { rc = PakHost(true); if (rc == 77) return 77; }
#else
    else if (mode == "pak_barrier_false" || mode == "pak_barrier_true") return 77;
#endif
#ifdef SCO_GAME_SERVICES
    else if (mode == "services_entry_refusals") rc = ServicesEntryRefusals();
    else if (mode == "services_start") rc = ServicesStart();
    else if (mode == "services_barrier_false") rc = ServicesBarrierFalse();
#else
    else if (mode == "services_start" || mode == "services_barrier_false" || mode == "services_entry_refusals") return 77;
#endif
    else if (mode == "admission_loss") rc = AdmissionLoss();
    else if (mode == "exclusive") rc = Exclusive();
    else if (mode == "exclusive_strict_first") rc = ExclusiveStrictFirst();
    else if (mode == "managed_pin_live") rc = ManagedPinLive();
    else if (mode == "owner_reads") rc = OwnerReads();
    else if (mode == "request_stop") rc = RequestStopScenario(false);
    else if (mode == "request_stop_callout") rc = RequestStopScenario(true);
    else return 2;
    std::printf("game_host_retained %s: %d checks, %d failures\n", mode.c_str(), checks, failures);
    // Exit normally, so static destructors run with the retained runtime's threads still alive: the
    // diagnostics worker, a hosted session's network thread, an undrained owner worker. None of them
    // may terminate, join or block at that point. The log sink's storage is a test static, so detach it.
    sco::SetLogSink(nullptr);
    return rc || failures ? 1 : 0;
}
