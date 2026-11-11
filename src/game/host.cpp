#include "sco/game/host.h"
#include "../api/internal.h"
#include "../api/owned_publication.h"
#include "../app/owned_start.h"
#include "../app/owned_subsystems.h"
#include "../owned_shutdown_access.h"
#include "../owned_registry_reservation.h"
#include "sco/game/signatures.h"
#include "sco/signatures.h"
#include "sco/log.h"
#include "sco/game/system.h"
#include "sco/game/pak.h"
#include "sco/hook.h"
#include "sco/caps.h"
#include "sco/datacore_service.h"
#include "host_testing.h"
#include "quit_owner_gate.h"
#include "admission.h"
#if defined(SCO_GAME_SERVICES)
#include "services/datacore_live_engine.h"
#endif
#include <atomic>
#include <cstring>
#include <new>
#include <mutex>
#include <limits>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <netfw.h>
#include <oleauto.h>
#undef min
#undef max
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

namespace sco::game::host {
namespace {
std::atomic<Phase> phase{Phase::Idle};
std::atomic<bool> resolved{false}, quitInstalled{false}, dataActive{false};
std::atomic<bool> testFailAfterAppStart{false}, testLogAfterRegistryInstall{false};
testing::QuitFn quitOriginal = nullptr;
Options options;
bool ticked = false, ticking = false, pakOwned = false;
uint32_t lastTick = 0;
std::mutex dataMutex;
std::atomic_flag dataTransformBusy = ATOMIC_FLAG_INIT;
struct DataInputs { std::vector<plugins::Plugin> list; plugins::ContentIndex index; };
std::shared_ptr<const DataInputs> dataInputs;
std::shared_ptr<const vfs::Table> dataMounts;
std::shared_ptr<SignatureRegistry> signatureRegistry;
enum class RetirementState { NotAttempted, InProgress, Quiesced, Retained };
std::atomic<RetirementState> consumerRetirement{RetirementState::NotAttempted};

// One physical game-host composition. No mutex is held through external code.
// The immutable root is intentionally never destroyed, including static teardown.
struct RetainedHost {
    std::shared_ptr<const HostCapsule> capsule;
    std::shared_ptr<SignatureRegistry> registry; // survives module-static teardown
    const uint64_t id = 1; // one terminal detailed session per process
};
std::atomic<RetainedHost*> retainedHost{nullptr};
detail::QuitOwnerGate legacyQuitOwner;
std::atomic<bool> legacyControlUsed{false};
std::atomic_flag hostControl = ATOMIC_FLAG_INIT;
#ifdef SCO_GAME_HOST_REPORT_TESTING
std::atomic<testing::ReportFault> reportFault{testing::ReportFault::None};
thread_local bool optionsAllocationArmed = false;
std::atomic<testing::StatusPause*> statusPause{nullptr};
std::atomic<testing::QuitOwnerPause*> quitOwnerPause{nullptr};
std::atomic<bool> quitOwnerAllocationFault{false}, quitOwnerNotificationFault{false};
void PauseQuitOwner(testing::QuitOwnerPauseStage stage) noexcept {
    auto* pause = quitOwnerPause.load(std::memory_order_acquire);
    if (!pause || pause->stage != stage ||
        !quitOwnerPause.compare_exchange_strong(pause, nullptr, std::memory_order_acq_rel)) return;
    pause->reached.store(true, std::memory_order_release);
    while (!pause->release.load(std::memory_order_acquire)) std::this_thread::yield();
}
#endif
struct ControlEntry {
    bool entered = !hostControl.test_and_set(std::memory_order_acquire);
    ~ControlEntry() { if (entered) hostControl.clear(std::memory_order_release); }
};
DrainDisposition ObserveDrain(const OwnedShutdown& owner) noexcept {
    switch (owner.drain_state()) {
    case OwnedShutdown::DrainState::Open: return DrainDisposition::Open;
    case OwnedShutdown::DrainState::Requested: return DrainDisposition::Requested;
    case OwnedShutdown::DrainState::InProgress: return DrainDisposition::InProgress;
    case OwnedShutdown::DrainState::Drained: return DrainDisposition::Drained;
    case OwnedShutdown::DrainState::Retained: return DrainDisposition::Retained;
    case OwnedShutdown::DrainState::Unknown: return DrainDisposition::Unknown;
    }
    return DrainDisposition::Unknown;
}
HostReport Report(Result result, bool uncertain = false) noexcept {
    // Refused overlapping control cannot later advertise a lower floor.
    // Pure status observation does not latch exposure or poison startup.
    if (uncertain) legacyControlUsed.store(true, std::memory_order_release);
    const auto observedPhase = phase.load();
    const auto* root = retainedHost.load(std::memory_order_acquire);
    const auto disposition = root || uncertain || legacyControlUsed.load(std::memory_order_acquire)
        ? DependencyDisposition::ProcessRetained : DependencyDisposition::NeverExposed;
    DrainDisposition drain = DrainDisposition::NotAdopted;
    if (root) {
        // Preserve the host's completed drain observation through the later
        // conservative owner fault; this field never grants cleanup authority.
        switch (consumerRetirement.load(std::memory_order_acquire)) {
        case RetirementState::InProgress:
            drain = ObserveDrain(*root->capsule->configuration().shutdownOwner);
            break;
        case RetirementState::Quiesced: drain = DrainDisposition::Drained; break;
        case RetirementState::Retained: drain = DrainDisposition::Retained; break;
        case RetirementState::NotAttempted:
            drain = ObserveDrain(*root->capsule->configuration().shutdownOwner);
            break;
        }
    } else if (uncertain || legacyControlUsed.load(std::memory_order_acquire)) {
        drain = DrainDisposition::Unknown;
    }
    return {result, observedPhase, root ? root->id : 0, disposition, drain};
}
bool DetailedConfiguration(const Options& o) noexcept {
    const auto& p = o.platform;
    return o.size >= sizeof(Options) && o.inspect && o.shutdownOwner &&
        o.quit == QuitPolicy::External && o.dataCoreHook == DataCoreHook::External &&
        !o.nativeDataCoreReads && !o.preEngineQuiesced && !o.earlyDataRequirement && !o.earlyDataContext &&
        !o.retireConsumer && !o.consumerContext && o.drainTimeoutMs && o.drainTimeoutMs <= 60000 &&
        o.tickIntervalMs <= 0x7fffffffu && !p.image && !p.registerSignatures &&
        !p.shutdownOwner && !p.dataCore && p.dataRoot.empty() && !p.gameServices && !p.gameReadersQuiesced &&
        !p.gameReadersContext && !p.pluginsEnabled && !p.builtins && !p.nBuiltins && !p.scripts &&
        !p.beforeDependentStop && !p.beforeDependentStopContext && !p.setCapabilities &&
        !p.hostService.context && !p.hostService.start && !p.hostService.tick && !p.hostService.stop &&
        !p.hostMenu.context && !p.hostMenu.load && !p.hostMenu.frame && !p.hostMenu.stop &&
        !p.diagnosticSources.diagnosticsPlugin && !p.runtimeInspection.enabled && !p.privateSession.enabled &&
        !p.privateSessionAdmitted && !p.reservedChords && !p.nReservedChords;
}


void InvalidateBeforeHostInstall() noexcept {
    caps::owned::InvalidateForOwner(options.shutdownOwner, "game host preparing");
}
void InvalidateFailedHostCapabilities() noexcept {
    caps::owned::InvalidateForOwner(options.shutdownOwner, "game host failed");
}

void RevokeFailedHostCapabilities() noexcept {
    if (signatureRegistry) InvalidateFailedHostCapabilities();
}

// Legacy retirement assertion retained for source migration only; it is not
// accepted as drain evidence and is never called by the owned supported path.
bool BeforeDependentStop(void*) noexcept {
    detail::SetFreshAdmissionEnabled(false);
    // The owned path does not invoke an unbounded legacy retirement callback.
    if (!options.shutdownOwner) return false;
    const RetirementState previous = consumerRetirement.load(std::memory_order_acquire);
    if (previous == RetirementState::Quiesced) return options.shutdownOwner->ready();
    if (previous != RetirementState::NotAttempted) return false;
    const auto ownerState = options.shutdownOwner->drain_state();
    if (ownerState == OwnedShutdown::DrainState::Drained) {
        consumerRetirement.store(RetirementState::Quiesced, std::memory_order_release);
        return true;
    }
    if (ownerState == OwnedShutdown::DrainState::InProgress ||
        ownerState == OwnedShutdown::DrainState::Retained ||
        ownerState == OwnedShutdown::DrainState::Unknown) {
        consumerRetirement.store(RetirementState::Retained, std::memory_order_release);
        RevokeFailedHostCapabilities();
        return false;
    }
    // Only Open or Requested can begin the one control-thread drain. A prior
    // failed/in-progress attempt is observed above and is never retried here.
    consumerRetirement.store(RetirementState::InProgress, std::memory_order_release);
    ConsumerRetirement answer = ConsumerRetirement::Retained;
    try {
        if (options.shutdownOwner->drain_until(std::chrono::steady_clock::now() +
                std::chrono::milliseconds(options.drainTimeoutMs))) answer = ConsumerRetirement::Quiesced;
    } catch (...) { options.shutdownOwner->fault(); }
    const auto completed = answer == ConsumerRetirement::Quiesced
        ? RetirementState::Quiesced : RetirementState::Retained;
    consumerRetirement.store(completed, std::memory_order_release);
    if (completed != RetirementState::Quiesced) RevokeFailedHostCapabilities();
    return completed == RetirementState::Quiesced;
}


bool Admitted() noexcept {
    try {
        const Admission a = options.inspect();
        return a.session == Session::Offline && a.antiCheat == Check::Clear &&
               a.officialNetwork == Check::Clear && a.targetProcess == Check::Clear;
    } catch (...) { return false; }
}
bool OwnsSignatureRegistry() noexcept {
    return signatureRegistry && options.shutdownOwner && !options.shutdownOwner->closed();
}
bool AdmittedAndOwnsSignatureRegistry() noexcept {
    return Admitted() && OwnsSignatureRegistry();
}
bool NativeBarrierHeld() noexcept {
    try {
        return options.platform.gameReadersQuiesced &&
            options.platform.gameReadersQuiesced(options.platform.gameReadersContext);
    } catch (...) { return false; }
}
void QuitDetour(void* system, void* info, int code) {
    // This is notification only: never take the control gate or drain an
    // obligation on the callback stack. Original arguments and thread stay live
    // on this stack; nothing here proves engine objects survive the original.
#ifdef SCO_GAME_HOST_REPORT_TESTING
    if (!quitOwnerNotificationFault.exchange(false)) (void)legacyQuitOwner.Notify();
#else
    (void)legacyQuitOwner.Notify();
#endif
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
int HasDataRequirement(const char* name) {
    if (!name || !Admitted()) return 0;
    const auto installed = AcquireHostSignatureRegistry();
    if (!installed || installed != signatureRegistry) return 0;
    if (ServiceExists(name))
        return Admitted() && AcquireHostSignatureRegistry() == installed ? 1 : 0;
    if (!options.earlyDataRequirement) return 0;
    const auto snapshot = installed->Snapshot();
    if (!snapshot) return 0;
    try {
        return options.earlyDataRequirement(name, *snapshot, options.earlyDataContext)
            && Admitted() && AcquireHostSignatureRegistry() == installed ? 1 : 0;
    } catch (...) { return 0; }
}
std::shared_ptr<const vfs::Table> Mounts() {
    std::lock_guard<std::mutex> hold(dataMutex);
    return dataActive.load() ? dataMounts : nullptr;
}
Result PrepareData(const sco::detail::StartupPublish& cap) {
    if (!options.platform.dataCore) return Result::Ok;
    const Result r = datacore::service::owned::Start({options.platform.dataRoot}, cap);
    if (r != Result::Ok) return r;
    dataActive = true;
    auto inputs = std::make_shared<DataInputs>();
    plugins::Options po;
    po.enabled = options.platform.pluginsEnabled;
    po.has = HasDataRequirement;
    inputs->list = plugins::Discover(options.platform.pluginRoot, po);
    // Requirement callbacks can invalidate admission or registry ownership.
    if (!AdmittedAndOwnsSignatureRegistry()) return Result::Unavailable;
    inputs->index.Build(inputs->list); // no plugin callbacks during early load
    dataInputs = std::move(inputs);
    vfs::Mount mount;
    mount.path = "Data/Game2.dcb"; mount.source = "host.datacore";
    mount.producer = vfs::Transform(testing::TransformDataCore);
    dataMounts = vfs::Table::Build({std::move(mount)});
    if (!dataMounts) return Result::Failed;
    if (!AdmittedAndOwnsSignatureRegistry()) return Result::Unavailable;
    if (options.dataCoreHook == DataCoreHook::Required) {
        pak::Options poPak; poPak.mounts = Mounts;
        const Result pr = pak::Enable(poPak);
        if (pr != Result::Ok) return pr;
        pakOwned = true;
    }
    return Result::Ok;
}
Result RetainTransitionFailure(Result result) noexcept {
    detail::SetFreshAdmissionEnabled(false);
    if (options.shutdownOwner) options.shutdownOwner->fault();
    RevokeFailedHostCapabilities();
    phase = Phase::Failed;
    return result;
}
}
HostCapsule::HostCapsule(const Options& o, std::shared_ptr<const void> backing)
    : backing_(std::move(backing)), options_(o), image_(o.image ? *o.image : Image{}) {
    if (o.platform.hostVersion) {
        const size_t n = strnlen(o.platform.hostVersion, 4097);
        if (n > 4096) throw std::invalid_argument("host version exceeds 4096 bytes");
        hostVersion_.assign(o.platform.hostVersion, n);
        options_.platform.hostVersion = hostVersion_.c_str();
    }
    options_.image = o.image ? &image_ : nullptr;
}

static Result PrepareImpl(const Options& o) {
    sco::detail::LegacyEntry entry;
    if (!entry) return Result::Unavailable;
    Phase expected = Phase::Idle;
    if (!phase.compare_exchange_strong(expected, Phase::Preparing)) return Result::BadArg;
    if (o.size < sizeof(Options) || !o.inspect || o.tickIntervalMs > 0x7fffffffu ||
        o.platform.image || o.platform.registerSignatures || o.platform.beforeDependentStop ||
        (o.quit != QuitPolicy::Required && o.quit != QuitPolicy::External) ||
        (o.dataCoreHook != DataCoreHook::Required && o.dataCoreHook != DataCoreHook::External) ||
        (o.platform.dataCore && (o.platform.dataRoot.empty() || o.platform.nBuiltins ||
                                !o.maxDataCoreBytes || o.maxDataCoreBytes > vfs::kMaxFileSize)))
        { phase = Phase::Idle; return Result::BadArg; }
    if (o.nativeDataCoreReads && (!o.preEngineQuiesced || !o.platform.dataCore ||
        o.dataCoreHook!=DataCoreHook::Required || !o.platform.gameServices)) { phase=Phase::Idle; return Result::BadArg; }
#if !defined(SCO_GAME_SERVICES)
    if(o.nativeDataCoreReads) {phase=Phase::Idle;return Result::Unavailable;}
#endif
    // Publish the exact legacy owner before startup transitions or hook
    // installation. An earlier actual quit notification cancels this attempt;
    // explicit no-root RequestStopDetailed remains non-queuing. This retains
    // no product module, other backing, or engine object.
    if (!retainedHost.load(std::memory_order_acquire) && o.shutdownOwner && o.quit == QuitPolicy::Required) {
#ifdef SCO_GAME_HOST_REPORT_TESTING
        PauseQuitOwner(testing::QuitOwnerPauseStage::BeforePublication);
        const bool failAllocation = quitOwnerAllocationFault.exchange(false);
#else
        constexpr bool failAllocation = false;
#endif
        const auto publication = legacyQuitOwner.Publish(o.shutdownOwner, failAllocation);
        if (publication != detail::QuitOwnerGate::Publication::Published) {
            phase = Phase::Failed;
            return publication == detail::QuitOwnerGate::Publication::Cancelled ? Result::Unavailable : Result::Failed;
        }
#ifdef SCO_GAME_HOST_REPORT_TESTING
        PauseQuitOwner(testing::QuitOwnerPauseStage::AfterPublication);
#endif
    }
    if (o.shutdownOwner && !OwnedShutdownHostAccess::begin_startup(*o.shutdownOwner)) {
        o.shutdownOwner->fault(); phase = Phase::Failed; return Result::Unavailable;
    }
    auto transition = o.shutdownOwner ? OwnedShutdownHostAccess::transition(*o.shutdownOwner) : OwnedShutdown::Transition{};
    if (o.shutdownOwner && !transition) {
        o.shutdownOwner->fault(); phase = Phase::Failed; return Result::Unavailable;
    }
    if (o.shutdownOwner && !sco::detail::ReserveEmptyHostRegistry(o.shutdownOwner, transition)) {
        o.shutdownOwner->fault(); phase = Phase::Failed; return Result::Unavailable;
    }
    if (o.shutdownOwner && !sco::detail::BindRuntimeStartup(o.shutdownOwner, transition)) {
        sco::detail::ReleaseUnusedHostRegistryReservation(o.shutdownOwner);
        o.shutdownOwner->fault(); phase = Phase::Failed; return Result::Unavailable;
    }
    try {
#ifdef SCO_GAME_HOST_REPORT_TESTING
        if (reportFault.load() == testing::ReportFault::BeforeOptionsCopy) throw std::bad_alloc();
        optionsAllocationArmed = reportFault.load() == testing::ReportFault::OptionsAllocation;
#endif
        options = o;
#ifdef SCO_GAME_HOST_REPORT_TESTING
        optionsAllocationArmed = false;
        if (reportFault.load() == testing::ReportFault::AfterOptionsCopy) throw std::bad_alloc();
#endif
    } catch (...) {
#ifdef SCO_GAME_HOST_REPORT_TESTING
        optionsAllocationArmed = false;
#endif
        if (o.shutdownOwner) { o.shutdownOwner->fault(); phase = Phase::Failed; return Result::Failed; }
        phase = Phase::Idle; return Result::Failed;
    }
    if (!Admitted()) {
        if (o.shutdownOwner) return RetainTransitionFailure(Result::Unavailable);
        options.imageOwner.reset(); options.resolverOwner.reset(); phase = Phase::Idle; return Result::Unavailable;
    }
    // Inspection is foreign code and may synchronously request shutdown.
    // This is a cancellation check, not a barrier against a later close.
    if (o.shutdownOwner && !transition.valid_for(*o.shutdownOwner))
        return RetainTransitionFailure(Result::Unavailable);
    if (!o.imageOwner || !o.resolverOwner || !o.image || !o.image->base || !o.image->size || !o.image->text.base ||
        !o.image->text.size || !o.image->rdata.base || !o.image->rdata.size) {
        if (o.shutdownOwner) return RetainTransitionFailure(Result::BadArg);
        options.imageOwner.reset(); options.resolverOwner.reset(); phase = Phase::Idle; return Result::BadArg;
    }
    try {
        auto candidate = std::make_shared<SignatureRegistry>(o.signatureRegistryBudgetBytes);
        if (o.shutdownOwner) signatureRegistry = candidate; // retain partial state on terminal failure
        if (auto* root = retainedHost.load(std::memory_order_acquire)) root->registry = candidate;
        const auto registration = RegisterGameSignatures(*candidate);
        if (registration.status != SigRegistryStatus::Ok) {
            Log("[game-host] signature registration failed: %s (table %zu, row %zu)",
                SigRegistryStatusName(registration.status), registration.table, registration.row);
            if (o.shutdownOwner) return RetainTransitionFailure(Result::Failed);
            options.imageOwner.reset(); options.resolverOwner.reset();
            phase = Phase::Failed; return Result::Failed;
        }
        auto status = candidate->Freeze();
        if (status != SigRegistryStatus::Ok) {
            Log("[game-host] signature freeze failed: %s", SigRegistryStatusName(status));
            if (o.shutdownOwner) return RetainTransitionFailure(Result::Failed);
            options.imageOwner.reset(); options.resolverOwner.reset();
            phase = Phase::Failed; return Result::Failed;
        }
        status = candidate->Resolve(*o.image, o.imageOwner, o.resolverOwner);
        if (!o.shutdownOwner) { options.imageOwner.reset(); options.resolverOwner.reset(); }
        if (status != SigRegistryStatus::Ok) {
            Log("[game-host] signature resolution failed: %s", SigRegistryStatusName(status));
            if (o.shutdownOwner) return RetainTransitionFailure(Result::Failed);
            phase = Phase::Failed; return Result::Failed;
        }
        if (!Admitted()) {
            if (o.shutdownOwner) return RetainTransitionFailure(Result::Unavailable);
            phase = Phase::Failed; return Result::Unavailable;
        }
        signatureRegistry = std::move(candidate);
        resolved = true;
        if (o.shutdownOwner) {
            if (!transition.publish(phase, Phase::Preparing, Phase::Prepared))
                return RetainTransitionFailure(Result::Unavailable);
        } else phase = Phase::Prepared;
        return Result::Ok;
    } catch (...) {
        if (o.shutdownOwner) return RetainTransitionFailure(Result::Failed);
        signatureRegistry.reset(); options.imageOwner.reset(); options.resolverOwner.reset(); resolved = false;
        phase = Phase::Failed; return Result::Failed;
    }
}
static Result ActivateImpl() {
    sco::detail::LegacyEntry entry;
    if (!entry) return Result::Unavailable;
    Phase expected = Phase::Prepared;
    if (!phase.compare_exchange_strong(expected, Phase::Activating)) return Result::BadArg;
    if (!options.shutdownOwner) { phase = Phase::Prepared; return Result::Unavailable; }
    auto transition = OwnedShutdownHostAccess::transition(*options.shutdownOwner);
    if (!transition) return RetainTransitionFailure(Result::Unavailable);
    const auto refuse = [&] {
        if (!transition.publish(phase, Phase::Activating, Phase::Prepared))
            return RetainTransitionFailure(Result::Unavailable);
        return Result::Unavailable;
    };
    if (!Admitted()) return refuse();
    // Legacy consumers expose copyable raw service tables and unowned threads.
    // No callback assertion can prove their exclusion or join: do not activate.
    const auto& pf = options.platform;
    if (!options.drainTimeoutMs || options.drainTimeoutMs > 60000 ||
        options.quit != QuitPolicy::External || options.dataCoreHook != DataCoreHook::External ||
        options.nativeDataCoreReads || pf.gameServices || pf.pluginsEnabled || pf.nBuiltins || pf.scripts ||
        pf.hostService.start || pf.hostService.tick || pf.hostService.stop ||
        pf.hostMenu.context || pf.hostMenu.load || pf.hostMenu.frame || pf.hostMenu.stop ||
        pf.setCapabilities || options.earlyDataRequirement || pf.diagnosticSources.diagnosticsPlugin ||
        pf.runtimeInspection.enabled || pf.privateSession.enabled || pf.reservedChords || pf.nReservedChords) {
        return refuse();
    }
    if ((options.quit == QuitPolicy::Required ||
         (options.platform.dataCore && options.dataCoreHook == DataCoreHook::Required) ||
         options.nativeDataCoreReads || options.platform.gameServices) && !NativeBarrierHeld()) {
        return refuse();
    }
    if (!Admitted()) return refuse();
    try {
        if (!OwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
        // No global registry, raw address or snapshot escapes the strict host.
        InvalidateBeforeHostInstall();
        if (testLogAfterRegistryInstall.load()) Log("[game-host] injected activation log");
        resolved = true;
        if (!AdmittedAndOwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
        if (options.quit == QuitPolicy::Required) {
            const Result q = InstallQuit();
            if (q != Result::Ok) return RetainTransitionFailure(q);
        }
        if (!AdmittedAndOwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
        const sco::detail::StartupPublish cap(options.shutdownOwner, transition);
        const Result d = PrepareData(cap);
        if (d != Result::Ok) return RetainTransitionFailure(d);
        if (!AdmittedAndOwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
#if defined(SCO_GAME_SERVICES)
        if (options.nativeDataCoreReads) {
            if (!services::datacore_live::PrepareProductionEngine(options.preEngineQuiesced))
                return RetainTransitionFailure(Result::Unavailable);
        }
#endif
        if (!AdmittedAndOwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
        detail::SetFreshInspector(options.inspect);
        detail::SetFreshShutdownOwner(options.shutdownOwner);
        if (!transition.publish(phase, Phase::Activating, Phase::Activated))
            return RetainTransitionFailure(Result::Unavailable);
        return Result::Ok;
    } catch (...) {
        // A throwing setup can have retained native resources. Do not tear them
        // down based on incomplete state, even if retirement succeeds.
        const auto result = RetainTransitionFailure(Result::Failed);
        try { Log("[game-host] preparation failed with an exception"); } catch (...) {}
        return result;
    }
}
static Result StartImpl() {
    sco::detail::LegacyEntry entry;
    if (!entry) return Result::Unavailable;
    if (phase.load() == Phase::Prepared) {
        const auto result = ActivateImpl();
        if (result != Result::Ok) return result;
    }
    Phase expected = Phase::Activated;
    if (!phase.compare_exchange_strong(expected, Phase::Starting)) return Result::BadArg;
    auto transition = OwnedShutdownHostAccess::transition(*options.shutdownOwner);
    if (!transition) return RetainTransitionFailure(Result::Unavailable);
    if (!OwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
    if (!Admitted()) {
        // Preserve the existing retry on admission refusal before startup work.
        if (!OwnsSignatureRegistry()) return RetainTransitionFailure(Result::Failed);
        if (!transition.publish(phase, Phase::Starting, Phase::Activated))
            return RetainTransitionFailure(Result::Unavailable);
        return Result::Unavailable;
    }
    if (!OwnsSignatureRegistry()) return RetainTransitionFailure(Result::Failed);
    detail::SetFreshAdmissionEnabled(true);
    try {
        auto platform = options.platform;
        platform.dataCore = false; // service already OPEN/LOADED; NEVER reset an early load
        platform.shutdownOwner = options.shutdownOwner;
        platform.beforeDependentStop = BeforeDependentStop;
        platform.beforeDependentStopContext = nullptr;
        platform.privateSessionAdmitted = Admitted; // exact same fail-closed admission
        if (!app::detail::StartOwned(platform, transition)) {
            // Start may have installed hooks or exposed readers before failing.
            // Keep early data and providers intact without a shutdown proof.
            return RetainTransitionFailure(Result::Failed);
        }
        if (testFailAfterAppStart.load()) throw std::runtime_error("injected post-start failure");
        if (dataActive) {
            auto inputs = std::make_shared<DataInputs>();
            if (!app::detail::CopyOwnedData(transition, options.shutdownOwner, inputs->list, inputs->index))
                return RetainTransitionFailure(Result::Unavailable);
            std::lock_guard<std::mutex> hold(dataMutex);
            dataInputs = std::move(inputs);
        }
        if (!transition.publish_and_open(phase, Phase::Starting, Phase::Running)) {
            app::detail::RetainFailedOwnedStartup(options.shutdownOwner);
            return RetainTransitionFailure(Result::Unavailable);
        }
        return Result::Ok;
    } catch (...) {
        app::detail::RetainFailedOwnedStartup(options.shutdownOwner);
        return RetainTransitionFailure(Result::Failed);
    }
}
static Result StopImpl();
static Result TickImpl(uint32_t nowMs) {
    sco::detail::LegacyEntry entry;
    if (!entry) return Result::Unavailable;
    if (phase.load() != Phase::Running) return Result::Unavailable;
    if (!OnGameThread()) return Result::WrongThread;
    if (sco::detail::InCallout() || ticking) return Result::Unavailable;
    {
        struct TickScope { TickScope() { ticking = true; } ~TickScope() { ticking = false; } } scope;
        auto read = options.shutdownOwner->try_read();
        if (!read) return RetainTransitionFailure(Result::Unavailable);
        if (Admitted()) {
            if (ticked && nowMs - lastTick < options.tickIntervalMs) return Result::Ok;
            ticked = true; lastTick = nowMs;
            app::Tick(nowMs);
            if (!options.shutdownOwner->open()) {
                app::detail::RetainFailedOwnedStartup(options.shutdownOwner);
                return RetainTransitionFailure(Result::Unavailable);
            }
            return Result::Ok;
        }
    } // Do not wait for our own lease in Stop.
    StopImpl();
    return Result::Unavailable;
}
static Result StopImpl() {
    sco::detail::LegacyEntry entry;
    if (!entry) return Result::Unavailable;
    const Phase p = phase.load();
    if (p == Phase::Idle || p == Phase::Stopped) return Result::Ok;
    if (!OnGameThread()) return Result::WrongThread;
    if (p == Phase::Stopping) return Result::Unavailable;
    if (p != Phase::Running) return Result::Unavailable;
    if (sco::detail::InCallout() || ticking) return Result::Unavailable;
    Phase expected = Phase::Running;
    if (!phase.compare_exchange_strong(expected, Phase::Stopping)) return Result::Unavailable;
    detail::SetFreshAdmissionEnabled(false); // retained workers must pass through before teardown
    const bool drained = BeforeDependentStop(nullptr);
    // Existing cleanup includes arbitrary callouts and potentially failing
    // destruction. Even after a successful drain it cannot commit atomically.
    // Close app admission and retain the full dependency closure on both paths.
    app::detail::RetainFailedOwnedStartup(options.shutdownOwner);
    const auto result = RetainTransitionFailure(Result::Failed);
    try { Log("[game-host] owned shutdown retained (%s)", drained ? "drained" : "not drained"); } catch (...) {}
    return result;
}
HostReport PrepareDetailed(const std::shared_ptr<const HostCapsule>& capsule) noexcept {
    ControlEntry control;
    if (!control.entered) return Report(Result::Unavailable, true);
    if (retainedHost.load()) return Report(Result::BadArg);
    if (legacyControlUsed.load()) return Report(Result::Unavailable);
    if (!capsule || !capsule->hasBacking() || !DetailedConfiguration(capsule->configuration()))
        return Report(Result::BadArg);
    try {
#ifdef SCO_GAME_HOST_REPORT_TESTING
        if (reportFault.load() == testing::ReportFault::BeforeRootAllocation) throw std::bad_alloc();
#endif
        auto* root = new RetainedHost{capsule, {}};
        // Coherent publication of actual ownership, identity and retained floor.
        // No allocation, callback, destructor or legacy operation follows before
        // this store; subsequent failures cannot retract the process root.
        retainedHost.store(root, std::memory_order_release);
        const auto result = PrepareImpl(root->capsule->configuration());
        if (result != Result::Ok) {
            root->capsule->configuration().shutdownOwner->fault();
            phase = Phase::Failed;
        }
        return Report(result);
    } catch (...) {
        if (const auto* root = retainedHost.load()) {
            try { root->capsule->configuration().shutdownOwner->fault(); } catch (...) {}
            phase = Phase::Failed;
        }
        return Report(Result::Failed);
    }
}
HostReport ActivateDetailed() noexcept {
    ControlEntry control;
    if (!control.entered) return Report(Result::Unavailable, true);
    if (!retainedHost.load()) return Report(Result::Unavailable);
    try { return Report(ActivateImpl()); } catch (...) { return Report(RetainTransitionFailure(Result::Failed)); }
}
HostReport StartDetailed() noexcept {
    ControlEntry control;
    if (!control.entered) return Report(Result::Unavailable, true);
    if (!retainedHost.load()) return Report(Result::Unavailable);
    try { return Report(StartImpl()); } catch (...) { return Report(RetainTransitionFailure(Result::Failed)); }
}
HostReport StopDetailed() noexcept {
    ControlEntry control;
    if (!control.entered) return Report(Result::Unavailable, true);
    try { return Report(StopImpl()); } catch (...) {
        if (retainedHost.load()) return Report(RetainTransitionFailure(Result::Failed));
        return Report(Result::Failed);
    }
}
HostReport RequestStopDetailed() noexcept {
    // The immutable capsule was rooted before publication and is never released.
    // Use that root, not mutable Options or the control gate: the requester may
    // itself be inside the inspector or an admitted callback on that gate.
    const auto* root = retainedHost.load(std::memory_order_acquire);
    if (!root) return Report(Result::Unavailable);
    Result result = Result::Ok;
    try { root->capsule->configuration().shutdownOwner->request_stop(); }
    catch (...) { result = Result::Failed; }
    // Closing is not app retirement. Do not change phase, set ready, release the
    // root, call foreign code, or imply that an exceptional close took effect.
    // Use the common snapshot so the returned drain field reflects the actual
    // owner state after this request (or Unknown if observation itself fails).
    return Report(result);
}
HostReport StatusDetailed() noexcept {
    // Classify the gate observation before collecting the scalar snapshot.
    // A busy observation remains busy even if Prepare finishes before return.
    // Overlapping calls can still return earlier observations after newer ones.
    const bool busy = hostControl.test(std::memory_order_acquire);
    auto report = Report(busy ? Result::Unavailable : Result::Ok);
#ifdef SCO_GAME_HOST_REPORT_TESTING
    if (auto* pause = statusPause.exchange(nullptr, std::memory_order_acq_rel)) {
        pause->reached.store(true, std::memory_order_release);
        while (!pause->release.load(std::memory_order_acquire)) std::this_thread::yield();
    }
#endif
    if (busy) {
        // Observation is pending, not a completed control/exposure decision.
        // It must not latch the legacy floor and poison a private failed prepare.
        report.dependencies = DependencyDisposition::ProcessRetained;
    }
    return report;
}
Result Prepare(const Options& o) {
    ControlEntry control;
    if (!control.entered || retainedHost.load()) return Result::BadArg;
    legacyControlUsed.store(true, std::memory_order_release);
    return PrepareImpl(o);
}
Result Activate() {
    ControlEntry control;
    if (!control.entered) return Result::BadArg;
    if (!retainedHost.load()) legacyControlUsed.store(true, std::memory_order_release);
    return ActivateImpl();
}
Result Start() {
    ControlEntry control;
    if (!control.entered) return Result::BadArg;
    if (!retainedHost.load()) legacyControlUsed.store(true, std::memory_order_release);
    return StartImpl();
}
Result Tick(uint32_t nowMs) {
    ControlEntry control;
    if (!control.entered) return Result::Unavailable;
    if (!retainedHost.load()) legacyControlUsed.store(true, std::memory_order_release);
    return TickImpl(nowMs);
}
Result Stop() {
    ControlEntry control;
    if (!control.entered) return Result::Unavailable;
    if (!retainedHost.load()) legacyControlUsed.store(true, std::memory_order_release);
    return StopImpl();
}
Result BeforeQuit() { return Stop(); }
namespace testing {
#ifdef SCO_GAME_HOST_REPORT_TESTING
void SetReportFault(ReportFault fault) noexcept { reportFault.store(fault); }
bool ConsumeOptionsAllocationFault() noexcept { return std::exchange(optionsAllocationArmed, false); }
void PauseNextStatus(StatusPause* pause) noexcept { statusPause.store(pause, std::memory_order_release); }
void PauseNextQuitOwner(QuitOwnerPause* pause) noexcept { quitOwnerPause.store(pause, std::memory_order_release); }
void FailNextQuitOwnerAllocation() noexcept { quitOwnerAllocationFault.store(true); }
void FailNextQuitOwnerNotification() noexcept { quitOwnerNotificationFault.store(true); }
void SetQuitOriginalForTest(QuitFn original) noexcept { quitOriginal = original; }
void InvokeQuitDetourForTest(void* system, void* info, int code) { QuitDetour(system, info, code); }
bool RetainedRegistrySurvivesStaticRelease() {
    std::weak_ptr<SignatureRegistry> weak = signatureRegistry;
    signatureRegistry.reset();
    return !weak.expired();
}
#endif
void FailAfterAppStart(bool enabled) { testFailAfterAppStart.store(enabled); }
void LogAfterRegistryInstall(bool enabled) { testLogAfterRegistryInstall.store(enabled); }
bool HasPrivateRegistry() { return static_cast<bool>(signatureRegistry); }
void ForwardQuit(QuitFn original, void* system, void* info, int code) {
    const Result r = BeforeQuit();
    if (r != Result::Ok) Log("[game-host] quit teardown refused: %s", ResultName(r));
    if (original) original(system, info, code);
}
vfs::Result TransformDataCore(vfs::BaseIo& base, uint64_t size, std::vector<vfs::Splice>& out) {
    if (phase.load() != Phase::Running || !options.shutdownOwner)
        return {"offline DataCore admission unavailable"};
    auto read = options.shutdownOwner->try_read();
    if (!read) return {"offline DataCore admission unavailable"};
    if (dataTransformBusy.test_and_set()) return {"offline DataCore transform busy"};
    struct ReleaseBusy { ~ReleaseBusy() { dataTransformBusy.clear(); } } releaseBusy;
    std::shared_ptr<const DataInputs> inputs;
    {
        std::lock_guard<std::mutex> hold(dataMutex);
        inputs = dataInputs;
    }
    const Phase p = phase.load();
    if (!dataActive || !inputs || p != Phase::Running || !Admitted())
        return {"offline DataCore admission unavailable"};
    if (!size || size > options.maxDataCoreBytes || size > std::numeric_limits<size_t>::max())
        return {"DataCore base exceeds host byte limit"};
    try {
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!base.Seek(0)) return {"DataCore base seek failed"};
        if (options.shutdownOwner->closed()) return {"offline DataCore admission unavailable"};
        size_t got = 0;
        while (got < bytes.size()) {
            const size_t n = base.Read(bytes.data() + got, bytes.size() - got);
            if (options.shutdownOwner->closed()) return {"offline DataCore admission unavailable"};
            if (!n || n > bytes.size() - got) return {"DataCore base read failed"};
            got += n;
        }
        datacore::Schema schema;
        if (!schema.Parse(bytes)) return {"DataCore schema refused: " + schema.error};
        if (options.shutdownOwner->closed()) return {"offline DataCore admission unavailable"};
        const auto loaded = datacore::service::Load(schema, inputs->list, inputs->index,
                                                    options.platform.dataRoot);
        if (!loaded.result.status) return {loaded.result.status.message};
        if (options.shutdownOwner->closed()) return {"offline DataCore admission unavailable"};
        out = loaded.result.splices;
        return {};
    } catch (...) { return {"DataCore transform failed"}; }
}
}
Snapshot Status() { return {phase.load(), resolved.load(), quitInstalled.load(), dataActive.load()}; }

#if defined(_WIN32)
Check InspectWindowsFirewallRule(const wchar_t* ruleName, const wchar_t* imagePath) noexcept {
    if (!ruleName || !ruleName[0]) return Check::Unknown;
    // INetFwPolicy2 query requires COM initialization
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needUninit = SUCCEEDED(hr);

    INetFwPolicy2* pPolicy = nullptr;
    hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                            __uuidof(INetFwPolicy2), reinterpret_cast<void**>(&pPolicy));
    if (FAILED(hr) || !pPolicy) {
        if (needUninit) CoUninitialize();
        return Check::Unknown;
    }

    INetFwRules* pRules = nullptr;
    hr = pPolicy->get_Rules(&pRules);
    if (FAILED(hr) || !pRules) {
        pPolicy->Release();
        if (needUninit) CoUninitialize();
        return Check::Unknown;
    }

    BSTR bstrName = SysAllocString(ruleName);
    INetFwRule* pRule = nullptr;
    hr = pRules->Item(bstrName, &pRule);
    SysFreeString(bstrName);

    Check result = Check::Unknown;
    if (SUCCEEDED(hr) && pRule) {
        VARIANT_BOOL vbEnabled = VARIANT_FALSE;
        NET_FW_ACTION action = NET_FW_ACTION_ALLOW;
        NET_FW_RULE_DIRECTION dir = NET_FW_RULE_DIR_IN;

        if (SUCCEEDED(pRule->get_Enabled(&vbEnabled)) &&
            SUCCEEDED(pRule->get_Action(&action)) &&
            SUCCEEDED(pRule->get_Direction(&dir))) {
            if (vbEnabled == VARIANT_TRUE && action == NET_FW_ACTION_BLOCK && dir == NET_FW_RULE_DIR_OUT) {
                // If an image path was specified, confirm it matches the rule's application
                if (imagePath && imagePath[0]) {
                    BSTR bstrApp = nullptr;
                    if (SUCCEEDED(pRule->get_ApplicationName(&bstrApp)) && bstrApp) {
                        if (_wcsicmp(bstrApp, imagePath) == 0) {
                            result = Check::Clear;
                        } else {
                            result = Check::Present; // Rule exists for different image
                        }
                        SysFreeString(bstrApp);
                    } else {
                        result = Check::Clear; // Rule without image restriction blocks all
                    }
                } else {
                    result = Check::Clear;
                }
            } else {
                result = Check::Present; // Rule exists but is not an active outbound block
            }
        }
        pRule->Release();
    } else {
        result = Check::Unknown; // Rule not found
    }

    pRules->Release();
    pPolicy->Release();
    if (needUninit) CoUninitialize();
    return result;
}
#endif

} // namespace sco::game::host
