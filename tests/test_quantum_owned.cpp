// Synthetic tests for the default production quantum backend's bookkeeping; a fake engine, no game addresses.
#include "sco/host.h"
#include "../src/game/services/quantum_owned.h"
#include "../src/api/internal.h"
#include <sc_quantum.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace sco::game::services;
namespace {
constexpr uintptr_t kDrive = 0x1000, kId = 0xABC0, kParamCell = 0x5000, kChargeCell = 0x6000;
constexpr uintptr_t kActor = 0x77, kEntity = 0x88, kOther = 0x99;
struct Fake {
    bool admitted = true, driveOk = true, failChargeWrite = false;
    int loseAfter = -1, admitCalls = 0, failCVarWrite = -1;
    int32_t cvar[kOwnedCVars] = {0, 0, 1, 0};
    QuantumCVarType type[kOwnedCVars] = {QuantumCVarType::Int32, QuantumCVarType::Int32,
                                         QuantumCVarType::Int32, QuantumCVarType::Int32};
    float param = 5.0f, charge = 0.4f;
    int32_t state = 3;
    uint32_t now = 0;
    bool pressed = false;
    std::vector<std::string> log;
    int Count(const std::string& what) const {
        int n = 0;
        for (const auto& e : log) if (e == what) ++n;
        return n;
    }
    int Writes() const {
        int n = 0;
        for (const auto& e : log) if (e.rfind("write", 0) == 0 || e == "press" || e == "release") ++n;
        return n;
    }
    size_t First(const std::string& what) const {
        for (size_t k = 0; k < log.size(); ++k) if (log[k] == what) return k;
        return log.size();
    }
};
Fake& F(void* c) { return *static_cast<Fake*>(c); }
bool Admitted(void* c) {
    auto& f = F(c);
    f.log.push_back("admit");
    if (f.loseAfter >= 0 && ++f.admitCalls > f.loseAfter) f.admitted = false;
    return f.admitted;
}
bool LocalUser(void*, uintptr_t user) { return user == kActor || user == kEntity; }
QuantumCVarType CVarType(void* c, uint32_t i) { F(c).log.push_back("type"); return F(c).type[i]; }
bool CVarRead(void* c, uint32_t i, int32_t* out) {
    F(c).log.push_back("read cvar" + std::to_string(i));
    *out = F(c).cvar[i];
    return true;
}
bool CVarWrite(void* c, uint32_t i, int32_t v) {
    auto& f = F(c);
    f.log.push_back("write cvar" + std::to_string(i));
    if (f.failCVarWrite == static_cast<int>(i)) return false;
    f.cvar[i] = v;
    return true;
}
bool DriveIdentity(void* c, uintptr_t drive, uintptr_t* id) {
    if (!F(c).driveOk || drive != kDrive) return false;
    *id = kId;
    return true;
}
bool DriveSample(void* c, uintptr_t, QuantumDriveSample* out) {
    out->state = F(c).state;
    out->charge = F(c).charge;
    return F(c).driveOk;
}
bool ParamCell(void*, uintptr_t, uintptr_t* cell) { *cell = kParamCell; return true; }
bool ChargeCell(void*, uintptr_t, uintptr_t* cell) { *cell = kChargeCell; return true; }
bool CellRead(void* c, uintptr_t cell, float* out) {
    auto& f = F(c);
    if (cell == kParamCell) { f.log.push_back("read param"); *out = f.param; return true; }
    if (cell == kChargeCell) { f.log.push_back("read charge"); *out = f.charge; return true; }
    return false;
}
bool CellWrite(void* c, uintptr_t cell, float v) {
    auto& f = F(c);
    if (cell == kParamCell) { f.log.push_back("write param"); f.param = v; return true; }
    if (cell == kChargeCell) {
        f.log.push_back("write charge");
        if (f.failChargeWrite) return false;
        f.charge = v;
        return true;
    }
    return false;
}
bool SendInput(void* c, uintptr_t, int32_t action, int32_t mode, float value) {
    auto& f = F(c);
    assert(action == kInputAction && (value == 0.0f || value == 1.0f));
    f.log.push_back(mode == kInputPress ? "press" : "release");
    f.pressed = mode == kInputPress;
    return true;
}
uint32_t NowMs(void* c) { return F(c).now; }
QuantumEngineOps Ops(Fake& f) {
    return {&f, Admitted, LocalUser, CVarType, CVarRead, CVarWrite, DriveIdentity, DriveSample,
            ParamCell, ChargeCell, CellRead, CellWrite, SendInput, NowMs};
}
bool CVarsRestored(const Fake& f) { return f.cvar[0] == 0 && f.cvar[1] == 0 && f.cvar[2] == 1 && f.cvar[3] == 0; }
bool Restored(const Fake& f) { return CVarsRestored(f) && f.param == 5.0f && f.charge == 0.4f && !f.pressed; }
void Rebind(OwnedQuantum& q, Fake& f) { f.driveOk = true; q.OnStartUse(kDrive, kActor); }

void UnitTests() {
    {
        Fake f; OwnedQuantum q(Ops(f));
        q.OnStartUse(kDrive, kActor);
        assert(q.Drive() == kDrive);
        f.log.clear();
        assert(q.Apply(true, true));
        assert(f.cvar[0] == 1 && f.cvar[1] == 1 && f.cvar[2] == 1 && f.cvar[3] == 1);
        assert(f.param == kDisableChargeOff && f.pressed);
        for (size_t k = 0; k < f.log.size(); ++k) {
            if (f.log[k].rfind("write", 0) != 0 && f.log[k] != "press") continue;
            assert(k >= 1 && f.log[k - 1] == "admit");
        }
        for (uint32_t i = 0; i < kOwnedCVars; ++i) {
            const size_t read = f.First("read cvar" + std::to_string(i)), write = f.First("write cvar" + std::to_string(i));
            if (i == 2) assert(write == f.log.size());
            else assert(read < write && write < f.log.size());
        }
        assert(f.First("read param") < f.First("write param") && f.First("write param") < f.log.size());
        assert(q.Apply(false, false) && Restored(f));
        std::puts("quantum_owned: first prior saved before each write, admission before each write, apply(false,false) restores");
    }
    {
        Fake f; OwnedQuantum q(Ops(f));
        q.OnStartUse(kDrive, kActor);
        assert(q.Apply(true, true) && !Restored(f));
        assert(q.Apply(true, false) && Restored(f));
        assert(q.Apply(true, true) && f.cvar[0] == 1);
        assert(q.Apply(false, false) && Restored(f));
        const int writes = f.Writes();
        assert(q.Apply(false, false) && f.Writes() == writes);
        std::puts("quantum_owned: release restores every saved value and is idempotent");
    }
    {
        Fake f; OwnedQuantum q(Ops(f));
        q.OnStartUse(kDrive, kActor);
        assert(q.Apply(true, true));
        f.cvar[1] = 7; f.param = 123.0f;
        assert(q.Apply(false, false));
        assert(f.cvar[1] == 7 && f.param == 123.0f && f.cvar[0] == 0 && f.cvar[3] == 0);
        std::puts("quantum_owned: restore leaves another actor's later value alone");
    }
    {
        Fake f; OwnedQuantum q(Ops(f));
        q.OnStartUse(kDrive, kActor);
        assert(q.Apply(true, true) && !Restored(f));
        f.admitted = false;
        QuantumObservation o;
        assert(q.Observe(&o) && !o.offline && !o.antiCheatAbsent);
        assert(q.Apply(false, false) && Restored(f));
        const int writes = f.Writes();
        assert(q.Apply(true, true) && f.Writes() == writes && Restored(f));
        Fake g; OwnedQuantum r(Ops(g));
        r.OnStartUse(kDrive, kActor);
        g.admitCalls = 0; g.loseAfter = 3;
        assert(r.Apply(true, true) && Restored(g) && !r.ControlActive());
        std::puts("quantum_owned: admission loss restores and stops writing");
    }
    {
        Fake f; OwnedQuantum q(Ops(f));
        q.OnStartUse(kDrive, kActor);
        assert(q.Apply(true, true));
        f.driveOk = false;
        QuantumObservation o;
        assert(q.Observe(&o) && o.offline && !o.drive && q.Drive() == 0);
        const int releases = f.Count("release");
        assert(q.Apply(false, false));
        assert(CVarsRestored(f) && f.param == 5.0f && f.Count("release") == releases);
        std::puts("quantum_owned: drive loss restores shared state and skips the dead drive");
    }
    {
        Fake f; OwnedQuantum q(Ops(f));
        q.OnStartUse(kDrive, kOther);
        assert(q.Drive() == 0 && f.log.empty());
        QuantumObservation o;
        assert(q.Observe(&o) && !o.drive);
        f.state = 4;
        assert(!q.KeepFull(kDrive) && f.log.size() == 1);
        q.OnStartUse(kDrive, kEntity);
        assert(q.Drive() == kDrive);
        std::puts("quantum_owned: non-local user passes through untouched, exact local actor/entity binds");
    }
    {
        Fake f; OwnedQuantum q(Ops(f));
        q.OnStartUse(kDrive, kActor);
        f.type[2] = QuantumCVarType::Unknown;
        f.log.clear();
        assert(!q.Apply(true, true));
        assert(f.Writes() == 0 && f.Count("read cvar0") == 0 && f.Count("read cvar2") == 0 && !q.ControlActive());
        assert(q.Apply(false, false) && f.Writes() == 0);
        std::puts("quantum_owned: a cvar whose type is not the verified int refuses before any read or write");
    }
    {
        Fake f; OwnedQuantum q(Ops(f));
        q.OnStartUse(kDrive, kActor);
        f.state = 3; f.now = 1000;
        assert(!q.KeepFull(kDrive) && f.Writes() == 0);
        assert(q.Apply(true, true));
        const int before = f.Writes();
        assert(!q.KeepFull(kDrive));
        f.now = 1000 + kChargeSettleMs;
        assert(!q.KeepFull(kDrive) && f.Writes() == before);
        f.now += 1;
        assert(q.KeepFull(kDrive) && f.charge == 1.0f);
        assert(f.First("read charge") < f.First("write charge") && f.First("write charge") < f.log.size());
        assert(q.Apply(false, false) && f.charge == 0.4f && Restored(f));
        f.state = 4; f.charge = NAN;
        assert(q.Apply(true, true) && !q.KeepFull(kDrive) && std::isnan(f.charge));
        QuantumObservation o;
        assert(q.Observe(&o) && o.block == SC_QUANTUM_BLOCK_ENGINE);
        f.charge = 0.5f;
        assert(q.Observe(&o) && o.phase == SC_QUANTUM_BOOSTING && o.progress == 500000 && o.block == SC_QUANTUM_BLOCK_NONE);
        f.state = 3;
        assert(q.Observe(&o) && o.phase == SC_QUANTUM_CHARGING);
        std::puts("quantum_owned: charge kept full only while holding, prior saved first and restored, invalid charge refused");
    }
    {
        Fake f; OwnedQuantum q(Ops(f));
        q.OnStartUse(kDrive, kActor);
        assert(q.Apply(true, true));
        f.failCVarWrite = 1;
        assert(!q.Apply(false, false));
        assert(f.cvar[0] == 0 && f.cvar[1] == 1);
        Fake g; OwnedQuantum r(Ops(g));
        r.OnStartUse(kDrive, kActor);
        assert(r.Apply(true, true));
        g.state = 4; g.failChargeWrite = true;
        assert(!r.KeepFull(kDrive) && r.Faulted());
        QuantumObservation o;
        assert(!r.Observe(&o) && !r.Apply(false, false));
        std::puts("quantum_owned: a failed restore or write reports failure so the provider latches restart");
    }
}
}

int main(int argc, char**) {
    UnitTests();
    sco::SetGameThread();
    const auto* api = sco::host::BuildApi({"quantum-owned-test"});
    assert(sco::AddReleaseHook(ReleaseQuantumOwner) == sco::Result::Ok);
    Fake f; OwnedQuantum owner(Ops(f));
    const QuantumBackend backend = MakeOwnedQuantumBackend(owner, nullptr, 0);
    assert(StartQuantum(backend, true) == sco::Result::Ok && api->has("game.quantum.control"));
    const void* table = nullptr;
    assert(api->query_service(SC_QUANTUM_SERVICE_NAME, SC_QUANTUM_SERVICE_VERSION, &table) == SCO_OK && table);
    const auto* q = static_cast<const sc_quantum_v1*>(table);
    sc_quantum_state st{sizeof(st), 0, 0, 0, 0, 0, 0};
    Rebind(owner, f);
    TickQuantum(1000);
    if (argc > 1) {
        auto* c = sco::host::NewPlugin("quantum_owned_c");
        assert(q->enable(c, 1) == SCO_OK && q->hold(c, 500) == SCO_OK && f.cvar[0] == 1);
        f.failCVarWrite = 0;
        assert(q->release(c) == SCO_FAILED);
        assert(!api->has("game.quantum.control") && f.cvar[0] == 1);
        assert(q->state(c, &st) == SCO_FAILED && st.block == SC_QUANTUM_BLOCK_RESTART);
        const size_t calls = f.log.size();
        assert(q->enable(c, 1) == SCO_FAILED && q->hold(c, 10) == SCO_FAILED);
        TickQuantum(1100);
        assert(sco::Release(c) == sco::Result::Ok);
        StopQuantum();
        assert(f.log.size() == calls && f.cvar[0] == 1);
        std::puts("quantum_owned: restore failure latches restart and suppresses every later callback");
        return 0;
    }
    auto* a = sco::host::NewPlugin("quantum_owned_a");
    assert(q->enable(a, 1) == SCO_OK && q->hold(a, 500) == SCO_OK);
    assert(f.cvar[0] == 1 && f.param == kDisableChargeOff && f.pressed);
    assert(q->release(a) == SCO_OK && Restored(f));
    assert(q->state(a, &st) == SCO_OK && st.enabled && !st.held);
    assert(q->hold(a, 500) == SCO_OK && f.cvar[0] == 1);
    assert(q->enable(a, 0) == SCO_OK && Restored(f));
    std::puts("quantum_owned: provider release and disable restore");

    assert(q->enable(a, 1) == SCO_OK && q->hold(a, 500) == SCO_OK && !Restored(f));
    f.admitted = false;
    TickQuantum(1100);
    assert(Restored(f) && q->state(a, &st) == SCO_UNAVAILABLE && !st.enabled && st.block == SC_QUANTUM_BLOCK_OFFLINE);
    f.admitted = true;
    std::puts("quantum_owned: admission loss mid-hold restores through the provider tick");

    assert(q->enable(a, 1) == SCO_OK && q->hold(a, 500) == SCO_OK && !Restored(f));
    f.driveOk = false;
    TickQuantum(1200);
    assert(CVarsRestored(f) && f.param == 5.0f && owner.Drive() == 0);
    assert(q->state(a, &st) == SCO_UNAVAILABLE && !st.enabled && st.block == SC_QUANTUM_BLOCK_DRIVE);
    f.pressed = false;
    Rebind(owner, f);
    std::puts("quantum_owned: drive loss mid-hold restores through the provider tick");

    TickQuantum(2000);
    assert(q->enable(a, 1) == SCO_OK && q->hold(a, 50) == SCO_OK && !Restored(f));
    TickQuantum(2049);
    assert(!Restored(f));
    TickQuantum(2050);
    assert(Restored(f) && q->state(a, &st) == SCO_OK && st.enabled && !st.held);
    std::puts("quantum_owned: lease expiry restores and keeps the owner");

    assert(q->hold(a, 500) == SCO_OK && !Restored(f));
    StopQuantum();
    assert(Restored(f) && !api->has("game.quantum.control"));
    assert(StartQuantum(backend, true) == sco::Result::Ok);
    std::puts("quantum_owned: stop while holding restores");

    auto* b = sco::host::NewPlugin("quantum_owned_b");
    assert(q->enable(b, 1) == SCO_OK && q->hold(b, 500) == SCO_OK && !Restored(f));
    assert(sco::Release(b) == sco::Result::Ok && Restored(f));
    StopQuantum();
    std::puts("quantum_owned: owner unload while holding restores");
    return 0;
}
