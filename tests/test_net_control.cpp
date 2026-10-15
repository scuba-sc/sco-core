// Synthetic host-control fixture: no socket, thread, DNS or real network.
#include "sco/net/control.h"
#include "sco/host.h"
#include "sco/net/udp.h"
#include "net_mem.h"
#ifdef SCO_TEST_PRIVATE_SESSION
#include "sco/game/private_session.h"
#include "sc_private_session.h"
#include "sco/caps.h"
#include <thread>
#endif
#include <cstdio>

using namespace sco::net;
using sco::Result;
#ifdef SCO_TEST_PRIVATE_SESSION
// The broker's cap and the session layer's limit are two constants for one number. If they drifted
// apart, a request the broker lets through would be refused one layer down.
static_assert(SC_PRIVATE_SESSION_MAX_PEERS == kMaxPeers, "broker cap and session limit agree");
static_assert(SC_PRIVATE_SESSION_MAX_PEERS == 51u, "the limit is 51 players, the host included");
static_assert(SC_PRIVATE_SESSION_MAX_PEERS_2_2 == 16u, "the cap of a runtime from SDK 2.2.0 and earlier");
#endif
static int passed = 0, failed = 0;
static unsigned socketAttempts = 0;
namespace sco::net::udp {
std::unique_ptr<Socket> Socket::Open(uint16_t, bool, std::string*) { ++socketAttempts; return nullptr; }
std::unique_ptr<Socket> Socket::Open(uint16_t, const std::string&, std::string*) { ++socketAttempts; return nullptr; }
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
    CHECK(broker.Host(h) == Result::Failed); // default enabled, no fresh consent
    CHECK(GetState() == State::Idle && local.sends == 0);
    ControlPolicyV1 p;
    p.enabled = true; p.bindAddress = "192.168.42.1"; p.allowedAddresses = { "192.168.42.2" };
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
    // Global host ingress budget counts even approved malformed frames.
    Scope rateScope = strict; rateScope.maxDatagramsPerSecond = 2;
    ScopedTransport limited{local, rateScope};
    net.Inject(remote.self, local.self, {0}); net.Inject(remote.self, local.self, {0});
    net.Inject(remote.self, local.self, {0});
    Endpoint from; uint8_t buffer[8]; size_t len = 0;
    CHECK(limited.Receive(&from, buffer, sizeof(buffer), &len));
    CHECK(limited.Receive(&from, buffer, sizeof(buffer), &len));
    CHECK(!limited.Receive(&from, buffer, sizeof(buffer), &len));
    nettest::MemNet::Datagram remaining; CHECK(net.Take(local.self, &remaining));
    for (int i = 0; i < 65; ++i) net.Inject(unchosen, local.self, {0});
    CHECK(!filtered.Receive(&from, buffer, sizeof(buffer), &len) && filtered.Dropped() == 64);
    CHECK(!filtered.Receive(&from, buffer, sizeof(buffer), &len) && filtered.Dropped() == 65);
    ControlPolicyV1 defaultPolicy;
    CHECK(defaultPolicy.enabled && defaultPolicy.bindAddress == "127.0.0.1");
    defaultPolicy.allowedAddresses = {"192.168.42.2"};
    CHECK(broker.Configure(defaultPolicy) == Result::BadArg); // loopback cannot silently expand LAN
    for (const char* bind : {"0.0.0.0", "203.0.113.5", "fixture.invalid", "192.168.42.0/24"}) {
        auto invalidBind = p; invalidBind.bindAddress = bind;
        CHECK(broker.Configure(invalidBind) == Result::BadArg);
    }
    CHECK(broker.Configure(p) == Result::Ok);
    // No secret is available for reuse: every new operation supplies one.
    auto empty = h; empty.passphrase = {};
    CHECK(broker.Host(empty) == Result::BadArg);
    empty = h; empty.playerName = "bad\nname"; CHECK(broker.Host(empty) == Result::BadArg);
    empty = h; empty.port = 0; CHECK(broker.Host(empty) == Result::BadArg);
    empty = h; empty.maxPeers = 1; CHECK(broker.Host(empty) == Result::BadArg);
    // The limit is kMaxPeers (51): one more is refused, the limit itself starts a session.
    empty = h; empty.maxPeers = kMaxPeers + 1; CHECK(broker.Host(empty) == Result::BadArg);
    empty = h; empty.maxPeers = kMaxPeers; CHECK(broker.Host(empty) == Result::Ok);
    CHECK(GetState() == State::Hosting);
    broker.Leave();
    CHECK(GetState() == State::Idle);
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
    ControlPolicyV1 disabled; disabled.enabled = false;
    CHECK(broker.Configure(disabled) == Result::Ok); // disable tears down
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
    ControlPolicyV1 bad; bad.enabled = true; bad.bindAddress = "192.168.42.1";
    bad.allowedAddresses.clear();
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
#ifdef SCO_TEST_PRIVATE_SESSION
    // Public C service drives the same authenticated backend, never plugin sockets.
    namespace ps = sco::game::private_session;
    static bool admitted = false;
    so.ownerReleased = ps::ReleaseOwner;
    CHECK(Start(so) == Result::Ok);
    ps::Options po; po.policy = p; po.syntheticTransport = &local;
    po.admitted = [] { return admitted; };
    CHECK(ps::Start(po) == Result::Unavailable); // unknown/online/EAC denied
    const void* publicTable = nullptr;
    CHECK(sco::QueryService(SC_PRIVATE_SESSION_NAME, SC_PRIVATE_SESSION_VERSION, &publicTable) == Result::NotFound);
    admitted = true;
    // A registration collision must never set a ready capability.
    static const sc_private_session_v1 collision{};
    CHECK(sco::host::ProvideGameService(SC_PRIVATE_SESSION_NAME, SC_PRIVATE_SESSION_VERSION, &collision) == Result::Ok);
    CHECK(ps::Start(po) == Result::BadArg && !sco::caps::Has(SC_PRIVATE_SESSION_NAME));
    CHECK(sco::host::WithdrawGameService(SC_PRIVATE_SESSION_NAME) == Result::Ok);
    CHECK(ps::Start(po) == Result::Ok);
    CHECK(sco::caps::Has(SC_PRIVATE_SESSION_NAME) && BoundPort() == 0 && GetState() == State::Idle);
    CHECK(sco::QueryService(SC_PRIVATE_SESSION_NAME, SC_PRIVATE_SESSION_VERSION, &publicTable) == Result::Ok);
    auto api = static_cast<const sc_private_session_v1*>(publicTable);
    sco_plugin* controller = sco::host::NewPlugin("multiplayer");
    sco_plugin* other = sco::host::NewPlugin("other");
    sc_private_session_host_request_v1 hr{};
    hr.size = sizeof(hr); hr.port = kDefaultPort; hr.max_peers = 8;
    hr.passphrase = "synthetic secret only"; hr.passphrase_len = 21;
    hr.player_name = "Fixture"; hr.player_name_len = 7;
    CHECK(api->request_host(controller, &hr) == SCO_FAILED);
    hr.consent = 2; CHECK(api->request_host(controller, &hr) == SCO_BAD_ARG);
    hr.consent = 1;
    hr.size = sizeof(uint32_t); CHECK(api->request_host(controller, &hr) == SCO_BAD_ARG);
    hr.size = sizeof(hr); hr.passphrase_len = 11; CHECK(api->request_host(controller, &hr) == SCO_BAD_ARG);
    hr.passphrase_len = 21;
    sco_result wrongThread = SCO_OK;
    std::thread worker([&] { wrongThread = api->request_host(controller, &hr); }); worker.join();
    CHECK(wrongThread == SCO_WRONG_THREAD);
    // The cap is SC_PRIVATE_SESSION_MAX_PEERS (51). One more, or fewer than two, is refused before it
    // counts as an attempt; the cap itself is the limit used for the sessions below.
    hr.max_peers = SC_PRIVATE_SESSION_MAX_PEERS + 1; CHECK(api->request_host(controller, &hr) == SCO_BAD_ARG);
    hr.max_peers = 1; CHECK(api->request_host(controller, &hr) == SCO_BAD_ARG);
    hr.max_peers = SC_PRIVATE_SESSION_MAX_PEERS;
    CHECK(api->request_host(controller, &hr) == SCO_OK); // attempt 1
    CHECK(api->request_host(other, &hr) == SCO_UNAVAILABLE);
    sc_private_session_state_v1 state{sizeof(state), 0, 0, 0};
    CHECK(api->get_state(&state) == SCO_OK && state.state == SC_PRIVATE_SESSION_HOSTING && state.peer_count == 1);
    CHECK(state.max_peers == SC_PRIVATE_SESSION_MAX_PEERS);
    CHECK(api->leave(other) == SCO_UNAVAILABLE && GetState() == State::Hosting);
    CHECK(api->leave(controller) == SCO_OK);
    sc_private_session_join_request_v1 jr{};
    jr.size = sizeof(jr); jr.consent = 1; jr.port = kDefaultPort;
    jr.address = "203.0.113.5"; jr.address_len = 11;
    jr.passphrase = hr.passphrase; jr.passphrase_len = hr.passphrase_len;
    jr.player_name = hr.player_name; jr.player_name_len = hr.player_name_len;
    CHECK(api->request_join(controller, &jr) == SCO_BAD_ARG); // attempt 2
    CHECK(api->request_host(controller, &hr) == SCO_OK); // attempt 3
    CHECK(sco::Release(controller) == Result::Ok && GetState() == State::Idle); // reused net release hook
    CHECK(api->request_host(controller, &hr) == SCO_BAD_ARG);
    CHECK(api->request_host(other, &hr) == SCO_OK); // attempt 4
    admitted = false; ps::Tick();
    CHECK(GetState() == State::Idle && !sco::caps::Has(SC_PRIVATE_SESSION_NAME));
    CHECK(api->request_host(other, &hr) == SCO_UNAVAILABLE);
    admitted = true;
    CHECK(api->request_host(other, &hr) == SCO_TOO_MANY); // global bounded requests
    ps::Stop();
    CHECK(sco::QueryService(SC_PRIVATE_SESSION_NAME, SC_PRIVATE_SESSION_VERSION, &publicTable) == Result::NotFound);
    CHECK(api->request_host(other, &hr) == SCO_UNAVAILABLE);
    CHECK(sco::Release(other) == Result::Ok);
    Stop();
#endif
    CHECK(socketAttempts == 0);
    CHECK(sco::Release(plugin) == Result::Ok);
    std::printf("test_net_control: %d passed, %d failed (synthetic; no sockets)\n", passed, failed);
    return failed ? 1 : 0;
}
