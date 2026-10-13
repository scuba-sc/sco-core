// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// Synthetic game-pack provider + real runtime release. NOT native/Windows proof.
// Fixture calls must execute in Release/RelWithDebInfo too; assertions carry
// both the tested mutations and the cleanup checks.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/game/services/mining.h"
#include "../src/game/admission.h"
#include "sco/host.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace sco::game::services;
namespace gh = sco::game::host;
std::atomic<bool> failRead{false};
std::atomic<int> reads{0}, inspections{0}, installs{0};
MiningCell cell{0, 0, 2};
gh::Admission evidence;
gh::Admission Inspect() { ++inspections; return evidence; }
bool Install() { ++installs; return true; }
bool Read(uintptr_t, uintptr_t, MiningCell* out) { ++reads; *out = cell; return !failRead.load(); }
static_assert(sizeof(sc_mining_v1) == 24);
static_assert(sizeof(sc_mining_state) == 40);
static_assert(offsetof(sc_mining_state, cells) == 16);
int main() {
    sco::SetGameThread();
    const auto* api = sco::host::BuildApi({"mining-test"});
    auto* a = sco::host::NewPlugin("mining_a");
    auto* b = sco::host::NewPlugin("mining_b");
    assert(!gh::FreshAdmissionAllowed());
    gh::detail::SetFreshInspector(Inspect);
    gh::detail::SetFreshAdmissionEnabled(true);
    assert(!gh::FreshAdmissionAllowed()); // unknown evidence cannot admit
    evidence = {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
    assert(gh::FreshAdmissionAllowed());
    evidence.session = gh::Session::Online; assert(!gh::FreshAdmissionAllowed());
    evidence.session = gh::Session::Offline;
    for (auto* condition : {&evidence.antiCheat, &evidence.officialNetwork, &evidence.targetProcess}) {
        *condition = gh::Check::Unknown; assert(!gh::FreshAdmissionAllowed());
        *condition = gh::Check::Present; assert(!gh::FreshAdmissionAllowed());
        *condition = gh::Check::Clear;
    }
    // Synthetic shared-dispatcher stand-in, real neutral owner release path.
    assert(sco::AddReleaseHook(ReleaseMiningOwner) == sco::Result::Ok);
    MiningBackend bridge{true, true, gh::FreshAdmissionAllowed, Install, Read};
    assert(StartMining(bridge) == sco::Result::Ok);
    const void* raw = nullptr;
    assert(api->query_service(SC_MINING_SERVICE_NAME, SC_MINING_SERVICE_VERSION, &raw) == SCO_OK);
    auto* svc = static_cast<const sc_mining_v1*>(raw);
    sc_mining_state state{}; state.size = sizeof(state);
    assert(sc_mining_read_state(svc, a, &state) == SCO_OK && !state.requested && !state.active && state.block == 0);
    assert(sc_mining_request(nullptr, a, 1) == SCO_UNAVAILABLE);
    assert(svc->request(nullptr, 1) == SCO_BAD_ARG);
    assert(svc->request(a, 2) == SCO_BAD_ARG);
    // Physical partial callback extents: adapter must not read the incomplete pointer.
    for (size_t n = 4; n < sizeof(sc_mining_v1); ++n) {
        auto* shortTable = static_cast<sc_mining_v1*>(std::malloc(n));
        *reinterpret_cast<uint32_t*>(shortTable) = static_cast<uint32_t>(n);
        if (n < offsetof(sc_mining_v1, request) + sizeof(svc->request))
            assert(sc_mining_request(shortTable, a, 1) == SCO_UNAVAILABLE);
        assert(sc_mining_read_state(shortTable, a, &state) == SCO_UNAVAILABLE);
        std::free(shortTable);
    }
    sc_mining_v1 nullCallbacks{sizeof(nullCallbacks), 0, nullptr, nullptr};
    assert(sc_mining_request(&nullCallbacks, a, 1) == SCO_UNAVAILABLE);
    assert(sc_mining_read_state(&nullCallbacks, a, &state) == SCO_UNAVAILABLE);
    evidence.officialNetwork = gh::Check::Unknown;
    assert(svc->request(a, 1) == SCO_OK && installs == 0);
    assert(svc->state(a, &state) == SCO_OK && state.requested && !state.active && state.block == SC_MINING_BLOCK_ADMISSION);
    evidence.officialNetwork = gh::Check::Clear;
    assert(svc->request(a, 1) == SCO_OK && installs == 1);
    assert(svc->request(b, 1) == SCO_OK && installs == 1);
    assert(svc->state(a, &state) == SCO_OK && state.active);
    assert(svc->request(a, 0) == SCO_OK);
    assert(svc->state(a, &state) == SCO_OK && !state.requested && !state.active);
    assert(MiningCellFlags(1, 2, 0, 6) == 14); // b remains owner
    assert(MiningCellFlags(1, 2, 1, 6) == 14);
    assert(MiningCellFlags(1, 2, 2, 6) == 6);
    assert(MiningCellFlags(1, 2, 0, 7) == 7);
    assert(MiningCellFlags(0, 2, 0, 6) == 6);
    cell.builderType = 1; assert(MiningCellFlags(1, 2, 0, 6) == 6); cell.builderType = 0;
    cell.spawnGate = 1; assert(MiningCellFlags(1, 2, 0, 6) == 6); cell.spawnGate = 0;
    cell.harvestLod = 1; assert(MiningCellFlags(1, 2, 0, 6) == 6);
    cell.harvestLod = 100; assert(MiningCellFlags(1, 2, 0, 6) == 14);
    cell.harvestLod = 101; assert(MiningCellFlags(1, 2, 0, 6) == 6); cell.harvestLod = 2;
    int before = reads;
    evidence.antiCheat = gh::Check::Present;
    assert(MiningCellFlags(1, 2, 0, 6) == 6 && reads == before);
    evidence.antiCheat = gh::Check::Clear;
    std::thread worker([&] {
        assert(svc->request(b, 1) == SCO_WRONG_THREAD);
        assert(MiningCellFlags(1, 2, 0, 6) == 14); // host-only thread-safe callbacks
    }); worker.join();
    assert(sco::Release(b) == sco::Result::Ok);
    before = reads;
    assert(MiningCellFlags(1, 2, 0, 6) == 6 && reads == before); // last owner removed
    assert(svc->request(b, 1) == SCO_BAD_ARG);
    assert(svc->request(a, 1) == SCO_OK);
    failRead = true;
    assert(MiningCellFlags(1, 2, 0, 6) == 6);
    assert(svc->state(a, &state) == SCO_OK && state.block == SC_MINING_BLOCK_RESTART && state.faults == 1);
    failRead = false; before = reads;
    assert(MiningCellFlags(1, 2, 0, 6) == 6 && reads == before); // process latch
    auto small = sc_mining_state{}; small.size = 4;
    assert(svc->state(a, &small) == SCO_BAD_ARG);
    gh::detail::SetFreshAdmissionEnabled(false); assert(!gh::FreshAdmissionAllowed());
    StopMining();
    assert(MiningCellFlags(1, 2, 0, 6) == 6 && reads == before);
    assert(api->query_service(SC_MINING_SERVICE_NAME, SC_MINING_SERVICE_VERSION, &raw) == SCO_NOT_FOUND);
    assert(StartMining(bridge) == sco::Result::Unavailable);
    assert(sco::Release(a) == sco::Result::Ok);
    std::puts("mining: ABI extents, fresh admission, exact promotion guards, worker route, owner release, fault latch and stopped pass-through PASS (synthetic)");
}
