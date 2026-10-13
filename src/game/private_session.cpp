// SPDX-License-Identifier: GPL-3.0-only
#include "sco/game/private_session.h"
#include "sc_private_session.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "sco/log.h"
#include "../api/internal.h"
#include <chrono>
#include <memory>
#include <string_view>

namespace sco::game::private_session {
namespace {
std::unique_ptr<net::SessionControlV1> broker;
bool (*inspect)() = nullptr;
sco_plugin* owner = nullptr;
bool calling = false;
uint32_t maxPeers = 0;
net::State lastState = net::State::Idle;
uint64_t rateWindow = 0;
uint32_t attempts = 0;
struct CallScope { CallScope() { calling = true; } ~CallScope() { calling = false; } };
bool Admitted() noexcept {
    if (!inspect) return false;
    try { return inspect(); } catch (...) { return false; }
}
void End(const char* reason) {
    if (owner) Log("[private-session] leave: %s", reason);
    if (broker) broker->Leave();
    owner = nullptr; maxPeers = 0; lastState = net::State::Idle;
}
sco_result Check(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    if (calling || !broker || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    return SCO_OK;
}
sco_result AdmitRequest() {
    if (!Admitted()) {
        End("admission refused");
        caps::Set(SC_PRIVATE_SESSION_NAME, false, "offline admission refused");
        return SCO_UNAVAILABLE;
    }
    if (owner && net::GetState() == net::State::Idle) End("session completed");
    if (owner || net::GetState() != net::State::Idle) return SCO_UNAVAILABLE;
    const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (now - rateWindow >= 1000) { rateWindow = now; attempts = 0; }
    if (attempts >= 4) return SCO_TOO_MANY;
    ++attempts;
    return SCO_OK;
}
bool Bytes(const char* p, uint32_t len, uint32_t min, uint32_t max) {
    return p && len >= min && len <= max &&
        std::string_view(p, len).find('\0') == std::string_view::npos;
}
sco_result Host(sco_plugin* self, const sc_private_session_host_request_v1* r) {
    const auto check = Check(self); if (check != SCO_OK) return check;
    if (!r || r->size < sizeof(*r)) return SCO_BAD_ARG;
    if (r->consent > 1 || r->_pad) return SCO_BAD_ARG;
    if (!r->consent) return SCO_FAILED;
    if (!Bytes(r->passphrase, r->passphrase_len, SC_PRIVATE_SESSION_MIN_PASSPHRASE,
               SC_PRIVATE_SESSION_MAX_PASSPHRASE) ||
        !Bytes(r->player_name, r->player_name_len, 1, SC_PRIVATE_SESSION_MAX_PLAYER_NAME) ||
        !r->port || r->max_peers < 2 || r->max_peers > SC_PRIVATE_SESSION_MAX_PEERS)
        return SCO_BAD_ARG;
    CallScope scope;
    const auto admitted = AdmitRequest(); if (admitted != SCO_OK) return admitted;
    net::ControlHostRequestV1 request;
    request.consent = true; request.port = r->port; request.maxPeers = r->max_peers;
    request.passphrase = {r->passphrase, r->passphrase_len};
    request.playerName = {r->player_name, r->player_name_len};
    Result result = Result::Failed;
    try { result = broker->Host(request); } catch (...) { End("host failed"); }
    Log("[private-session] host request: %s", ResultName(result));
    if (result == Result::Ok) { owner = self; maxPeers = r->max_peers; lastState = net::GetState(); }
    return static_cast<sco_result>(result);
}
sco_result Join(sco_plugin* self, const sc_private_session_join_request_v1* r) {
    const auto check = Check(self); if (check != SCO_OK) return check;
    if (!r || r->size < sizeof(*r)) return SCO_BAD_ARG;
    if (r->consent > 1 || r->_pad) return SCO_BAD_ARG;
    if (!r->consent) return SCO_FAILED;
    if (!Bytes(r->address, r->address_len, 1, SC_PRIVATE_SESSION_MAX_ADDRESS) ||
        !Bytes(r->passphrase, r->passphrase_len, SC_PRIVATE_SESSION_MIN_PASSPHRASE,
               SC_PRIVATE_SESSION_MAX_PASSPHRASE) ||
        !Bytes(r->player_name, r->player_name_len, 1, SC_PRIVATE_SESSION_MAX_PLAYER_NAME) || !r->port)
        return SCO_BAD_ARG;
    CallScope scope;
    const auto admitted = AdmitRequest(); if (admitted != SCO_OK) return admitted;
    net::ControlJoinRequestV1 request;
    request.consent = true; request.port = r->port;
    request.address = {r->address, r->address_len};
    request.passphrase = {r->passphrase, r->passphrase_len};
    request.playerName = {r->player_name, r->player_name_len};
    Result result = Result::Failed;
    try { result = broker->Join(request); } catch (...) { End("join failed"); }
    Log("[private-session] join request: %s", ResultName(result));
    if (result == Result::Ok) { owner = self; maxPeers = 0; lastState = net::GetState(); }
    return static_cast<sco_result>(result);
}
sco_result Leave(sco_plugin* self) {
    const auto check = Check(self); if (check != SCO_OK) return check;
    if (owner && owner != self) return SCO_UNAVAILABLE;
    CallScope scope; End("explicit leave"); return SCO_OK;
}
sco_result State(sc_private_session_state_v1* out) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    if (!broker || calling || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    CallScope scope;
    if (!Admitted()) { End("admission lost"); return SCO_UNAVAILABLE; }
    uint32_t count = 0; (void)net::Table()->get_peers(nullptr, &count);
    *out = {sizeof(*out), static_cast<uint32_t>(net::GetState()), count, maxPeers};
    return SCO_OK;
}
const sc_private_session_v1 table{sizeof(table), 0, Host, Join, Leave, State};
}
Result Start(const Options& options) {
    if (!OnGameThread()) return Result::WrongThread;
    if (broker || calling) return Result::BadArg;
    caps::Set(SC_PRIVATE_SESSION_NAME, false, "broker not published");
    if (!options.policy.enabled) return Result::Ok;
    if (!net::Started() || !net::HasOwnerReleaseDispatcher(ReleaseOwner) || !options.admitted)
        return Result::Unavailable;
    CallScope scope;
    inspect = options.admitted;
    if (!Admitted()) { inspect = nullptr; return Result::Unavailable; }
    try {
        auto next = std::make_unique<net::SessionControlV1>(options.syntheticTransport);
        const auto configured = next->Configure(options.policy);
        if (configured != Result::Ok) { inspect = nullptr; return configured; }
        const auto result = host::ProvideGameService(SC_PRIVATE_SESSION_NAME, SC_PRIVATE_SESSION_VERSION, &table);
        if (result != Result::Ok) { inspect = nullptr; return result; }
        broker = std::move(next);
    } catch (...) { inspect = nullptr; return Result::TooMany; }
    owner = nullptr; maxPeers = 0; lastState = net::State::Idle;
    caps::Set(SC_PRIVATE_SESSION_NAME, true, "enabled; explicit consent required");
    Log("[private-session] broker published; no session started");
    return Result::Ok;
}
void Tick() {
    if (!OnGameThread() || !broker || calling) return;
    CallScope scope;
    if (!Admitted()) {
        End("admission lost");
        caps::Set(SC_PRIVATE_SESSION_NAME, false, "offline admission refused");
        return;
    }
    const auto state = net::GetState();
    if (owner && state != lastState) {
        Log("[private-session] state: %u", static_cast<unsigned>(state));
        lastState = state;
        if (state == net::State::Idle) End("session completed");
    }
}
void Stop() {
    if (!OnGameThread() || calling) return;
    if (!broker) return;
    CallScope scope;
    End("host stop"); broker->Stop(); broker.reset(); inspect = nullptr;
    host::WithdrawGameService(SC_PRIVATE_SESSION_NAME);
    caps::Set(SC_PRIVATE_SESSION_NAME, false, "broker stopped");
    Log("[private-session] broker stopped");
}
void ReleaseOwner(const void* released) {
    if (OnGameThread() && released && owner == released) End("owner release");
}
}
