#include "sco/game/host.h"
#include "../api/internal.h"
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
testing::QuitFn quitOriginal = nullptr;
Options options;
bool ticked = false, ticking = false, pakOwned = false;
uint32_t lastTick = 0;
std::mutex dataMutex;
struct DataInputs { std::vector<plugins::Plugin> list; plugins::ContentIndex index; };
std::shared_ptr<const DataInputs> dataInputs;
std::shared_ptr<const vfs::Table> dataMounts;
std::shared_ptr<SignatureRegistry> signatureRegistry;

void InvalidateBeforeHostInstall() noexcept {
    caps::InvalidateAll("game host preparing");
}
void InvalidateHostCapabilities() noexcept {
    caps::InvalidateAll("game host stopped");
}
void InvalidateFailedHostCapabilities() noexcept {
    caps::InvalidateAll("game host failed");
}

void RevokeFailedHostCapabilities() noexcept {
    if (signatureRegistry) WithHostSignatureRegistry(signatureRegistry, InvalidateFailedHostCapabilities);
}

void ReleaseSignatureRegistry() noexcept {
    if (signatureRegistry) ClearHostSignatureRegistry(signatureRegistry, InvalidateHostCapabilities);
    signatureRegistry.reset();
    resolved = false;
}

// A failed prepare can discard the registry only if no host-owned hook remains.
void ReleaseAfterSafePrepareFailure() noexcept {
    if (!quitInstalled.load() && !pakOwned) ReleaseSignatureRegistry();
    else RevokeFailedHostCapabilities();
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
void StopData() {
    if (pakOwned) { pak::Disable(); if(pak::Enabled()) return; pakOwned=false; }
    if (!dataActive.exchange(false)) return;
    // A producer already inside service::Load must finish before service withdrawal.
    // It only posts game-thread work, never waits for that work while holding this lock.
    std::lock_guard<std::mutex> hold(dataMutex);
    datacore::service::Stop();
    dataInputs.reset(); dataMounts.reset();
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
    if (!AdmittedAndOwnsSignatureRegistry()) { StopData(); return Result::Unavailable; }
    inputs->index.Build(inputs->list); // no plugin callbacks during early load
    dataInputs = std::move(inputs);
    vfs::Mount mount;
    mount.path = "Data/Game2.dcb"; mount.source = "host.datacore";
    mount.producer = vfs::Transform(testing::TransformDataCore);
    dataMounts = vfs::Table::Build({std::move(mount)});
    if (!dataMounts) { StopData(); return Result::Failed; }
    if (!AdmittedAndOwnsSignatureRegistry()) { StopData(); return Result::Unavailable; }
    if (options.dataCoreHook == DataCoreHook::Required) {
        pak::Options poPak; poPak.mounts = Mounts;
        const Result pr = pak::Enable(poPak);
        if (pr != Result::Ok) { StopData(); return pr; }
        pakOwned = true;
    }
    return Result::Ok;
}
Result FailPreparedAdmission(bool nativeAttempted) {
    StopData();
    // Native lifecycle setup may have installed hooks even if it later failed.
    if (nativeAttempted) RevokeFailedHostCapabilities();
    else ReleaseAfterSafePrepareFailure();
    phase = Phase::Failed;
    return Result::Unavailable;
}
}
Result Prepare(const Options& o) {
    if (phase.load() != Phase::Idle) return Result::BadArg;
    if (o.size < sizeof(Options) || !o.inspect || o.tickIntervalMs > 0x7fffffffu ||
        o.platform.image || o.platform.registerSignatures ||
        (o.quit != QuitPolicy::Required && o.quit != QuitPolicy::External) ||
        (o.dataCoreHook != DataCoreHook::Required && o.dataCoreHook != DataCoreHook::External) ||
        (o.platform.dataCore && (o.platform.dataRoot.empty() || o.platform.nBuiltins ||
                                !o.maxDataCoreBytes || o.maxDataCoreBytes > vfs::kMaxFileSize)))
        return Result::BadArg;
    phase = Phase::Preparing; // also prevents admission callback reentry
    if (o.nativeDataCoreReads && (!o.preEngineQuiesced || !o.platform.dataCore ||
        o.dataCoreHook!=DataCoreHook::Required || !o.platform.gameServices)) { phase=Phase::Idle; return Result::BadArg; }
#if !defined(SCO_GAME_SERVICES)
    if(o.nativeDataCoreReads) {phase=Phase::Idle;return Result::Unavailable;}
#endif
    try { options = o; } catch (...) { phase = Phase::Idle; return Result::Failed; }
    if (!Admitted()) { phase = Phase::Idle; return Result::Unavailable; }
    if (!o.image || !o.image->base || !o.image->size || !o.image->text.base ||
        !o.image->text.size || !o.image->rdata.base || !o.image->rdata.size) {
        phase = Phase::Idle; return Result::BadArg;
    }
    bool dataAttempted = false;
    try {
        auto candidate = std::make_shared<SignatureRegistry>(o.signatureRegistryBudgetBytes);
        const auto registration = RegisterGameSignatures(*candidate);
        if (registration.status != SigRegistryStatus::Ok) {
            Log("[game-host] signature registration failed: %s (table %zu, row %zu)",
                SigRegistryStatusName(registration.status), registration.table, registration.row);
            phase = Phase::Failed; return Result::Failed;
        }
        auto status = candidate->Freeze();
        if (status != SigRegistryStatus::Ok) {
            Log("[game-host] signature freeze failed: %s", SigRegistryStatusName(status));
            phase = Phase::Failed; return Result::Failed;
        }
        status = candidate->Resolve(*o.image);
        if (status != SigRegistryStatus::Ok) {
            Log("[game-host] signature resolution failed: %s", SigRegistryStatusName(status));
            phase = Phase::Failed; return Result::Failed;
        }
        if (!Admitted()) { phase = Phase::Failed; return Result::Unavailable; }
        if (!InstallHostSignatureRegistry(candidate, InvalidateBeforeHostInstall)) {
            Log("[game-host] signature registry already owned");
            phase = Phase::Failed; return Result::Failed;
        }
        signatureRegistry = std::move(candidate);
        resolved = true;
        if (!AdmittedAndOwnsSignatureRegistry()) return FailPreparedAdmission(false);
        if (options.quit == QuitPolicy::Required) {
            const Result q = InstallQuit();
            if (q != Result::Ok) { ReleaseAfterSafePrepareFailure(); phase = Phase::Failed; return q; }
        }
        if (!AdmittedAndOwnsSignatureRegistry()) return FailPreparedAdmission(false);
        dataAttempted = true;
        const Result d = PrepareData();
        if (d != Result::Ok) { StopData(); ReleaseAfterSafePrepareFailure(); phase = Phase::Failed; return d; }
        if (!AdmittedAndOwnsSignatureRegistry()) return FailPreparedAdmission(false);
        bool nativeAttempted = false;
#if defined(SCO_GAME_SERVICES)
        if (options.nativeDataCoreReads) {
            nativeAttempted = true;
            if (!services::datacore_live::PrepareProductionEngine(options.preEngineQuiesced))
                return FailPreparedAdmission(true);
        }
#endif
        if (!AdmittedAndOwnsSignatureRegistry()) return FailPreparedAdmission(nativeAttempted);
        detail::SetFreshInspector(options.inspect);
        phase = Phase::Prepared;
        return Result::Ok;
    } catch (...) {
        StopData();
        // A throwing data/hook setup can have retained native resources.
        if (!dataAttempted) ReleaseAfterSafePrepareFailure();
        else RevokeFailedHostCapabilities();
        Log("[game-host] preparation failed with an exception");
        phase = Phase::Failed; return Result::Failed;
    }
}
Result Start() {
    if (phase.load() != Phase::Prepared) return Result::BadArg;
    phase = Phase::Starting;
    if (!OwnsSignatureRegistry()) return FailPreparedAdmission(options.nativeDataCoreReads);
    if (!Admitted()) {
        // Preserve the existing retry on admission refusal before startup work.
        if (!OwnsSignatureRegistry()) return FailPreparedAdmission(options.nativeDataCoreReads);
        phase = Phase::Prepared; return Result::Unavailable;
    }
    if (!OwnsSignatureRegistry()) return FailPreparedAdmission(options.nativeDataCoreReads);
    detail::SetFreshAdmissionEnabled(true);
    try {
        auto platform = options.platform;
        platform.dataCore = false; // service already OPEN/LOADED; NEVER reset an early load
        platform.privateSessionAdmitted = Admitted; // exact same fail-closed admission
        if (!app::Start(platform)) { detail::SetFreshAdmissionEnabled(false); StopData(); RevokeFailedHostCapabilities(); phase = Phase::Failed; return Result::Failed; }
        if (dataActive) {
            auto inputs = std::make_shared<DataInputs>();
            inputs->list = app::Plugins(); inputs->index = app::Content();
            std::lock_guard<std::mutex> hold(dataMutex);
            dataInputs = std::move(inputs);
        }
        phase = Phase::Running;
        return Result::Ok;
    } catch (...) { detail::SetFreshAdmissionEnabled(false); StopData(); RevokeFailedHostCapabilities(); phase = Phase::Failed; return Result::Failed; }
}
Result Tick(uint32_t nowMs) {
    if (phase.load() != Phase::Running) return Result::Unavailable;
    if (!OnGameThread()) return Result::WrongThread;
    if (sco::detail::InCallout() || ticking) return Result::Unavailable;
    struct TickScope { TickScope() { ticking = true; } ~TickScope() { ticking = false; } } scope;
    if (!Admitted()) { Stop(); return Result::Unavailable; }
    if (ticked && nowMs - lastTick < options.tickIntervalMs) return Result::Ok;
    ticked = true; lastTick = nowMs;
    app::Tick(nowMs);
    return Result::Ok;
}
Result Stop() {
    const Phase p = phase.load();
    if (p == Phase::Idle || p == Phase::Stopped) return Result::Ok;
    if (!OnGameThread()) return Result::WrongThread;
    if (p == Phase::Stopping) return Result::Ok;
    if (p != Phase::Running) return Result::Unavailable;
    if (sco::detail::InCallout()) return Result::Unavailable;
    const bool cleanupAdmitted = FreshAdmissionAllowed();
    detail::SetFreshAdmissionEnabled(false); // retained workers must pass through before teardown
    phase = Phase::Stopping;
    struct CvarCleanupScope {
        explicit CvarCleanupScope(bool admitted) { detail::SetCvarCleanupEnabled(admitted); }
        ~CvarCleanupScope() { detail::SetCvarCleanupEnabled(false); }
    } cleanup(cleanupAdmitted);
    // Stop transport/producer access first, but keep DataCore tables available for unload.
    if (pakOwned) {
        pak::Disable();
        if(pak::Enabled()) {phase=Phase::Running;return Result::Failed;}
        pakOwned=false;
    }
    { std::lock_guard<std::mutex> hold(dataMutex); dataInputs.reset(); dataMounts.reset(); }
    // A refusal or failed provider teardown may leave consumers alive. Retain the
    // registry and fail the host instead of dropping their published snapshot.
    Result stopped = Result::Failed;
    try { stopped = app::Stop(); } // service withdrawal AFTER plugin unload
    catch (...) { Log("[game-host] app stop failed with an exception"); }
    if (stopped != Result::Ok) { RevokeFailedHostCapabilities(); phase = Phase::Failed; return stopped; }
    dataActive = false;
    ReleaseSignatureRegistry();
    phase = Phase::Stopped;
    return Result::Ok;
}
Result BeforeQuit() { return Stop(); }
namespace testing {
void ForwardQuit(QuitFn original, void* system, void* info, int code) {
    const Result r = BeforeQuit();
    if (r != Result::Ok) Log("[game-host] quit teardown refused: %s", ResultName(r));
    if (original) original(system, info, code);
}
vfs::Result TransformDataCore(vfs::BaseIo& base, uint64_t size, std::vector<vfs::Splice>& out) {
    std::lock_guard<std::mutex> hold(dataMutex);
    const Phase p = phase.load();
    if (!dataActive || !dataInputs ||
        (p != Phase::Prepared && p != Phase::Starting && p != Phase::Running) || !Admitted())
        return {"offline DataCore admission unavailable"};
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
        const auto loaded = datacore::service::Load(schema, dataInputs->list, dataInputs->index,
                                                    options.platform.dataRoot);
        if (!loaded.result.status) return {loaded.result.status.message};
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
