#include "sco/game/host.h"
#include "../api/internal.h"
#include "sco/game/signatures.h"
#include "sco/signatures.h"
#include "sco/log.h"
#include <atomic>

namespace sco::game::host {
namespace {
std::atomic<Phase> phase{Phase::Idle};
std::atomic<bool> resolved{false};
Options options;
bool Admitted() noexcept {
    try {
        const Admission a = options.inspect();
        return a.session == Session::Offline && a.antiCheat == Check::Clear &&
               a.officialNetwork == Check::Clear && a.targetProcess == Check::Clear;
    } catch (...) { return false; }
}
}
Result Prepare(const Options& o) {
    if (phase.load() != Phase::Idle) return Result::BadArg;
    if (o.size < sizeof(Options) || !o.inspect || o.platform.image || o.platform.registerSignatures)
        return Result::BadArg;
    options = o;
    // Admission is deliberately before even registering signature rows.
    if (!Admitted()) return Result::Unavailable;
    if (!o.image || !o.image->base || !o.image->size || !o.image->text.base ||
        !o.image->text.size || !o.image->rdata.base || !o.image->rdata.size)
        return Result::BadArg;
    phase = Phase::Preparing;
    try {
        if (!RegisterGameSignatures()) { phase = Phase::Failed; return Result::Failed; }
        ResolveAll(*o.image);
        resolved = true;
        phase = Phase::Prepared;
        return Result::Ok;
    } catch (...) { phase = Phase::Failed; return Result::Failed; }
}
Result Start() {
    if (phase.load() != Phase::Prepared) return Result::BadArg;
    if (!Admitted()) return Result::Unavailable;
    phase = Phase::Starting;
    try {
        if (!app::Start(options.platform)) { phase = Phase::Failed; return Result::Failed; }
        phase = Phase::Running;
        return Result::Ok;
    } catch (...) { phase = Phase::Failed; return Result::Failed; }
}
Result Stop() {
    const Phase p = phase.load();
    if (p == Phase::Idle || p == Phase::Stopped) return Result::Ok;
    if (!OnGameThread()) return Result::WrongThread;
    if (p == Phase::Stopping) return Result::Ok;
    if (p != Phase::Running) return Result::Unavailable;
    // app::Stop refuses unsafe entry. Do not claim Stopped if it would be ignored.
    if (sco::detail::InCallout()) return Result::Unavailable;
    phase = Phase::Stopping;
    app::Stop();
    phase = Phase::Stopped;
    return Result::Ok;
}
Snapshot Status() { return {phase.load(), resolved.load()}; }
} // namespace sco::game::host
