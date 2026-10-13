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
#include <atomic>
#include <mutex>
#include <limits>

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

bool Admitted() noexcept {
    try {
        const Admission a = options.inspect();
        return a.session == Session::Offline && a.antiCheat == Check::Clear &&
               a.officialNetwork == Check::Clear && a.targetProcess == Check::Clear;
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
int HasDataRequirement(const char* name) { return caps::Has(name) || ServiceExists(name); }
std::shared_ptr<const vfs::Table> Mounts() {
    std::lock_guard<std::mutex> hold(dataMutex);
    return dataActive.load() ? dataMounts : nullptr;
}
void StopData() {
    if (!dataActive.exchange(false)) return;
    if (pakOwned) { pak::Disable(); pakOwned = false; }
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
    inputs->index.Build(inputs->list); // no plugin callbacks during early load
    dataInputs = std::move(inputs);
    vfs::Mount mount;
    mount.path = "Data/Game2.dcb"; mount.source = "host.datacore";
    mount.producer = vfs::Transform(testing::TransformDataCore);
    dataMounts = vfs::Table::Build({std::move(mount)});
    if (!dataMounts) { StopData(); return Result::Failed; }
    if (options.dataCoreHook == DataCoreHook::Required) {
        pak::Options poPak; poPak.mounts = Mounts;
        const Result pr = pak::Enable(poPak);
        if (pr != Result::Ok) { StopData(); return pr; }
        pakOwned = true;
    }
    return Result::Ok;
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
    try { options = o; } catch (...) { phase = Phase::Idle; return Result::Failed; }
    if (!Admitted()) { phase = Phase::Idle; return Result::Unavailable; }
    if (!o.image || !o.image->base || !o.image->size || !o.image->text.base ||
        !o.image->text.size || !o.image->rdata.base || !o.image->rdata.size) {
        phase = Phase::Idle; return Result::BadArg;
    }
    try {
        if (!RegisterGameSignatures()) { phase = Phase::Failed; return Result::Failed; }
        ResolveAll(*o.image);
        resolved = true;
        if (options.quit == QuitPolicy::Required) {
            const Result q = InstallQuit();
            if (q != Result::Ok) { phase = Phase::Failed; return q; }
        }
        const Result d = PrepareData();
        if (d != Result::Ok) { phase = Phase::Failed; return d; }
        phase = Phase::Prepared;
        return Result::Ok;
    } catch (...) { StopData(); phase = Phase::Failed; return Result::Failed; }
}
Result Start() {
    if (phase.load() != Phase::Prepared) return Result::BadArg;
    phase = Phase::Starting;
    if (!Admitted()) { phase = Phase::Prepared; return Result::Unavailable; }
    try {
        auto platform = options.platform;
        platform.dataCore = false; // service already OPEN/LOADED; NEVER reset an early load
        platform.privateSessionAdmitted = Admitted; // exact same fail-closed admission
        if (!app::Start(platform)) { StopData(); phase = Phase::Failed; return Result::Failed; }
        if (dataActive) {
            auto inputs = std::make_shared<DataInputs>();
            inputs->list = app::Plugins(); inputs->index = app::Content();
            std::lock_guard<std::mutex> hold(dataMutex);
            dataInputs = std::move(inputs);
        }
        phase = Phase::Running;
        return Result::Ok;
    } catch (...) { StopData(); phase = Phase::Failed; return Result::Failed; }
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
    phase = Phase::Stopping;
    // Stop transport/producer access first, but keep DataCore tables available for unload.
    if (pakOwned) { pak::Disable(); pakOwned = false; }
    { std::lock_guard<std::mutex> hold(dataMutex); dataInputs.reset(); dataMounts.reset(); }
    app::Stop(); // service withdrawal AFTER plugin unload, as in app's lifecycle
    dataActive = false;
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
} // namespace sco::game::host
