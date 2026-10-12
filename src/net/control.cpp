// Host-only explicit-consent control over the existing sco.net session backend.
#include "sco/net/control.h"

namespace sco::net {
namespace {
bool PrivateAddress(const Endpoint& e) {
    return IsLan(e) || (e.family == Endpoint::kIpv4 && e.addr[0] == 100 &&
                       e.addr[1] >= 64 && e.addr[1] <= 127);
}
bool ValidRequest(std::string_view passphrase, std::string_view name, uint16_t port) {
    return port != 0 && !passphrase.empty() && passphrase.size() <= 256 &&
           passphrase.find('\0') == std::string_view::npos && ValidName(name);
}
}
SessionControlV1::SessionControlV1(Transport* transport) : transport_(transport) {}
SessionControlV1::~SessionControlV1() { Stop(); }
Result SessionControlV1::Configure(const ControlPolicyV1& policy) {
    if (stopped_) return Result::Unavailable;
    Leave();
    enabled_ = false;
    scope_ = Scope();
    if (!policy.enabled) return Result::Ok;
    if (policy.allowedAddresses.empty() || policy.allowedAddresses.size() > 16) return Result::BadArg;
    Scope next;
    next.allowOnly = true;
    for (const auto& address : policy.allowedAddresses) {
        Endpoint e;
        Cidr c;
        if (!ParseAddress(address, kDefaultPort, &e) || !PrivateAddress(e) || !ParseCidr(address, &c))
            return Result::BadArg;
        for (const auto& old : next.allow)
            if (InCidr(old, e)) return Result::BadArg;
        next.allow.push_back(c);
    }
    scope_ = std::move(next);
    enabled_ = true;
    return Result::Ok;
}
Result SessionControlV1::Host(const ControlHostRequestV1& r) {
    if (stopped_ || !enabled_) return Result::Unavailable;
    if (!r.consent) return Result::Failed;
    if (!ValidRequest(r.passphrase, r.playerName, r.port) || r.maxPeers < 2 || r.maxPeers > kMaxPeers)
        return Result::BadArg;
    HostOptions o;
    o.port = r.port; o.passphrase = r.passphrase; o.playerName = r.playerName;
    o.maxPeers = r.maxPeers; o.scope = scope_; o.transport = transport_;
    const Result result = sco::net::Host(o);
    if (result == Result::Ok) ownsSession_ = true;
    return result;
}
Result SessionControlV1::Join(const ControlJoinRequestV1& r) {
    if (stopped_ || !enabled_) return Result::Unavailable;
    if (!r.consent) return Result::Failed;
    Endpoint e;
    if (!ValidRequest(r.passphrase, r.playerName, r.port) ||
        !ParseAddress(r.address, r.port, &e) || !InScope(scope_, e)) return Result::BadArg;
    JoinOptions o;
    o.address = r.address; o.port = r.port; o.passphrase = r.passphrase; o.playerName = r.playerName;
    o.scope = scope_; o.transport = transport_; o.endpoint = e;
    const Result result = sco::net::Join(o);
    if (result == Result::Ok) ownsSession_ = true;
    return result;
}
void SessionControlV1::Leave() {
    if (ownsSession_) sco::net::Leave("host left private session");
    ownsSession_ = false;
}
void SessionControlV1::Stop() {
    Leave(); enabled_ = false; scope_ = Scope(); stopped_ = true;
}
} // namespace sco::net
