// Synthetic offline host composition; no game image, data, process or network.
#include "sco/game/host.h"
#include "sco/runtime.h"
#include <cstdio>
#include <thread>

namespace gh = sco::game::host;
static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static gh::Admission evidence;
static int inspections = 0;
static gh::Admission Inspect() { ++inspections; return evidence; }
int main() {
    gh::Options o;
    o.inspect = Inspect;
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
    sco::Result wrong = sco::Result::Ok;
    std::thread t([&] { wrong = gh::Stop(); }); t.join();
    CHECK(wrong == sco::Result::WrongThread);
    CHECK(gh::Status().phase == gh::Phase::Running);
    CHECK(gh::Stop() == sco::Result::Ok);
    CHECK(gh::Stop() == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Stopped);
    CHECK(inspections >= 8);
    std::printf("game_host: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
