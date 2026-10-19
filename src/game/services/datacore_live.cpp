// SPDX-License-Identifier: GPL-3.0-only
#include "datacore_live.h"
#include "sco/host.h"
#include "sco/runtime.h"
#include "../../api/internal.h"
#if defined(_WIN32) && defined(_MSC_VER)
#include <windows.h>
#endif

namespace sco::game::services::datacore_live {
namespace {
Backend g_backend{};
bool g_started = false, g_faulted = false, g_inBackend = false, g_writes = false;
using Thunk = sco_result (*)(void*);
sco_result RunCpp(Thunk thunk, void* context) {
    try { return thunk(context); } catch (...) { return SCO_FAILED; }
}
sco_result RunGuarded(Thunk thunk, void* context) {
#if defined(_WIN32) && defined(_MSC_VER)
    __try { return RunCpp(thunk, context); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return SCO_FAILED; }
#else
    return RunCpp(thunk, context);
#endif
}
template<class F> sco_result Guard(F call) {
    g_inBackend = true;
    const auto result = RunGuarded([](void* p) { return (*static_cast<F*>(p))(); }, &call);
    g_inBackend = false;
    if (result == SCO_FAILED) g_faulted = true;
    return result;
}
sco_result Enter(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !sco::host::PluginId(self) || sco::detail::Released(self)) return SCO_BAD_ARG;
    if (!g_started || g_inBackend || g_faulted || !g_backend.verified || !g_backend.offline ||
        !g_backend.ready || !g_backend.stats || !g_backend.find || !g_backend.read || !g_backend.write)
        return SCO_UNAVAILABLE;
    return Guard([] { return g_backend.offline() && g_backend.ready() ? SCO_OK : SCO_UNAVAILABLE; });
}
sco_result IsReady(sco_plugin* self) { return Enter(self); }
sco_result Stats(sco_plugin* self, uint32_t type, sc_datacore_pool_stats* out) {
    auto r = Enter(self); if (r != SCO_OK) return r;
    if (type >= SC_DC_POOL_COUNT || !out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    return Guard([&] { return g_backend.stats(type, out); });
}
sco_result Find(sco_plugin* self, const uint8_t guid[16], sc_datacore_live_record_info* out) {
    auto r = Enter(self); if (r != SCO_OK) return r;
    if (!guid || !out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    return Guard([&] { return g_backend.find(guid, out); });
}
sco_result Read(sco_plugin* self, uint32_t type, uint64_t offset, void* out, size_t bytes, size_t* read) {
    auto r = Enter(self); if (r != SCO_OK) return r;
    if (!out || !bytes || !read || type >= SC_DC_POOL_COUNT) return SCO_BAD_ARG;
    *read = 0;
    return Guard([&] { return g_backend.read(type, offset, out, bytes, read); });
}
sco_result Write(sco_plugin* self, uint32_t type, uint64_t offset, const void* in, size_t bytes, size_t* written) {
    auto r = Enter(self); if (r != SCO_OK) return r;
    if (!in || !bytes || !written || type >= SC_DC_POOL_COUNT) return SCO_BAD_ARG;
    *written = 0;
    if (!g_writes) return SCO_UNAVAILABLE;
    return Guard([&] { return g_backend.write(type, offset, in, bytes, written); });
}
const sc_datacore_live_v1 kTable{sizeof(sc_datacore_live_v1), 0, IsReady, Stats, Find, Read, Write};
}
sco_result Start(const Backend* backend) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (g_started) return SCO_UNAVAILABLE;
    g_backend = backend ? *backend : Backend{};
    const auto r = sco::host::ProvideGameService(SC_DATACORE_LIVE_SERVICE_NAME, SC_DATACORE_LIVE_SERVICE_VERSION, &kTable);
    g_started = r == Result::Ok;
    if (!g_started) g_backend = {};
    return static_cast<sco_result>(r);
}
void Stop() {
    if (!OnGameThread() || g_inBackend) return;
    if (g_started) sco::host::WithdrawGameService(SC_DATACORE_LIVE_SERVICE_NAME);
    g_started = false; g_writes = false; g_backend = {};
}
void SetWritesEnabled(bool enabled) { if (OnGameThread() && !g_inBackend) g_writes = enabled; }
}
