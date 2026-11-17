// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// Process-retained host composition: prepare (signatures, optional quit hook, early DataCore) ->
// start the NORMAL app runtime -> tick -> stop by closure. See Options::composition in host.h.
// Nothing here is ever unloaded or freed: the state below is immortal heap storage that module-static
// teardown never destroys, and Stop closes admission only.
#include "retained_host.h"
#include "../api/internal.h"
#include "../app/process_retained.h"
#include "../owned_shutdown_access.h"
#include "sco/game/signatures.h"
#include "sco/signatures.h"
#include "sco/log.h"
#include "sco/game/system.h"
#include "sco/game/pak.h"
#include "pak_retained.h"
#include "sco/hook.h"
#include "sco/caps.h"
#include "sco/datacore_service.h"
#include "admission.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace sco::game::host::retained {
namespace {
using QuitFn = void (*)(void* system, void* info, int exitCode);
std::atomic<int> claim{0}; // 0 none, 1 strict, 2 retained; never released
std::atomic<Phase> phase{Phase::Idle};
std::atomic<bool> resolved{false}, quitInstalled{false}, dataActive{false}, stopRequested{false};
std::atomic<OwnedShutdown*> ownerPtr{nullptr}; // always points into Owners(): never dangles
std::atomic_flag control = ATOMIC_FLAG_INIT;
QuitFn quitOriginal = nullptr;
#ifdef SCO_GAME_HOST_REPORT_TESTING
std::atomic<const pak::Targets*> fakePakTargets{nullptr};
#endif
bool ticked = false;
uint32_t lastTick = 0;

struct Data {
    std::mutex mutex;
    struct Inputs { std::vector<plugins::Plugin> list; plugins::ContentIndex index; };
    std::shared_ptr<const Inputs> inputs;
    std::shared_ptr<const vfs::Table> mounts;
};
Data& D() { static Data* data = new Data; return *data; }
Options& Opt() { static Options* options = new Options; return *options; }
std::shared_ptr<SignatureRegistry>& Registry() { static auto* registry = new std::shared_ptr<SignatureRegistry>; return *registry; }
std::vector<std::shared_ptr<OwnedShutdown>>& Owners() {
    static auto* owners = new std::vector<std::shared_ptr<OwnedShutdown>>;
    return *owners;
}

// One nonblocking gate over every control entry; a reentrant or overlapping call refuses.
struct Gate {
    bool entered = !control.test_and_set(std::memory_order_acquire);
    ~Gate() { if (entered) control.clear(std::memory_order_release); }
};

void RevokeCapabilities() noexcept { caps::InvalidateAll("signature host unavailable"); }
bool Admitted() noexcept {
    try {
        const Admission a = Opt().inspect();
        return a.session == Session::Offline && a.antiCheat == Check::Clear &&
               a.officialNetwork == Check::Clear && a.targetProcess == Check::Clear;
    } catch (...) { return false; }
}
// The shell's attestation that engine execution is quiesced; a throw is false.
bool Quiesced() noexcept {
    try {
        const auto& pf = Opt().platform;
        return pf.gameReadersQuiesced && pf.gameReadersQuiesced(pf.gameReadersContext);
    } catch (...) { return false; }
}
// Closure of the optional owner: no wait, no drain, no fault. The product drains from its own thread.
void CloseOwner() noexcept {
    try { if (auto* owner = ownerPtr.load(std::memory_order_acquire)) owner->request_stop(); } catch (...) {}
}
// Before any exposure: back to Idle with nothing installed.
Result Refuse(Result result) noexcept {
    ownerPtr.store(nullptr, std::memory_order_release);
    Registry().reset();
    resolved = false;
    stopRequested = false;
    try { Opt() = Options(); } catch (...) {}
    phase = Phase::Idle;
    return result;
}
// After exposure: retained, no restart.
Result Fail(Result result) noexcept {
    detail::SetFreshAdmissionEnabled(false);
    CloseOwner();
    phase = Phase::Failed;
    return result;
}

// Non-waiting closure request: nothing blocks, joins or frees. Closes the optional owner and the fresh
// admission predicate, and asks the next Tick to run the game-thread closure.
void RequestClosure() noexcept {
    stopRequested.store(true);
    detail::SetFreshAdmissionEnabled(false);
    CloseOwner();
}
void QuitDetour(void* system, void* info, int code) {
    // Closure runs on this stack, then the original is forwarded exactly once. A quit issued from inside
    // that closure (a game.exit subscriber) is absorbed: the outer detour forwards for both.
    static thread_local bool closing = false;
    if (closing) { Log("[game-host] reentrant quit during closure ignored"); return; }
    closing = true;
    try {
        const Result r = retained::Stop();
        if (r != Result::Ok) {
            // Closure ran nowhere (a tick or callout holds the gate, or this is not the game thread):
            // close admission without waiting, and let the next Tick run the closure. game.exit is
            // not guaranteed on this path.
            RequestClosure();
            Log("[game-host] quit closure refused: %s; admission closed, game.exit not guaranteed", ResultName(r));
        }
    } catch (...) { RequestClosure(); }
    closing = false;
    if (quitOriginal) quitOriginal(system, info, code);
}
Result InstallQuit() {
    QuitHook target;
    if (!QuitFunction(target)) return Result::Unavailable;
    if (hook::InstallDetour(target.fn, target.stolenBytes, reinterpret_cast<void*>(&QuitDetour),
                            reinterpret_cast<void**>(&quitOriginal)) != hook::Error::None)
        return Result::Failed;
    quitInstalled = true;
    return Result::Ok;
}

int HasDataRequirement(const char* name) { return name && (caps::Has(name) || ServiceExists(name)) ? 1 : 0; }
std::shared_ptr<const vfs::Table> Mounts() {
    auto& d = D();
    std::lock_guard<std::mutex> hold(d.mutex);
    return dataActive.load() ? d.mounts : nullptr; // retained after Stop: an in-flight producer may still hold it
}
vfs::Result TransformDataCore(vfs::BaseIo& base, uint64_t size, std::vector<vfs::Splice>& out) {
    // Copy the shared inputs under the lock and release it before any I/O or parsing; Start takes the
    // same mutex on the game thread.
    std::shared_ptr<const Data::Inputs> inputs;
    {
        auto& d = D();
        std::lock_guard<std::mutex> hold(d.mutex);
        inputs = d.inputs;
    }
    const Phase p = phase.load();
    if (!dataActive || !inputs ||
        (p != Phase::Prepared && p != Phase::Activated && p != Phase::Starting && p != Phase::Running) ||
        !Admitted())
        return {"offline DataCore admission unavailable"};
    const auto& options = Opt();
    if (!size || size > options.maxDataCoreBytes || size > std::numeric_limits<size_t>::max())
        return {"DataCore base exceeds host byte limit"};
    try {
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!base.Seek(0)) return {"DataCore base seek failed"};
        size_t got = 0;
        while (got < bytes.size()) {
            const size_t n = base.Read(bytes.data() + got, bytes.size() - got);
            if (!n || n > bytes.size() - got) return {"DataCore base read failed"};
            got += n;
        }
        datacore::Schema schema;
        if (!schema.Parse(bytes)) return {"DataCore schema refused: " + schema.error};
        const auto loaded = datacore::service::Load(schema, inputs->list, inputs->index,
                                                    options.platform.dataRoot);
        if (!loaded.result.status) return {loaded.result.status.message};
        out = loaded.result.splices;
        return {};
    } catch (...) { return {"DataCore transform failed"}; }
}
// Wall time per Prepare phase in milliseconds (steady clock), logged once when Prepare returns so a
// slow start shows where it went. Written only inside Prepare, which the gate serializes.
using Clock = std::chrono::steady_clock;
struct PrepareTimes {
    int64_t admission = 0, registration = 0, freeze = 0, resolve = 0, install = 0, quiesce = 0, quit = 0;
    int64_t dataService = 0, discovery = 0, index = 0, mounts = 0, pak = 0, data = 0, total = 0;
};
PrepareTimes times;
int64_t SinceMs(Clock::time_point from) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - from).count();
}
struct PrepareLog {
    Clock::time_point start = Clock::now();
    PrepareLog() { times = {}; }
    ~PrepareLog() {
        times.total = SinceMs(start);
        try {
            Log("[game-host] prepare timing ms: total %lld, admission %lld, sig-register %lld, freeze %lld, resolve %lld, "
                "quiesce %lld, registry-install %lld, quit-hook %lld, datacore %lld",
                static_cast<long long>(times.total), static_cast<long long>(times.admission),
                static_cast<long long>(times.registration), static_cast<long long>(times.freeze),
                static_cast<long long>(times.resolve), static_cast<long long>(times.quiesce),
                static_cast<long long>(times.install), static_cast<long long>(times.quit),
                static_cast<long long>(times.data));
            Log("[game-host] prepare datacore ms: service-start %lld, plugin-discovery %lld, index-build %lld, "
                "mount-build %lld, pak-enable %lld",
                static_cast<long long>(times.dataService), static_cast<long long>(times.discovery),
                static_cast<long long>(times.index), static_cast<long long>(times.mounts),
                static_cast<long long>(times.pak));
        } catch (...) {}
    }
};
Result PrepareData() {
    const auto& pf = Opt().platform;
    if (!pf.dataCore) return Result::Ok;
    auto mark = Clock::now();
    const Result r = datacore::service::Start({pf.dataRoot});
    times.dataService = SinceMs(mark);
    if (r != Result::Ok) return r;
    dataActive = true;
    auto inputs = std::make_shared<Data::Inputs>();
    plugins::Options po;
    po.enabled = pf.pluginsEnabled;
    po.has = HasDataRequirement;
    mark = Clock::now();
    inputs->list = plugins::Discover(pf.pluginRoot, po);
    times.discovery = SinceMs(mark);
    mark = Clock::now();
    inputs->index.Build(inputs->list); // no plugin callbacks during early load
    times.index = SinceMs(mark);
    mark = Clock::now();
    vfs::Mount mount;
    mount.path = "Data/Game2.dcb"; mount.source = "host.datacore";
    mount.producer = vfs::Transform(TransformDataCore);
    auto mounts = vfs::Table::Build({std::move(mount)});
    times.mounts = SinceMs(mark);
    if (!mounts) return Result::Failed;
    {
        auto& d = D();
        std::lock_guard<std::mutex> hold(d.mutex);
        d.inputs = std::move(inputs);
        d.mounts = std::move(mounts);
    }
    if (!Admitted()) return Result::Unavailable;
    if (Opt().dataCoreHook == DataCoreHook::Required) {
        struct PakTimer { Clock::time_point from = Clock::now(); ~PakTimer() { times.pak = SinceMs(from); } } pakTimer;
        pak::Options poPak; poPak.mounts = Mounts;
        // The entry calls the shell's attestation itself, immediately before the patch; latched after Ok.
#ifdef SCO_GAME_HOST_REPORT_TESTING
        const pak::Targets* fake = fakePakTargets.load();
        const Result pr = fake ? pak::retained::Enable(poPak, *fake, pf.gameReadersQuiesced, pf.gameReadersContext)
                               : pak::retained::Enable(poPak, pf.gameReadersQuiesced, pf.gameReadersContext);
#else
        const Result pr = pak::retained::Enable(poPak, pf.gameReadersQuiesced, pf.gameReadersContext);
#endif
        if (pr != Result::Ok) return pr;
    }
    return Result::Ok;
}

bool ValidOptions(const Options& o) noexcept {
    const auto& p = o.platform;
    return o.size >= sizeof(Options) && o.composition == Composition::ProcessRetained && o.inspect &&
        o.tickIntervalMs <= 0x7fffffffu && p.gameReadersQuiesced && !p.shutdownOwner &&
        !p.image && !p.registerSignatures && !p.beforeDependentStop &&
        !o.earlyDataRequirement && !o.retireConsumer && !o.nativeDataCoreReads && !o.preEngineQuiesced &&
        (o.quit == QuitPolicy::Required || o.quit == QuitPolicy::External) &&
        (o.dataCoreHook == DataCoreHook::Required || o.dataCoreHook == DataCoreHook::External) &&
        (!p.dataCore || (!p.dataRoot.empty() && !p.nBuiltins && o.maxDataCoreBytes &&
                         o.maxDataCoreBytes <= vfs::kMaxFileSize));
}

DrainDisposition ObserveDrain() noexcept {
    const auto* owner = ownerPtr.load(std::memory_order_acquire);
    if (!owner) return stopRequested.load() ? DrainDisposition::Requested : DrainDisposition::Open;
    switch (owner->drain_state()) {
    case OwnedShutdown::DrainState::Open: return DrainDisposition::Open;
    case OwnedShutdown::DrainState::Requested: return DrainDisposition::Requested;
    case OwnedShutdown::DrainState::InProgress: return DrainDisposition::InProgress;
    case OwnedShutdown::DrainState::Drained: return DrainDisposition::Drained;
    case OwnedShutdown::DrainState::Retained: return DrainDisposition::Retained;
    case OwnedShutdown::DrainState::Unknown: return DrainDisposition::Unknown;
    }
    return DrainDisposition::Unknown;
}
HostReport Report(Result result) noexcept {
    const Phase p = phase.load();
    return {result, p, p == Phase::Idle ? uint64_t{0} : uint64_t{1}, DependencyDisposition::ProcessRetained, ObserveDrain()};
}

Result StopLocked() {
    const Phase p = phase.load();
    if (p == Phase::Idle || p == Phase::Stopped) return Result::Ok;
    if (p == Phase::Prepared || p == Phase::Activated) { // nothing runs; keep every hook and mapping
        detail::SetFreshAdmissionEnabled(false);
        CloseOwner();
        phase = Phase::Stopped;
        return Result::Ok;
    }
    if (p != Phase::Running && p != Phase::Failed) return Result::Unavailable;
    if (!OnGameThread()) return Result::WrongThread;
    if (sco::detail::InCallout()) return Result::Unavailable;
    detail::SetFreshAdmissionEnabled(false);
    CloseOwner();
    if (p == Phase::Failed) return app::Stop(); // best-effort closure of a failed start; stays Failed
    Phase expected = Phase::Running;
    if (!phase.compare_exchange_strong(expected, Phase::Stopping)) return Result::Unavailable;
    const Result r = app::Stop(); // closure for a process-retained runtime; never an unload
    if (r != Result::Ok) {
        try { Log("[game-host] closure failed: %s; backing retained", ResultName(r)); } catch (...) {}
        phase = Phase::Failed;
        return r;
    }
    phase = Phase::Stopped;
    return Result::Ok;
}
} // namespace

bool ClaimStrict() noexcept {
    int expected = 0;
    return claim.compare_exchange_strong(expected, 1) || expected == 1;
}
static bool ClaimRetained() noexcept {
    int expected = 0;
    return claim.compare_exchange_strong(expected, 2) || expected == 2;
}
bool Selected() noexcept { return claim.load() == 2; }

Result Prepare(const Options& o) {
    Gate gate;
    if (!gate.entered || !ClaimRetained()) return Result::BadArg;
    Phase expected = Phase::Idle;
    if (!phase.compare_exchange_strong(expected, Phase::Preparing)) return Result::BadArg; // also blocks inspector reentry
    if (!ValidOptions(o)) { phase = Phase::Idle; return Result::BadArg; }
    PrepareLog timing; // one pair of log lines whichever way Prepare returns
    const auto timedAdmitted = [] {
        const auto mark = Clock::now();
        const bool ok = Admitted();
        times.admission += SinceMs(mark);
        return ok;
    };
    try {
        if (o.shutdownOwner) Owners().push_back(o.shutdownOwner);
        Opt() = o;
        ownerPtr.store(o.shutdownOwner.get(), std::memory_order_release);
    } catch (...) { return Refuse(Result::Failed); }
    if (!timedAdmitted()) return Refuse(Result::Unavailable);
    if (!o.imageOwner || !o.resolverOwner || !o.image || !o.image->base || !o.image->size ||
        !o.image->text.base || !o.image->text.size || !o.image->rdata.base || !o.image->rdata.size)
        return Refuse(Result::BadArg);
    try {
        if (AcquireHostSignatureRegistry()) {
            Log("[game-host] signature registry already owned");
            return Refuse(Result::Unavailable);
        }
        auto candidate = std::make_shared<SignatureRegistry>(o.signatureRegistryBudgetBytes);
        auto mark = Clock::now();
        const auto registration = RegisterGameSignatures(*candidate);
        times.registration = SinceMs(mark);
        if (registration.status != SigRegistryStatus::Ok) {
            Log("[game-host] signature registration failed: %s (table %zu, row %zu)",
                SigRegistryStatusName(registration.status), registration.table, registration.row);
            return Refuse(Result::Failed);
        }
        mark = Clock::now();
        auto status = candidate->Freeze();
        times.freeze = SinceMs(mark);
        if (status != SigRegistryStatus::Ok) {
            Log("[game-host] signature freeze failed: %s", SigRegistryStatusName(status));
            return Refuse(Result::Failed);
        }
        Registry() = candidate; // resolver code may run: keep the registry (and owners) until exit once it does
        mark = Clock::now();
        status = candidate->Resolve(*o.image, o.imageOwner, o.resolverOwner);
        times.resolve = SinceMs(mark);
        if (status != SigRegistryStatus::Ok) {
            Log("[game-host] signature resolution failed: %s", SigRegistryStatusName(status));
            return Fail(Result::Failed);
        }
        if (!timedAdmitted()) return Refuse(Result::Unavailable);
        // Rule B: the shell attests quiescence immediately before the first native mutation.
        mark = Clock::now();
        const bool quiesced = Quiesced();
        times.quiesce = SinceMs(mark);
        if (!quiesced) {
            Log("[game-host] prepare refused: native install quiescence not attested");
            return Refuse(Result::Unavailable);
        }
        auto* owner = ownerPtr.load();
        if (owner && !OwnedShutdownHostAccess::begin_startup(*owner)) return Refuse(Result::Unavailable);
        auto transition = owner ? OwnedShutdownHostAccess::transition(*owner) : OwnedShutdown::Transition{};
        if (owner && !transition) return Fail(Result::Unavailable);
        mark = Clock::now();
        const bool installed = InstallHostSignatureRegistry(candidate, RevokeCapabilities);
        times.install = SinceMs(mark);
        if (!installed) {
            Log("[game-host] signature registry install refused");
            return Fail(Result::Unavailable);
        }
        resolved = true;
        if (!timedAdmitted()) return Fail(Result::Unavailable);
        if (o.quit == QuitPolicy::Required) {
            mark = Clock::now();
            const Result q = InstallQuit();
            times.quit = SinceMs(mark);
            if (q != Result::Ok) return Fail(q);
        }
        mark = Clock::now();
        const Result d = PrepareData();
        times.data = SinceMs(mark);
        if (d != Result::Ok) return Fail(d);
        if (!timedAdmitted()) return Fail(Result::Unavailable);
        detail::SetFreshInspector(o.inspect);
        if (owner) {
            if (!transition.publish(phase, Phase::Preparing, Phase::Prepared)) return Fail(Result::Unavailable);
        } else phase = Phase::Prepared;
        return Result::Ok;
    } catch (...) {
        try { Log("[game-host] preparation failed with an exception"); } catch (...) {}
        return Fail(Result::Failed);
    }
}

Result Activate() {
    Gate gate;
    if (!gate.entered) return Result::BadArg;
    Phase expected = Phase::Prepared; // all native preparation happened in Prepare
    return phase.compare_exchange_strong(expected, Phase::Activated) ? Result::Ok : Result::BadArg;
}

Result Start() {
    Gate gate;
    if (!gate.entered) return Result::BadArg;
    Phase from = Phase::Prepared;
    if (!phase.compare_exchange_strong(from, Phase::Starting)) {
        from = Phase::Activated;
        if (!phase.compare_exchange_strong(from, Phase::Starting)) return Result::BadArg;
    }
    auto* owner = ownerPtr.load();
    auto transition = owner ? OwnedShutdownHostAccess::transition(*owner) : OwnedShutdown::Transition{};
    if ((owner && !transition) || !Admitted()) { phase = from; return Result::Unavailable; } // retryable refusal
    detail::SetFreshAdmissionEnabled(true);
    try {
        auto platform = Opt().platform;
        platform.dataCore = false; // service already OPEN/LOADED; NEVER reset an early load
        platform.privateSessionAdmitted = Admitted; // exact same fail-closed admission
        if (app::detail::StartProcessRetained(platform) != app::StartResult::Started) return Fail(Result::Failed);
        if (dataActive) {
            auto inputs = std::make_shared<Data::Inputs>();
            inputs->list = app::Plugins(); inputs->index = app::Content();
            auto& d = D();
            std::lock_guard<std::mutex> hold(d.mutex);
            d.inputs = std::move(inputs);
        }
        if (owner) {
            if (!transition.publish_and_open(phase, Phase::Starting, Phase::Running)) {
                (void)app::Stop(); // the owner closed during startup: close the runtime as well
                return Fail(Result::Unavailable);
            }
        } else phase = Phase::Running;
        return Result::Ok;
    } catch (...) { return Fail(Result::Failed); }
}

Result Tick(uint32_t nowMs) {
    Gate gate;
    if (!gate.entered) return Result::Unavailable;
    if (phase.load() != Phase::Running) return Result::Unavailable;
    if (!OnGameThread()) return Result::WrongThread;
    if (sco::detail::InCallout()) return Result::Unavailable;
    if (stopRequested.load() || !Admitted()) { // lost admission or a requested stop: closure, no queued work
        StopLocked();
        return Result::Unavailable;
    }
    if (ticked && nowMs - lastTick < Opt().tickIntervalMs) return Result::Ok;
    ticked = true; lastTick = nowMs;
    app::Tick(nowMs);
    return Result::Ok;
}

Result Stop() {
    Gate gate;
    if (!gate.entered) return Result::Unavailable;
    return StopLocked();
}
Result BeforeQuit() { return Stop(); }
Snapshot Status() { return {phase.load(), resolved.load(), quitInstalled.load(), dataActive.load()}; }

HostReport ActivateDetailed() noexcept { try { return Report(Activate()); } catch (...) { return Report(Fail(Result::Failed)); } }
HostReport StartDetailed() noexcept { try { return Report(Start()); } catch (...) { return Report(Fail(Result::Failed)); } }
HostReport StopDetailed() noexcept { try { return Report(Stop()); } catch (...) { return Report(Fail(Result::Failed)); } }
// Any thread, inside a callout: close the owner and ask the next Tick to run the game-thread stop.
HostReport RequestStop() noexcept {
    if (phase.load() == Phase::Idle) return Report(Result::Unavailable);
    stopRequested.store(true);
    Result result = Result::Ok;
    if (auto* owner = ownerPtr.load(std::memory_order_acquire)) {
        try { owner->request_stop(); } catch (...) { result = Result::Failed; }
    }
    auto report = Report(result);
    report.drain = DrainDisposition::Requested;
    return report;
}
HostReport StatusReport() noexcept { return Report(Result::Ok); }

#ifdef SCO_GAME_HOST_REPORT_TESTING
namespace testing {
void SetQuitOriginalForTest(QuitFn original) noexcept { quitOriginal = original; }
void InvokeQuitDetourForTest(void* system, void* info, int code) { QuitDetour(system, info, code); }
void SetPakTargetsForTest(const pak::Targets* targets) noexcept { fakePakTargets.store(targets); }
}
#endif
} // namespace sco::game::host::retained
