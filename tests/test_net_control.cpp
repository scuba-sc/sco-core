// Synthetic host-control fixture: no socket, thread, DNS or real network.
#include "sco/net/control.h"
#include "sco/host.h"
#include "sco/net/udp.h"
#include "net_mem.h"
#include <cstdio>

using namespace sco::net;
using sco::Result;
static int passed = 0, failed = 0;
static unsigned socketAttempts = 0;
namespace sco::net::udp {
std::unique_ptr<Socket> Socket::Open(uint16_t, bool, std::string*) { ++socketAttempts; return nullptr; }
Socket::~Socket() { (void)fd_; }
bool Socket::Send(const Endpoint&, const uint8_t*, size_t) { ++socketAttempts; return false; }
bool Socket::Receive(Endpoint*, uint8_t*, size_t, size_t*) { ++socketAttempts; return false; }
bool Socket::Wait(uint32_t) { ++socketAttempts; return false; }
}
#define CHECK(c) do { if (c) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

// Numeric IPv4 endpoints exercise the real strict scope, unlike kMemory's legacy bypass.
class SyntheticTransport final : public Transport {
public:
    SyntheticTransport(nettest::MemNet& net, const char* address) : net_(net) {
        ParseAddress(address, kDefaultPort, &self);
    }
    Endpoint self;
    unsigned sends = 0;
    bool Send(const Endpoint& to, const uint8_t* bytes, size_t len) override {
        ++sends; net_.Send(self, to, bytes, len); return true;
    }
    bool Receive(Endpoint* from, uint8_t* bytes, size_t cap, size_t* len) override {
        nettest::MemNet::Datagram d;
        if (!net_.Take(self, &d)) return false;
        *from = d.from; *len = d.bytes.size();
        for (size_t i = 0; i < cap && i < d.bytes.size(); ++i) bytes[i] = d.bytes[i];
        return true;
    }
private:
    nettest::MemNet& net_;
};

int main() {
    sco::SetGameThread();
    CHECK(sco::host::BuildApi({ "test_net_control" }) != nullptr);
    uint64_t now = 1000;
    ServiceOptions so;
    so.quotaMsgs = 2;
    so.thread = false; so.clock = [&] { return now; }; so.pbkdf2Iters = 10;
    CHECK(Start(so) == Result::Ok);
    nettest::MemNet net{ 134 };
    SyntheticTransport local{ net, "192.168.42.1" }, remote{ net, "192.168.42.2" };
    SessionControlV1 broker{ &local };
    ControlHostRequestV1 h;
    h.passphrase = "synthetic session only"; h.playerName = "SyntheticHost";
    CHECK(broker.Host(h) == Result::Unavailable);
    CHECK(GetState() == State::Idle && local.sends == 0);
    ControlPolicyV1 p;
    p.enabled = true; p.allowedAddresses = { "192.168.42.2" };
    CHECK(broker.Configure(p) == Result::Ok);
    CHECK(broker.Host(h) == Result::Failed); // no local consent
    h.consent = true;
    CHECK(broker.Host(h) == Result::Ok);
    CHECK(GetState() == State::Hosting);
    CHECK(broker.Host(h) == Result::BadArg); // backend refuses replacement
    auto peerCount = [] { uint32_t n = 0; Table()->get_peers(nullptr, &n); return n; };
    Core peer{ remote };
    Options opts;
    opts.passphrase = "wrong synthetic proof"; opts.playerName = "SyntheticPeer"; opts.pbkdf2Iters = 10;
    auto step = [&] { now += 10; net.now = now; PumpNetwork(); peer.Pump(now); };
    auto until = [&](auto done) {
        for (int i = 0; i < 2000 && !done(); ++i) step();
        return done();
    };
    CHECK(peer.Join(opts, local.self, now));
    CHECK(until([&] { return peer.GetState() == State::Idle; }));
    CHECK(peerCount() == 1 && GetServiceStats().core.joinsRefused > 0);
    opts.passphrase = h.passphrase;
    CHECK(peer.Join(opts, local.self, now));
    CHECK(until([&] { return peer.Active() && peerCount() == 2; }));
    sco_plugin* plugin = sco::host::NewPlugin("fixture");
    CHECK(Table()->register_channel(plugin, "fixture.pose", 0, 8,
        [](uint64_t, const void*, uint32_t, void*) {}, nullptr) == SCO_OK);
    CHECK(peer.RegisterChannel("fixture.pose", 0, 8));
    for (int i = 0; i < 20; ++i) step();
    CHECK(Table()->send_channel(plugin, "fixture.pose", "x", 1) == SCO_OK);
    CHECK(Table()->send_channel(plugin, "fixture.pose", "x", 1) == SCO_OK);
    CHECK(Table()->send_channel(plugin, "fixture.pose", "x", 1) == SCO_TOO_MANY);
    CHECK(GetServiceStats().quotaRefused == 1);
    // Junk from an approved address reaches framing; unchosen LAN, public and memory
    // sources are discarded BEFORE framing despite the legacy LAN defaults.
    Endpoint unchosen, publicAddress;
    ParseAddress("192.168.42.3", kDefaultPort, &unchosen);
    ParseAddress("203.0.113.5", kDefaultPort, &publicAddress);
    const auto before = GetServiceStats();
    net.Inject(remote.self, local.self, { 1, 2, 3 });
    net.Inject(unchosen, local.self, { 1, 2, 3 });
    net.Inject(publicAddress, local.self, { 1, 2, 3 });
    net.Inject(nettest::MemEndpoint(9), local.self, { 1, 2, 3 });
    step();
    CHECK(GetServiceStats().outOfScope == before.outOfScope + 3);
    CHECK(GetServiceStats().core.malformed == before.core.malformed + 1);
    CHECK(peerCount() == 2);
    // Stale authenticated peer is removed by the actual backend's timeout.
    now += 40000; net.now = now; PumpNetwork();
    CHECK(peerCount() == 1);
    broker.Leave(); broker.Leave();
    CHECK(GetState() == State::Idle && Table()->is_active() == 0);
    peer.Leave("fixture reset");
    // Strict scope also constrains sends and ignores any=true, including kMemory.
    Scope strict;
    strict.allowOnly = true; strict.any = true;
    Cidr exact; CHECK(ParseCidr("192.168.42.2", &exact)); strict.allow.push_back(exact);
    ScopedTransport filtered{ local, strict };
    const uint8_t byte = 0;
    const auto sends = local.sends;
    CHECK(!filtered.Send(unchosen, &byte, 1));
    CHECK(!filtered.Send(nettest::MemEndpoint(9), &byte, 1));
    CHECK(local.sends == sends && filtered.Refused() == 2);
    Scope legacy; CHECK(InScope(legacy, unchosen) && InScope(legacy, nettest::MemEndpoint(9)));
    // No secret is available for reuse: every new operation supplies one.
    auto empty = h; empty.passphrase = {};
    CHECK(broker.Host(empty) == Result::BadArg);
    empty = h; empty.playerName = "bad\nname"; CHECK(broker.Host(empty) == Result::BadArg);
    empty = h; empty.port = 0; CHECK(broker.Host(empty) == Result::BadArg);
    empty = h; empty.maxPeers = 1; CHECK(broker.Host(empty) == Result::BadArg);
    empty = h; const std::string huge(257, 'x'); empty.passphrase = huge;
    CHECK(broker.Host(empty) == Result::BadArg);
    // Join uses the real backend and parsed approved IPv4 even through injection.
    CHECK(peer.Host(opts, now));
    ControlJoinRequestV1 j;
    j.address = "192.168.42.2"; j.passphrase = h.passphrase; j.playerName = "SyntheticJoiner";
    CHECK(broker.Join(j) == Result::Failed);
    j.consent = true;
    for (const char* denied : { "192.168.42.3", "203.0.113.5", "fixture.invalid", "192.168.042.2", "192.168.42.2/32" }) {
        j.address = denied; CHECK(broker.Join(j) == Result::BadArg);
    }
    j.address = "192.168.42.2"; j.passphrase = "wrong synthetic proof";
    CHECK(broker.Join(j) == Result::Ok);
    CHECK(until([&] { return GetState() == State::Idle; }));
    CHECK(!LastReason().empty()); broker.Leave();
    j.passphrase = h.passphrase;
    CHECK(broker.Join(j) == Result::Ok);
    CHECK(until([&] { return GetState() == State::Joined && Table()->is_active(); }));
    CHECK(peer.Peers().size() == 2);
    CHECK(broker.Configure(ControlPolicyV1{}) == Result::Ok); // disable tears down
    CHECK(GetState() == State::Idle && broker.Join(j) == Result::Unavailable);
    CHECK(until([&] { return peer.Peers().size() == 1; }));
    peer.Leave("fixture done");
    // All malformed/wide/public policy choices fail closed, including from active state.
    CHECK(broker.Configure(p) == Result::Ok && broker.Host(h) == Result::Ok);
    for (const char* denied : { "0.0.0.0", "203.0.113.5", "192.168.42.0/24", "*", "fixture.invalid", "::1", "192.168.042.2" }) {
        ControlPolicyV1 bad; bad.enabled = true; bad.allowedAddresses = { denied };
        CHECK(broker.Configure(bad) == Result::BadArg);
        CHECK(GetState() == State::Idle && broker.Host(h) == Result::Unavailable);
    }
    ControlPolicyV1 bad; bad.enabled = true;
    CHECK(broker.Configure(bad) == Result::BadArg);
    bad.allowedAddresses.assign(17, "192.168.42.2"); CHECK(broker.Configure(bad) == Result::BadArg);
    bad.allowedAddresses.assign(2, "192.168.42.2"); CHECK(broker.Configure(bad) == Result::BadArg);
    bad.allowedAddresses = { "100.64.42.2" }; CHECK(broker.Configure(bad) == Result::Ok);
    // Destruction and terminal stop leave no session/peer state and cannot reopen.
    broker.Stop(); broker.Stop();
    CHECK(broker.Configure(p) == Result::Unavailable && broker.Host(h) == Result::Unavailable);
    CHECK(broker.Join(j) == Result::Unavailable);
    {
        SessionControlV1 temporary{ &local };
        CHECK(temporary.Configure(p) == Result::Ok && temporary.Host(h) == Result::Ok);
    }
    CHECK(GetState() == State::Idle && Table()->is_active() == 0 && peerCount() == 0);
    Stop();
    {
        SessionControlV1 unavailable{ &local };
        CHECK(unavailable.Configure(p) == Result::Ok && unavailable.Host(h) == Result::Unavailable);
        CHECK(unavailable.Join(j) == Result::Unavailable);
    }
    CHECK(socketAttempts == 0);
    CHECK(sco::Release(plugin) == Result::Ok);
    std::printf("test_net_control: %d passed, %d failed (synthetic; no sockets)\n", passed, failed);
    return failed ? 1 : 0;
}
