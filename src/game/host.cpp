#include "sco/game/host.h"
#include "../api/internal.h"
#include "../app/owned_start.h"
#include "../owned_shutdown_access.h"
#include "sco/game/signatures.h"
#include "sco/signatures.h"
#include "sco/log.h"
#include "sco/game/system.h"
#include "sco/game/pak.h"
#include "sco/hook.h"
#include "sco/caps.h"
#include "sco/datacore_service.h"
#include "host_testing.h"
#include "admission.h"
#if defined(SCO_GAME_SERVICES)
#include "services/datacore_live_engine.h"
#endif
#include <atomic>
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
RetirementState consumerRetirement = RetirementState::NotAttempted;

void InvalidateBeforeHostInstall() noexcept {
    caps::InvalidateAll("game host preparing");
}
void InvalidateFailedHostCapabilities() noexcept {
    caps::InvalidateAll("game host failed");
}

void RevokeFailedHostCapabilities() noexcept {
    if (signatureRegistry) WithHostSignatureRegistry(signatureRegistry, InvalidateFailedHostCapabilities);
}

// Legacy retirement assertion retained for source migration only; it is not
// accepted as drain evidence and is never called by the owned supported path.
bool BeforeDependentStop(void*) noexcept {
    detail::SetFreshAdmissionEnabled(false);
    // The owned path does not invoke an unbounded legacy retirement callback.
    if (!options.shutdownOwner) return false;
    if (consumerRetirement == RetirementState::Quiesced) return options.shutdownOwner->ready();
    if (consumerRetirement != RetirementState::NotAttempted) return false;
    consumerRetirement = RetirementState::InProgress;
    ConsumerRetirement answer = ConsumerRetirement::Retained;
    try {
        if (options.shutdownOwner->drain_until(std::chrono::steady_clock::now() +
                std::chrono::milliseconds(options.drainTimeoutMs))) answer = ConsumerRetirement::Quiesced;
    } catch (...) { options.shutdownOwner->fault(); }
    consumerRetirement = answer == ConsumerRetirement::Quiesced
        ? RetirementState::Quiesced : RetirementState::Retained;
    if (consumerRetirement != RetirementState::Quiesced) RevokeFailedHostCapabilities();
    return consumerRetirement == RetirementState::Quiesced;
}


bool Admitted() noexcept {
    try {
        const Admission a = options.inspect();
        return a.session == Session::Offline && a.antiCheat == Check::Clear &&
               a.officialNetwork == Check::Clear && a.targetProcess == Check::Clear;
    } catch (...) { return false; }
}
bool OwnsSignatureRegistry() noexcept {
    return signatureRegistry && AcquireHostSignatureRegistry() == signatureRegistry;
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
    testing::ForwardQuit(quitOriginal, system, info, code);
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
Result PrepareData() {
    if (!options.platform.dataCore) return Result::Ok;
    const Result r = datacore::service::Start({options.platform.dataRoot});
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
Result Prepare(const Options& o) {
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
    if (o.shutdownOwner && !OwnedShutdownHostAccess::begin_startup(*o.shutdownOwner)) {
        o.shutdownOwner->fault(); phase = Phase::Failed; return Result::Unavailable;
    }
    auto transition = o.shutdownOwner ? OwnedShutdownHostAccess::transition(*o.shutdownOwner) : OwnedShutdown::Transition{};
    if (o.shutdownOwner && !transition) {
        o.shutdownOwner->fault(); phase = Phase::Failed; return Result::Unavailable;
    }
    try { options = o; } catch (...) {
        if (o.shutdownOwner) { o.shutdownOwner->fault(); phase = Phase::Failed; return Result::Failed; }
        phase = Phase::Idle; return Result::Failed;
    }
    if (!Admitted()) {
        if (o.shutdownOwner) return RetainTransitionFailure(Result::Unavailable);
        options.imageOwner.reset(); options.resolverOwner.reset(); phase = Phase::Idle; return Result::Unavailable;
    }
    if (!o.imageOwner || !o.resolverOwner || !o.image || !o.image->base || !o.image->size || !o.image->text.base ||
        !o.image->text.size || !o.image->rdata.base || !o.image->rdata.size) {
        if (o.shutdownOwner) return RetainTransitionFailure(Result::BadArg);
        options.imageOwner.reset(); options.resolverOwner.reset(); phase = Phase::Idle; return Result::BadArg;
    }
    try {
        auto candidate = std::make_shared<SignatureRegistry>(o.signatureRegistryBudgetBytes);
        if (o.shutdownOwner) signatureRegistry = candidate; // retain partial state on terminal failure
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
Result Activate() {
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
        pf.setCapabilities || options.earlyDataRequirement) {
        return refuse();
    }
    if ((options.quit == QuitPolicy::Required ||
         (options.platform.dataCore && options.dataCoreHook == DataCoreHook::Required) ||
         options.nativeDataCoreReads || options.platform.gameServices) && !NativeBarrierHeld()) {
        return refuse();
    }
    if (!Admitted()) return refuse();
    try {
        if (!InstallHostSignatureRegistry(signatureRegistry, InvalidateBeforeHostInstall)) {
            return RetainTransitionFailure(Result::Failed);
        }
        if (testLogAfterRegistryInstall.load()) Log("[game-host] injected activation log");
        if (!PinHostSignatureRegistry(signatureRegistry)) {
            Log("[game-host] could not pin signature registry");
            return RetainTransitionFailure(Result::Failed);
        }
        resolved = true;
        if (!AdmittedAndOwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
        if (options.quit == QuitPolicy::Required) {
            const Result q = InstallQuit();
            if (q != Result::Ok) return RetainTransitionFailure(q);
        }
        if (!AdmittedAndOwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
        const Result d = PrepareData();
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
Result Start() {
    if (phase.load() == Phase::Prepared) {
        const auto result = Activate();
        if (result != Result::Ok) return result;
    }
    Phase expected = Phase::Activated;
    if (!phase.compare_exchange_strong(expected, Phase::Starting)) return Result::BadArg;
    auto transition = OwnedShutdownHostAccess::transition(*options.shutdownOwner);
    if (!transition) return RetainTransitionFailure(Result::Unavailable);
    if (!OwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
    if (!Admitted()) {
        // Preserve the existing retry on admission refusal before startup work.
        if (!OwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
        if (!transition.publish(phase, Phase::Starting, Phase::Activated))
            return RetainTransitionFailure(Result::Unavailable);
        return Result::Unavailable;
    }
    if (!OwnsSignatureRegistry()) return RetainTransitionFailure(Result::Unavailable);
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
            inputs->list = app::Plugins(); inputs->index = app::Content();
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
Result Tick(uint32_t nowMs) {
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
    Stop();
    return Result::Unavailable;
}
Result Stop() {
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
Result BeforeQuit() { return Stop(); }
namespace testing {
void FailAfterAppStart(bool enabled) { testFailAfterAppStart.store(enabled); }
void LogAfterRegistryInstall(bool enabled) { testLogAfterRegistryInstall.store(enabled); }
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
