#pragma once
// Host-source contract v1; NOT a plugin service or a wire ABI. See docs/net.md.
// Use one broker on one host control thread; do not mix direct Host/Join/Leave
// calls with it. Destroy/Stop it before stopping sco.net or its injected transport.
#include "sco/net/session.h"
#include <string_view>
#include <vector>

namespace sco::net {

struct ControlPolicyV1 {
    bool enabled = false;
    // 1..16 exact numeric IPv4 addresses, private/loopback/link-local or CGNAT VPN.
    // No DNS, CIDRs, public addresses, automatic LAN expansion or wildcard mode.
    std::vector<std::string> allowedAddresses;
};
struct ControlHostRequestV1 {
    bool consent = false; // fresh explicit local confirmation, never peer-supplied
    uint16_t port = kDefaultPort;
    std::string_view passphrase; // borrowed for this call; never remembered by broker
    std::string_view playerName;
    uint32_t maxPeers = 8;
};
struct ControlJoinRequestV1 {
    bool consent = false;
    std::string_view address;
    uint16_t port = kDefaultPort;
    std::string_view passphrase;
    std::string_view playerName;
};

class SessionControlV1 final {
public:
    // Test seam ONLY: non-owned synthetic datagram transport. Null uses existing UDP
    // backend. Requests cannot select a transport or bypass numeric address policy.
    explicit SessionControlV1(Transport* syntheticTransport = nullptr);
    ~SessionControlV1();
    SessionControlV1(const SessionControlV1&) = delete;
    SessionControlV1& operator=(const SessionControlV1&) = delete;
    // Reconfiguration always leaves our old session first. Invalid policy fails
    // closed (disabled). Disabled policy needs no addresses and opens no socket.
    Result Configure(const ControlPolicyV1& policy);
    Result Host(const ControlHostRequestV1& request);
    Result Join(const ControlJoinRequestV1& request);
    void Leave(); // no consent required to stop; idempotent
    void Stop();  // terminal: leaves, disables, clears policy; cannot reopen
private:
    Transport* transport_;
    Scope scope_;
    bool enabled_ = false, ownsSession_ = false, stopped_ = false;
};

} // namespace sco::net
