// Synthetic public-service/provider tests; no game addresses or engine execution.
#include "sco/host.h"
#include "../src/game/services/quantum.h"
#include <sc_quantum.h>
#include <thread>
#include <cstdlib>
#include "../src/api/internal.h"
// Fixture operations and checks must remain active in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#if defined(_WIN32) && defined(_M_X64)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstring>
#endif
using namespace sco::game::services;
struct Fixture {
    QuantumObservation observation{true, true, true, SC_QUANTUM_CHARGING, 0, 125000};
    int changes = 0, observations = 0;
    bool enabled = false, held = false, fail = false;
};
bool Observe(void* ctx, QuantumObservation* out) {
    auto& f = *static_cast<Fixture*>(ctx); ++f.observations; *out = f.observation; return true;
}
bool Apply(void* ctx, bool enabled, bool held) {
    auto& f = *static_cast<Fixture*>(ctx); ++f.changes; f.enabled = enabled; f.held = held; return !f.fail;
}
static Fixture defaultFixture;
static int defaultCalls = 0;
static sco::Result SelectDefault() {
    ++defaultCalls;
    return ConfigureImplicitQuantum({&defaultFixture, Observe, Apply, nullptr, 0});
}
static sco::Result DefaultUnavailable() { ++defaultCalls; return sco::Result::Unavailable; }
#if defined(_WIN32) && defined(_M_X64)
static QuantumBackend staleDefault;
static sco::Result SelectStaleDefault() { return ConfigureImplicitQuantum(staleDefault); }
#endif
template<int N> void NoopRelease(const void*) {
    // Keep distinct hook identities under MSVC identical-COMDAT folding.
    static volatile int identity = N;
    (void)identity;
}
int main(int argc, char** argv) {
    sco::SetGameThread();
    const auto* api = sco::host::BuildApi({"quantum-test"});
    sco::ReleaseHook hooks[] = {NoopRelease<0>, NoopRelease<1>, NoopRelease<2>, NoopRelease<3>, NoopRelease<4>, NoopRelease<5>, NoopRelease<6>, NoopRelease<7>};
    for (auto fn : hooks) assert(sco::AddReleaseHook(fn) == sco::Result::Ok);
    assert(StartQuantum() == sco::Result::Ok);
    for (auto fn : hooks) assert(sco::RemoveReleaseHook(fn) == sco::Result::Ok);
    const void* table = nullptr;
    assert(api->query_service("game.quantum", 0x00010000u, &table) == SCO_OK);
    assert(table);
    const auto* q = static_cast<const sc_quantum_v1*>(table);
    auto* self = sco::host::NewPlugin("quantum_test");
    assert(q->enable(nullptr, 1) == SCO_BAD_ARG);
    assert(q->enable(self, 2) == SCO_BAD_ARG);
    assert(q->hold(self, 0) == SCO_BAD_ARG);
    assert(q->hold(self, SC_QUANTUM_MAX_HOLD_MS + 1) == SCO_BAD_ARG);
    sc_quantum_state state{sizeof(state), 0, 0, 0, 0, 0, 0};
    assert(q->state(self, &state) == SCO_UNAVAILABLE);
    assert(state.block == SC_QUANTUM_BLOCK_UNAVAILABLE);
    assert(q->enable(self, 1) == SCO_UNAVAILABLE);
    assert(!api->has("game.quantum.control"));
    std::thread t([&] { assert(q->enable(self, 1) == SCO_WRONG_THREAD); assert(StopQuantum() == sco::Result::WrongThread); });
    t.join();
    assert(q->enable(self, 0) == SCO_OK);
    auto* shortOut = static_cast<sc_quantum_state*>(std::malloc(4));
    *reinterpret_cast<uint32_t*>(shortOut) = 4;
    assert(q->state(self, shortOut) == SCO_BAD_ARG);
    std::free(shortOut);
    assert(sco::Release(self) == sco::Result::Ok);
    assert(q->enable(self, 1) == SCO_BAD_ARG);
    StopQuantum();
    std::puts("quantum: unavailable and invalid admission fail closed");
    assert(sco::AddReleaseHook(ReleaseQuantumOwner) == sco::Result::Ok); // fixture host composition
    // A failed start still ends the selected provider's lifetime when stopped.
    auto* failedContext = new Fixture;
    failedContext->observation.offline = false;
    char failedTarget[32]{}; void* failedOriginal = nullptr;
    QuantumHook failedHook{failedTarget, 14, failedTarget + 16, &failedOriginal};
    assert(ConfigureProductionQuantum({failedContext, Observe, Apply, &failedHook, 1}) == sco::Result::Ok);
    assert(StartProductionQuantumWithDefault(true, SelectDefault) == sco::Result::Unavailable);
    assert(!sco::hook::IsHooked(failedTarget));
    StopQuantum();
    delete failedContext;
    assert(StartProductionQuantumWithDefault(true, DefaultUnavailable) == sco::Result::Ok);
    assert(!api->has("game.quantum.control"));
    StopQuantum();
    failedContext = new Fixture;
    assert(ConfigureProductionQuantum({failedContext, Observe, Apply, nullptr, 0}) == sco::Result::Ok);
    assert(sco::host::ProvideGameService(SC_QUANTUM_SERVICE_NAME, SC_QUANTUM_SERVICE_VERSION, q) == sco::Result::Ok);
    assert(StartProductionQuantumWithDefault(true, SelectDefault) != sco::Result::Ok);
    sco::host::WithdrawGameService(SC_QUANTUM_SERVICE_NAME);
    StopQuantum();
    delete failedContext;
    assert(StartProductionQuantumWithDefault(true, DefaultUnavailable) == sco::Result::Ok);
    assert(!api->has("game.quantum.control"));
    StopQuantum();
    defaultCalls = 0;
    // Exercise the same default-selection/start path as normal game-service startup.
    Fixture selected;
    QuantumBackend chosen{&selected, Observe, Apply, nullptr, 0};
    assert(ConfigureProductionQuantum(chosen) == sco::Result::Ok);
    auto invalid = chosen; invalid.apply = nullptr;
    assert(ConfigureProductionQuantum(invalid) == sco::Result::BadArg);
    assert(ConfigureImplicitQuantum(invalid) == sco::Result::BadArg);
    char defaultTarget[32]{}; void* defaultOriginal = nullptr;
    QuantumHook defaultHook{defaultTarget, 14, defaultTarget + 16, &defaultOriginal};
    QuantumBackend competing{&defaultFixture, Observe, Apply, &defaultHook, 1};
    assert(ConfigureImplicitQuantum(competing) == sco::Result::Ok);
    assert(StartProductionQuantumWithDefault(true, SelectDefault) == sco::Result::Ok);
    assert(defaultCalls == 0 && api->has("game.quantum.control") && !sco::hook::IsHooked(defaultTarget));
    auto* selectedOwner = sco::host::NewPlugin("quantum_selected");
    assert(q->enable(selectedOwner, 1) == SCO_OK && selected.changes == 1);
    assert(defaultFixture.changes == 0 && defaultFixture.observations == 0);
    assert(ConfigureImplicitQuantum(chosen) == sco::Result::Failed);
    StopQuantum();
    assert(sco::Release(selectedOwner) == sco::Result::Ok);
    // The host may release its provider context as soon as successful stop returns.
    auto* releasedContext = new Fixture;
    assert(ConfigureProductionQuantum({releasedContext, Observe, Apply, nullptr, 0}) == sco::Result::Ok);
    assert(StartProductionQuantumWithDefault(true, SelectDefault) == sco::Result::Ok);
    StopQuantum();
    delete releasedContext;
    assert(StartProductionQuantumWithDefault(true, DefaultUnavailable) == sco::Result::Ok);
    assert(!api->has("game.quantum.control"));
    StopQuantum();
    assert(ConfigureProductionQuantum(chosen) == sco::Result::Ok);
    assert(StartProductionQuantumWithDefault(true, SelectDefault) == sco::Result::Ok);
    assert(api->has("game.quantum.control"));
    StopQuantum();
    defaultCalls = 0;
#if defined(_WIN32) && defined(_M_X64)
    // Executable synthetic code proves which provider owns the installed hook.
    auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    assert(page);
    const unsigned char code[] = {0xB8, 1, 0, 0, 0, 0xC3};
    std::memcpy(page + 64, code, sizeof(code)); std::memcpy(page + 128, code, sizeof(code));
    std::memcpy(page + 192, code, sizeof(code));
    void* selectedOriginal = nullptr; void* defaultHookOriginal = nullptr;
    QuantumHook selectedHook{page + 64, 5, page + 128, &selectedOriginal};
    QuantumHook unusedHook{page + 192, 5, page + 128, &defaultHookOriginal};
    chosen.hooks = &selectedHook; chosen.hookCount = 1;
    assert(ConfigureProductionQuantum(chosen) == sco::Result::Ok);
    QuantumBackend unused{&defaultFixture, Observe, Apply, &unusedHook, 1};
    assert(ConfigureImplicitQuantum(unused) == sco::Result::Ok);
    assert(StartProductionQuantumWithDefault(true, SelectDefault) == sco::Result::Ok);
    assert(defaultCalls == 0 && sco::hook::IsHooked(page + 64) && !sco::hook::IsHooked(page + 192));
    StopQuantum();
    assert(!sco::hook::IsHooked(page + 64));
    // Successful implicit start followed by failed revalidation cannot reuse its hook.
    staleDefault = {&defaultFixture, Observe, Apply, &unusedHook, 1};
    assert(StartProductionQuantumWithDefault(true, SelectStaleDefault) == sco::Result::Ok);
    assert(sco::hook::IsHooked(page + 192));
    StopQuantum();
    assert(!sco::hook::IsHooked(page + 192));
    assert(StartProductionQuantumWithDefault(true, DefaultUnavailable) == sco::Result::Ok);
    assert(!api->has("game.quantum.control") && !sco::hook::IsHooked(page + 192));
    StopQuantum();
    defaultCalls = 0;
    assert(VirtualFree(page, 0, MEM_RELEASE));
#endif
    char rejectedTarget[32]{}; void* rejectedOriginal = nullptr;
    QuantumHook rejectedHook{rejectedTarget, 14, rejectedTarget + 16, &rejectedOriginal};
    selected.observation.offline = false;
    chosen.hooks = &rejectedHook; chosen.hookCount = 1;
    assert(ConfigureProductionQuantum(chosen) == sco::Result::Ok);
    assert(StartProductionQuantumWithDefault(false, SelectDefault) == sco::Result::Unavailable);
    assert(StartProductionQuantumWithDefault(true, SelectDefault) == sco::Result::Unavailable);
    assert(defaultCalls == 0 && !sco::hook::IsHooked(rejectedTarget));
    assert(!api->has("game.quantum.control"));
    assert(ConfigureProductionQuantum({}) == sco::Result::Ok);
    assert(StartProductionQuantumWithDefault(true, DefaultUnavailable) == sco::Result::Ok);
    assert(defaultCalls == 1 && !api->has("game.quantum.control"));
    StopQuantum();
    assert(StartProductionQuantumWithDefault(true, SelectDefault) == sco::Result::Ok);
    assert(defaultCalls == 2 && api->has("game.quantum.control"));
    StopQuantum();
    assert(ConfigureProductionQuantum({}) == sco::Result::Ok);
    Fixture f;
    QuantumBackend backend{&f, Observe, Apply, nullptr, 0};
    auto malformed = backend; malformed.apply = nullptr;
    assert(ConfigureProductionQuantum(malformed) == sco::Result::BadArg);
    assert(StartQuantum(malformed, true) == sco::Result::BadArg);
    assert(ConfigureProductionQuantum(backend) == sco::Result::Ok);
    auto* a = sco::host::NewPlugin("quantum_a");
    auto* b = sco::host::NewPlugin("quantum_b");
    assert(StartProductionQuantum(false) == sco::Result::Ok);
    assert(!api->has("game.quantum.control"));
    assert(q->enable(a, 1) == SCO_UNAVAILABLE && f.changes == 0 && f.observations == 0);
    StopQuantum();
    assert(ConfigureProductionQuantum(backend) == sco::Result::Ok);
    assert(sco::host::ProvideGameService(SC_QUANTUM_SERVICE_NAME, SC_QUANTUM_SERVICE_VERSION, q) == sco::Result::Ok);
    assert(StartProductionQuantum(true) != sco::Result::Ok);
    assert(!api->has("game.quantum.control"));
    sco::host::WithdrawGameService(SC_QUANTUM_SERVICE_NAME);
    assert(StartProductionQuantum(true) == sco::Result::Ok);
    assert(api->has("game.quantum.control"));
    assert(ConfigureProductionQuantum({}) == sco::Result::Failed);
    assert(q->enable(a, 1) == SCO_OK);
    assert(q->enable(a, 1) == SCO_OK && f.changes == 1);
    assert(q->enable(b, 1) == SCO_FAILED); // never steals a's lease
    // No uptime origin exists until the first tick after start.
    assert(q->hold(a, 50) == SCO_OK && f.held);
    TickQuantum(1000000); assert(f.held);
    TickQuantum(1000049); assert(f.held);
    TickQuantum(1000050); assert(!f.held);
    TickQuantum(UINT32_MAX - 10);
    assert(q->hold(a, 20) == SCO_OK && f.held);
    TickQuantum(8); assert(f.held);
    TickQuantum(9); assert(!f.held);
    TickQuantum(100);
    assert(q->hold(a, 50) == SCO_OK && f.held);
    state.size = sizeof(state);
    assert(q->state(a, &state) == SCO_OK && state.progress == 125000 && state.held);
    TickQuantum(149); assert(f.held);
    assert(sco::GameThreadTick(150) == sco::Result::Ok); assert(!f.held && f.enabled);
    assert(q->hold(a, 50) == SCO_OK);
    assert(q->release(b) == SCO_NOT_FOUND && f.held);
    assert(q->release(a) == SCO_OK && !f.held);
    assert(q->release(a) == SCO_OK);
    assert(q->hold(a, 10) == SCO_OK);
    assert(sco::Release(a) == sco::Result::Ok && !f.enabled && !f.held);
    assert(q->hold(a, 10) == SCO_BAD_ARG);
    assert(q->enable(b, 1) == SCO_OK);
    sco::detail::SetRuntimeStopping(true);
    assert(q->enable(b, 1) == SCO_UNAVAILABLE);
    TickQuantum(180); assert(!f.enabled && !f.held);
    sco::detail::SetRuntimeStopping(false);
    const auto previousSession = state.session;
    assert(StopQuantum() == sco::Result::Ok); assert(StopQuantum() == sco::Result::Ok);
    assert(!f.enabled && !f.held && !api->has("game.quantum.control"));
    assert(q->enable(b, 1) == SCO_UNAVAILABLE);
    assert(StartQuantum(backend, true) == sco::Result::Ok);
    assert(q->state(b, &state) == SCO_OK && state.session != previousSession && !state.enabled);
    f.observation.drive = false;
    assert(q->enable(b, 1) == SCO_UNAVAILABLE);
    assert(q->state(b, &state) == SCO_UNAVAILABLE && state.block == SC_QUANTUM_BLOCK_DRIVE);
    f.observation.drive = true;
    f.observation.offline = false;
    assert(q->enable(b, 1) == SCO_UNAVAILABLE);
    assert(q->state(b, &state) == SCO_UNAVAILABLE && state.block == SC_QUANTUM_BLOCK_OFFLINE);
    f.observation.offline = true;
    assert(q->enable(b, 1) == SCO_OK);
    assert(q->hold(b, 100) == SCO_OK);
    f.observation.antiCheatAbsent = false;
    TickQuantum(200);
    assert(!f.enabled && !f.held);
    StopQuantum();
    std::puts("quantum: activated configured bridge, cleanup gate, publication collision, first-tick/wrap expiry and admission refusal");
    char target[32]{}; void* original = nullptr;
    QuantumHook duplicate[]{{target, 14, target + 16, &original}, {target, 14, target + 16, &original}};
    auto conflict = backend; conflict.hooks = duplicate; conflict.hookCount = 2;
    assert(StartQuantum(conflict, true) == sco::Result::Failed);
    table = nullptr;
    assert(api->query_service(SC_QUANTUM_SERVICE_NAME, SC_QUANTUM_SERVICE_VERSION, &table) == SCO_NOT_FOUND);
    assert(!sco::hook::IsHooked(target));
    assert(StartQuantum(backend, true) == sco::Result::Ok);
    f.observation.antiCheatAbsent = true;
    assert(q->enable(b, 1) == SCO_OK);
    if (argc > 1 && std::strcmp(argv[1], "--stop-cleanup-fault") == 0) {
        f.fail = true;
        const int before = f.changes;
        assert(StopQuantum() == sco::Result::Failed);
        assert(f.changes == before + 1 && !api->has("game.quantum.control"));
        assert(StopQuantum() == sco::Result::Failed && f.changes == before + 1);
        std::puts("quantum: failed stop retains bridge and does not retry cleanup");
        return 0;
    }
    if (argc > 1) {
        f.fail = true;
        assert(q->enable(b, 0) == SCO_FAILED);
    } else {
        f.observation.progress = SC_QUANTUM_PROGRESS_MAX + 1;
        const int before = f.changes;
        assert(q->enable(b, 1) == SCO_FAILED); // no cleanup callback after invalid-output latch
        assert(f.changes == before);
    }
    const int faultChanges = f.changes, faultObservations = f.observations;
    assert(!api->has("game.quantum.control"));
    assert(q->state(b, &state) == SCO_FAILED && state.block == SC_QUANTUM_BLOCK_RESTART && state.enabled);
    assert(q->enable(b, 1) == SCO_FAILED);
    TickQuantum(250);
    assert(sco::Release(b) == sco::Result::Ok);
    assert(f.changes == faultChanges && f.observations == faultObservations);
    assert(StopQuantum() == sco::Result::Failed);
    assert(StopQuantum() == sco::Result::Failed);
    assert(f.changes == faultChanges && f.observations == faultObservations);
    state.block = 99;
    auto* c = sco::host::NewPlugin("quantum_c");
    assert(q->state(c, &state) == SCO_UNAVAILABLE && state.block == 99);
    assert(StartQuantum(backend, true) == sco::Result::Failed);
    assert(ConfigureProductionQuantum({}) == sco::Result::Failed);
    std::puts("quantum: duplicate hook refusal and restart latch suppresses every later adapter callback");
}
