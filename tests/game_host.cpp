// Synthetic offline host composition; no game image, data, process or network.
#include "sco/game/host.h"
#include "sco/runtime.h"
#include <cstdio>
#include <thread>
#include <stdexcept>
#include <cstring>

namespace gh = sco::game::host;
static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static gh::Admission evidence;
static int inspections = 0;
static gh::Admission Inspect() { ++inspections; return evidence; }
static int ticks = 0, tasks = 0;
static void TickEvent(const char*, const void*, void*) { ++ticks; }
static void Task(void*) { ++tasks; }
static int crashedTicks = 0;
static void CrashTick(const char*, const void*, void*) { ++crashedTicks; throw std::runtime_error("synthetic tick fault"); }
static const sco_plugin_info* Query() {
    static const sco_plugin_info info{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "synthetic", "1.0.0", "tests"};
    return &info;
}
static sco_result Load(const sco_api* api, sco_plugin* self) { return api->subscribe(self, "tick", CrashTick, nullptr); }
static void Unload() {}
static const sco::plugins::Builtin builtin{"synthetic", Query, Load, Unload};
static uint32_t ExceptionGuard(void (*thunk)(void*), void* ctx) {
    try { thunk(ctx); return 0; } catch (const std::runtime_error&) { return 0xe06d7363u; }
}
int main(int argc, char** argv) {
    sco::plugins::SetCallGuard(ExceptionGuard);
    const bool loss = argc > 1 && std::strcmp(argv[1], "loss") == 0;
    gh::Options o;
    o.inspect = Inspect;
    o.platform.builtins = &builtin;
    o.platform.nBuiltins = 1;
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
    uint8_t bytes[8192]{};
    sco::Image image{bytes, {bytes, 4096}, {bytes + 4096, 4096}, {}, 0, sizeof(bytes)};
    o.image = &image;
    CHECK(gh::Prepare(o) == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Prepared);
    CHECK(gh::Prepare(o) == sco::Result::BadArg);
    evidence.session = gh::Session::Online; // revalidate before publication
    CHECK(gh::Start() == sco::Result::Unavailable);
    CHECK(gh::Status().phase == gh::Phase::Prepared);
    evidence.session = gh::Session::Offline;
    CHECK(gh::Start() == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Running);
    CHECK(gh::Start() == sco::Result::BadArg);
    CHECK(sco::Subscribe(&ticks, "tick", TickEvent, nullptr) == sco::Result::Ok);
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
    if (loss) {
        evidence.session = gh::Session::Unknown;
        CHECK(gh::Tick(0x55u) == sco::Result::Unavailable); // checked even when throttled
    } else {
        CHECK(gh::Stop() == sco::Result::Ok);
    }
    CHECK(gh::Stop() == sco::Result::Ok);
    CHECK(gh::Tick(500) == sco::Result::Unavailable);
    CHECK(tasks == 2 && ticks == 2 && sco::QueuedTasks() == 0);
    CHECK(gh::Status().phase == gh::Phase::Stopped);
    CHECK(inspections >= 8);
    std::printf("game_host: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
