// Tests for the sco.net host service (sco/net/session.h, include/sco_net.h, scosdk/net.hpp), the
// LAN rule (sco/net/scope.h) and the UDP transport (sco/net/udp.h).
//   - The service over the in-memory network of tests/net_mem.h with no network thread
//     (ServiceOptions::thread false): this thread runs each network step with PumpNetwork and
//     plays the game thread with Tick, on a virtual clock. Channel ownership, quotas, directions,
//     events, delivery on the game thread only, release on unload, hosting and joining.
//   - Two Cores over real UDP sockets on 127.0.0.1 in one process.
//   - The service with its network thread over UDP loopback, with a test Core as the other
//     player, and table calls from 8 threads (run under TSan by tools/test.sh).
//   - Failures of the service itself: the network thread throws, and an allocation fails while
//     the events the Core raised move to the game thread's inbox.
// Nothing sleeps for a fixed time: the in-memory runs advance a virtual clock, and the socket runs
// wait for an event (a datagram, a delivery) with a bound.
//   test_net_service             (tools/test.sh, CTest test_net_service)
#include "sco/caps.h"
#include "sco/host.h"
#include "sco/net/core.h"
#include "sco/net/scope.h"
#include "sco/net/session.h"
#include "sco/net/udp.h"
#include "sco/runtime.h"
#include "sco_api.h"
#include "sco_net.h"
#include "scosdk/net.hpp"
#include "net_mem.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace sco::net;
using nettest::MemNet;
using nettest::MemTransport;
using sco::Result;

static std::atomic<int> g_fail{ 0 }, g_pass{ 0 };
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const sco_api*    g_api = nullptr;
static const sco_net_v1* g_t = nullptr;
static std::atomic<uint64_t> g_clock{ 1000 };

constexpr const char* kPass = "correct horse";
constexpr uint32_t kIters = 1000;   // both sides; the default is exercised by test_net

// ---- allocation failure injection --------------------------------------------------------------
// This program replaces the global allocation functions so that one allocation on one thread can be made
// to fail with std::bad_alloc, the way the host runs out of memory. Allocations are counted only on the
// thread that called Arm, from then until Disarm; every other allocation goes straight to malloc.
// ThreadSanitizer's runtime defines operator new itself, and a second definition does not link, so a
// ThreadSanitizer build leaves this out and TestHeldTransferFailure says it did not run.

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
#define SCO_TEST_TSAN 1
#else
#define SCO_TEST_TSAN 0
#endif

#if !SCO_TEST_TSAN
namespace allocfail {
thread_local bool t_armed = false;
thread_local long t_seen = 0;     // allocations on this thread since Arm
thread_local long t_failAt = 0;   // the one that throws (1 is the next); 0 throws none
thread_local bool t_fired = false;

void Arm(long failAt) {
    t_seen = 0;
    t_failAt = failAt;
    t_fired = false;
    t_armed = true;
}
long Disarm() {
    t_armed = false;
    return t_seen;
}
}  // namespace allocfail

// Out of line: GCC 12 inlines a replaced operator delete into its callers, sees free() on what it takes
// for operator new's pointer and reports -Wmismatched-new-delete, which -Werror makes an error.
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE static void FreeBlock(void* p) noexcept { std::free(p); }

void* operator new(std::size_t n) {
    if (allocfail::t_armed && ++allocfail::t_seen == allocfail::t_failAt) {
        allocfail::t_fired = true;
        throw std::bad_alloc();
    }
    if (void* p = std::malloc(n != 0 ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { FreeBlock(p); }
void operator delete(void* p, std::size_t) noexcept { FreeBlock(p); }
#endif   // !SCO_TEST_TSAN

// ---- helpers ------------------------------------------------------------------------------------

static uint64_t SteadyMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

// What a plugin's callbacks saw, and whether each ran on the game thread inside Tick.
static bool g_inTick = false;
struct Got {
    struct Msg { uint64_t from; std::string data; };
    std::vector<Msg> msgs;
    bool offTick = false;
};
static void OnMsg(uint64_t from, const void* buf, uint32_t len, void* ctx) {
    Got* g = static_cast<Got*>(ctx);
    g->msgs.push_back(Got::Msg{ from, std::string(static_cast<const char*>(buf), len) });
    if (!g_inTick || !sco::OnGameThread()) g->offTick = true;
}
static void TickNet() {
    g_inTick = true;
    Tick();
    g_inTick = false;
}

// Events on the bus.
struct Events {
    std::vector<sco_net_state_event> states;
    std::vector<sco_net_peer_event>  peers;
    // For each "net.peer" event: what get_peer_flags answered from inside the handler, as a plugin reads it.
    std::vector<std::pair<sco_result, uint32_t>> peerFlags;
    // For each "net.state" event: its `active` and what is_active() said inside the handler.
    std::vector<std::pair<int, int>> stateSeen;
    // Every event in the order it was delivered: A and I for a state event (active, inactive), J, L and E
    // for a peer event (joined, left, entity).
    std::string order;
};
static Events g_events;
static int    g_eventsOwner;
static void OnEvent(const char* ev, const void* data, void*) {
    if (std::strcmp(ev, SCO_NET_EVENT_STATE) == 0) {
        const auto* e = static_cast<const sco_net_state_event*>(data);
        if (e->size == sizeof(sco_net_state_event)) {
            g_events.states.push_back(*e);
            g_events.stateSeen.emplace_back(e->active, g_t ? g_t->is_active() : -1);
            g_events.order += e->active ? 'A' : 'I';
        }
    } else if (std::strcmp(ev, SCO_NET_EVENT_PEER) == 0) {
        const auto* e = static_cast<const sco_net_peer_event*>(data);
        if (e->size == sizeof(sco_net_peer_event)) {
            g_events.peers.push_back(*e);
            g_events.order += e->what == SCO_NET_PEER_JOINED ? 'J' : e->what == SCO_NET_PEER_LEFT ? 'L' : 'E';
            uint32_t flags = 0xFFFFFFFFu;
            const sco_result r = g_t ? g_t->get_peer_flags(e->peer.peer_id, &flags) : SCO_UNAVAILABLE;
            g_events.peerFlags.emplace_back(r, flags);
        }
    }
}

// The other player: a plain Core the test drives.
struct Remote {
    struct Msg { PeerId from; std::string channel, data; };
    std::vector<Msg> got;
    Core core;
    explicit Remote(Transport& t) : core(t, Cbs()) {}
    Callbacks Cbs() {
        Callbacks c;
        c.message = [this](const Message& m) {
            got.push_back(Msg{ m.from, std::string(m.channel), std::string(reinterpret_cast<const char*>(m.data), m.len) });
        };
        return c;
    }
};

static Options RemoteOpts(const char* pass = kPass) {
    Options o;
    o.passphrase = pass;
    o.playerName = "Remote";
    o.pbkdf2Iters = kIters;
    return o;
}

// One step of the in-memory world: time moves, the service's network side runs, the remote runs.
struct Lab {
    MemNet       net{ 42 };
    MemTransport svc{ net, 1 }, other{ net, 2 };
    Remote       remote{ other };
    void Step(uint32_t ms = 10) {
        g_clock += ms;
        net.now = g_clock;
        PumpNetwork();
        remote.core.Pump(g_clock);
    }
    bool Until(const std::function<bool()>& done, int steps = 3000) {
        for (int i = 0; i < steps; ++i) {
            if (done()) return true;
            Step();
        }
        return done();
    }
};

static uint32_t PeerCount() {
    uint32_t n = 0;
    g_t->get_peers(nullptr, &n);
    return n;
}

static sco_result SendStr(sco_plugin* p, const char* ch, const std::string& s) {
    return g_t->send_channel(p, ch, s.data(), static_cast<uint32_t>(s.size()));
}

// ---- the table without a session ----------------------------------------------------------------

static void TestTable() {
    const void* table = nullptr;
    CHECK(g_api->query_service(SCO_NET_NAME, SCO_NET_VERSION_1_0, &table) == SCO_OK && table == g_t);
    CHECK(g_t->size == sizeof(sco_net_v1));
    CHECK(sco::caps::Has("sco.net"));
    CHECK(g_t->is_active() == 0 && g_t->self_peer() == 0);
    uint32_t n = 5;
    CHECK(g_t->get_peers(nullptr, &n) == SCO_BAD_ARG);
    n = 0;
    CHECK(g_t->get_peers(nullptr, &n) == SCO_OK && n == 0);
    CHECK(g_t->get_peers(nullptr, nullptr) == SCO_BAD_ARG);
    char buf[8];
    CHECK(g_t->get_peer_name(1, buf, sizeof buf) == SCO_NOT_FOUND);
    CHECK(g_t->get_peer_name(1, nullptr, 8) == SCO_BAD_ARG && g_t->get_peer_name(1, buf, 0) == SCO_BAD_ARG);
    CHECK(GetState() == State::Idle && BoundPort() == 0);
}

static Got g_alpha, g_beta;

static void TestOwnership() {
    sco_plugin* a = sco::host::NewPlugin("own_a");
    sco_plugin* b = sco::host::NewPlugin("own_b");
    Got got;
    // Names: only "<own id>.<name>", a valid sco.net name.
    CHECK(g_t->register_channel(a, "own_b.pose", 0, 64, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "own_a", 0, 64, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "own_a.", 0, 64, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "own_ax.pose", 0, 64, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "own_a.bad name", 0, 64, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "sco.net", 0, 64, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, nullptr, 0, 64, OnMsg, &got) == SCO_BAD_ARG);
    const std::string longName = "own_a." + std::string(SCO_NET_MAX_FQN, 'x');
    CHECK(g_t->register_channel(a, longName.c_str(), 0, 64, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(reinterpret_cast<sco_plugin*>(&got), "own_a.pose", 0, 64, OnMsg, &got) == SCO_BAD_ARG);
    // Flags, sizes, callback.
    CHECK(g_t->register_channel(a, "own_a.pose", 0, 64, nullptr, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "own_a.pose", 0x8, 64, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "own_a.pose", SCO_NET_FROM_HOST | SCO_NET_TO_HOST, 64, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "own_a.pose", 0, 0, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "own_a.pose", 0, SCO_NET_MAX_UNREL + 1, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "own_a.big", SCO_NET_RELIABLE, SCO_NET_MAX_RELIABLE + 1, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->register_channel(a, "own_a.big", SCO_NET_RELIABLE, SCO_NET_MAX_RELIABLE, OnMsg, &got) == SCO_OK);
    CHECK(g_t->register_channel(a, "own_a.pose", 0, 64, OnMsg, &got) == SCO_OK);
    CHECK(g_t->register_channel(a, "own_a.pose", 0, 64, OnMsg, &got) == SCO_BAD_ARG);   // taken
    CHECK(g_t->register_channel(b, "own_a.pose", 0, 64, OnMsg, &got) == SCO_BAD_ARG);   // not b's
    // Sending: ownership first, then registration, size, then the session.
    CHECK(SendStr(b, "own_a.pose", "x") == SCO_BAD_ARG);
    CHECK(SendStr(b, "own_b.none", "x") == SCO_NOT_FOUND);
    CHECK(SendStr(a, "own_a.pose", std::string(65, 'x')) == SCO_BAD_ARG);
    CHECK(g_t->send_channel(a, "own_a.pose", nullptr, 3) == SCO_BAD_ARG);
    CHECK(SendStr(a, "own_a.pose", "x") == SCO_UNAVAILABLE);   // no session
    // Unregistering.
    CHECK(g_t->unregister_channel(b, "own_a.big") == SCO_BAD_ARG);
    CHECK(g_t->unregister_channel(a, "own_a.big") == SCO_OK);
    CHECK(g_t->unregister_channel(a, "own_a.big") == SCO_NOT_FOUND);
    // At most SCO_NET_MAX_CHANNELS per plugin.
    int ok = 0;
    for (uint32_t i = 0; i < SCO_NET_MAX_CHANNELS + 1; ++i) {
        const std::string n = "own_b.c" + std::to_string(i);
        if (g_t->register_channel(b, n.c_str(), 0, 8, OnMsg, &got) == SCO_OK) ++ok;
    }
    CHECK(ok == static_cast<int>(SCO_NET_MAX_CHANNELS));
    CHECK(g_t->register_channel(b, "own_b.more", 0, 8, OnMsg, &got) == SCO_TOO_MANY);
    // Release drops them all: the names are free again, and the released handle is refused.
    CHECK(sco::Release(a) == Result::Ok && sco::Release(b) == Result::Ok);
    CHECK(g_t->register_channel(a, "own_a.pose", 0, 64, OnMsg, &got) == SCO_BAD_ARG);
    CHECK(g_t->unregister_channel(a, "own_a.pose") == SCO_BAD_ARG);
    sco_plugin* a2 = sco::host::NewPlugin("own_a");
    CHECK(g_t->register_channel(a2, "own_a.pose", 0, 64, OnMsg, &got) == SCO_OK);
    CHECK(sco::Release(a2) == Result::Ok);
    CHECK(got.msgs.empty());
}

// ---- hosting over the in-memory network ---------------------------------------------------------

static sco_plugin* g_a = nullptr;   // "alpha", released by TestReleaseOnUnload
static sco_plugin* g_a2 = nullptr;  // its reload
static sco_plugin* g_b = nullptr;   // "beta"

static void TestHostSession(Lab& lab) {
    g_a = sco::host::NewPlugin("alpha");
    g_b = sco::host::NewPlugin("beta");
    CHECK(g_t->register_channel(g_a, "alpha.pose", 0, 64, OnMsg, &g_alpha) == SCO_OK);
    CHECK(g_t->register_channel(g_a, "alpha.spawn", SCO_NET_RELIABLE, SCO_NET_MAX_RELIABLE, OnMsg, &g_alpha) == SCO_OK);
    CHECK(g_t->register_channel(g_a, "alpha.cmd", SCO_NET_FROM_HOST, 64, OnMsg, &g_alpha) == SCO_OK);
    CHECK(g_t->register_channel(g_a, "alpha.up", SCO_NET_TO_HOST, 64, OnMsg, &g_alpha) == SCO_OK);
    CHECK(g_t->register_channel(g_b, "beta.chat", SCO_NET_RELIABLE, 1024, OnMsg, &g_beta) == SCO_OK);

    HostOptions bad;
    bad.transport = &lab.svc;
    bad.playerName = "Host";
    CHECK(Host(bad) == Result::BadArg);   // no passphrase
    bad.passphrase = kPass;
    bad.playerName = std::string("bad\nname");
    CHECK(Host(bad) == Result::BadArg);
    bad.playerName = "Host";
    bad.maxPeers = SCO_NET_SESSION_MAX_PEERS + 1;
    CHECK(Host(bad) == Result::BadArg);

    HostOptions h;
    h.transport = &lab.svc;
    h.passphrase = kPass;
    h.playerName = "Host";
    CHECK(Host(h) == Result::Ok);
    CHECK(Host(h) == Result::BadArg);   // already in a session
    JoinOptions busy;
    busy.transport = &lab.svc;
    busy.endpoint = lab.other.Self();
    busy.passphrase = kPass;
    busy.playerName = "Host";
    CHECK(Join(busy) == Result::BadArg);
    CHECK(GetState() == State::Hosting && g_t->is_active() == 1 && g_t->self_peer() == kHostPeer);
    CHECK(g_events.states.empty());   // events wait for the game thread
    TickNet();
    CHECK(g_events.states.size() == 1 && g_events.states[0].active == 1);

    // The other player joins with the same channels (alpha's, but not beta's).
    Remote& r = lab.remote;
    CHECK(r.core.RegisterChannel("alpha.pose", 0, 64));
    CHECK(r.core.RegisterChannel("alpha.spawn", kReliable, kMaxReliable));
    CHECK(r.core.RegisterChannel("alpha.cmd", kFromHost, 64));
    CHECK(r.core.RegisterChannel("alpha.up", kToHost, 64));
    CHECK(r.core.Join(RemoteOpts(), lab.svc.Self(), g_clock));
    CHECK(lab.Until([&] { return r.core.GetState() == State::Joined && r.core.Active() && PeerCount() == 2; }));
    TickNet();
    CHECK(g_events.peers.size() == 1 && g_events.peers[0].what == SCO_NET_PEER_JOINED && g_events.peers[0].peer.peer_id == 2);

    // Peers and names.
    sco_net_peer peers[4] = {};
    uint32_t n = 1;
    CHECK(g_t->get_peers(peers, &n) == SCO_TOO_MANY && n == 2 && peers[0].peer_id == kHostPeer);
    n = 4;
    CHECK(g_t->get_peers(peers, &n) == SCO_OK && n == 2 && peers[1].peer_id == 2 && peers[1].entity_id == 0);
    char name[16];
    CHECK(g_t->get_peer_name(2, name, sizeof name) == SCO_OK && std::strcmp(name, "Remote") == 0);
    CHECK(g_t->get_peer_name(2, name, 4) == SCO_OK && std::strcmp(name, "Rem") == 0);
    CHECK(g_t->get_peer_name(1, name, sizeof name) == SCO_OK && std::strcmp(name, "Host") == 0);
    CHECK(g_t->get_peer_name(9, name, sizeof name) == SCO_NOT_FOUND);
    CHECK(SetPeerEntity(2, 0xCAFE) == Result::Ok && SetPeerEntity(9, 1) == Result::NotFound);
    n = 4;
    CHECK(g_t->get_peers(peers, &n) == SCO_OK && peers[1].entity_id == 0xCAFE);
    TickNet();
    CHECK(g_events.peers.size() == 2 && g_events.peers[1].what == SCO_NET_PEER_ENTITY &&
          g_events.peers[1].peer.entity_id == 0xCAFE);

    // The C++ wrapper sees the same.
    sco::sdk::Net net;
    CHECK(net.Open(g_api, g_a) == SCO_OK && net.Active() && net.SelfPeer() == kHostPeer);
    CHECK(net.Peers().size() == 2 && net.PeerName(2) == "Remote" && net.PeerName(7).empty());

    // Host -> remote.
    CHECK(SendStr(g_a, "alpha.pose", "hello") == SCO_OK);
    CHECK(lab.Until([&] { return !r.got.empty(); }));
    CHECK(r.got.size() == 1 && r.got[0].from == kHostPeer && r.got[0].channel == "alpha.pose" && r.got[0].data == "hello");

    // Remote -> host: queued until Tick, then delivered on the game thread.
    CHECK(r.core.Send("alpha.pose", "hi", 2) == SendResult::Ok);
    CHECK(r.core.Send("alpha.up", "up", 2) == SendResult::Ok);
    CHECK(lab.Until([] { return WaitForDelivery(0); }));
    lab.Step();
    CHECK(g_alpha.msgs.empty());
    TickNet();
    CHECK(g_alpha.msgs.size() == 2 && !g_alpha.offTick);
    CHECK(g_alpha.msgs.size() == 2 && g_alpha.msgs[0].from == 2 && g_alpha.msgs[0].data == "hi" && g_alpha.msgs[1].data == "up");

    // Directions: the host sends FROM_HOST, never TO_HOST.
    CHECK(SendStr(g_a, "alpha.up", "x") == SCO_BAD_ARG);
    CHECK(SendStr(g_a, "alpha.cmd", "go") == SCO_OK);
    CHECK(lab.Until([&] { return r.got.size() == 2; }));
    CHECK(r.got.size() == 2 && r.got[1].channel == "alpha.cmd" && r.got[1].data == "go");

    // A channel no other peer has: accepted, then dropped by the session and counted.
    const uint64_t dropped = GetServiceStats().sendDropped;
    CHECK(SendStr(g_b, "beta.chat", "anyone?") == SCO_OK);
    lab.Step();
    CHECK(GetServiceStats().sendDropped == dropped + 1);
}

static void TestQuotas(Lab& lab) {
    Remote& r = lab.remote;
    // Messages: a full bucket holds one second's worth (256), then SCO_TOO_MANY until time passes.
    g_clock += 2000;
    lab.Step();
    const uint64_t refusedBefore = GetServiceStats().quotaRefused;
    int ok = 0, tooMany = 0, other = 0;
    for (int i = 0; i < 300; ++i) {
        const sco_result res = SendStr(g_a, "alpha.pose", "p");
        if (res == SCO_OK) ++ok;
        else if (res == SCO_TOO_MANY) ++tooMany;
        else ++other;
    }
    CHECK(ok == static_cast<int>(SCO_NET_QUOTA_MSGS) && tooMany == 300 - ok && other == 0);
    CHECK(GetServiceStats().quotaRefused == refusedBefore + static_cast<uint64_t>(tooMany));
    CHECK(SendStr(g_b, "beta.chat", "mine") == SCO_OK);   // the quota is per plugin
    g_clock += 100;   // a tenth of a second refills a tenth of the bucket
    ok = 0;
    for (int i = 0; i < 100; ++i)
        if (SendStr(g_a, "alpha.pose", "p") == SCO_OK) ++ok;
    CHECK(ok == static_cast<int>(SCO_NET_QUOTA_MSGS / 10) || ok == static_cast<int>(SCO_NET_QUOTA_MSGS / 10) + 1);

    // Bytes: 512 KiB a second, so two full reliable messages, not three.
    g_clock += 2000;
    lab.Step();
    const size_t before = r.got.size();
    std::string big(SCO_NET_MAX_RELIABLE, 'b');
    big[0] = '1';
    CHECK(SendStr(g_a, "alpha.spawn", big) == SCO_OK);
    big[0] = '2';
    CHECK(SendStr(g_a, "alpha.spawn", big) == SCO_OK);
    CHECK(SendStr(g_a, "alpha.spawn", big) == SCO_TOO_MANY);
    CHECK(SendStr(g_a, "alpha.pose", "small") == SCO_TOO_MANY);   // the byte bucket is empty
    auto bigs = [&] {
        size_t k = 0;
        for (size_t i = before; i < r.got.size(); ++i)
            if (r.got[i].channel == "alpha.spawn") ++k;
        return k;
    };
    CHECK(lab.Until([&] { return bigs() == 2; }, 20000));
    bool inOrder = false;
    for (size_t i = before, k = 0; i < r.got.size(); ++i)
        if (r.got[i].channel == "alpha.spawn") {
            inOrder = r.got[i].data.size() == SCO_NET_MAX_RELIABLE && r.got[i].data[0] == static_cast<char>('1' + k++);
            if (!inOrder) break;
        }
    CHECK(inOrder);
}

static void TestReleaseOnUnload(Lab& lab) {
    Remote& r = lab.remote;
    // Three messages wait for alpha's next tick; it unloads first: none reaches it.
    g_alpha.msgs.clear();
    for (int i = 0; i < 3; ++i) CHECK(r.core.Send("alpha.pose", "late", 4) == SendResult::Ok);
    CHECK(lab.Until([] { return WaitForDelivery(0); }));
    lab.Step();
    CHECK(sco::Release(g_a) == Result::Ok);
    TickNet();
    CHECK(g_alpha.msgs.empty());
    CHECK(SendStr(g_a, "alpha.pose", "x") == SCO_BAD_ARG);
    CHECK(g_t->register_channel(g_a, "alpha.pose", 0, 64, OnMsg, &g_alpha) == SCO_BAD_ARG);
    // Its channels are gone from the session too: messages on them are dropped and counted there.
    const uint64_t dropped = GetServiceStats().core.unknownChannel;
    CHECK(r.core.Send("alpha.pose", "gone", 4) == SendResult::Ok);
    CHECK(lab.Until([&] { return GetServiceStats().core.unknownChannel > dropped; }));
    TickNet();
    CHECK(g_alpha.msgs.empty());
    // The plugin loads again: its channel works again in the same session.
    g_a2 = sco::host::NewPlugin("alpha");
    CHECK(g_t->register_channel(g_a2, "alpha.pose", 0, 64, OnMsg, &g_alpha) == SCO_OK);
    lab.Step();
    CHECK(r.core.Send("alpha.pose", "back", 4) == SendResult::Ok);
    CHECK(lab.Until([] { return WaitForDelivery(0); }));
    TickNet();
    CHECK(g_alpha.msgs.size() == 1 && g_alpha.msgs[0].data == "back" && !g_alpha.offTick);
    // Unregistered between the network step and the tick: skipped.
    CHECK(r.core.Send("alpha.pose", "skip", 4) == SendResult::Ok);
    CHECK(lab.Until([] { return WaitForDelivery(0); }));
    CHECK(g_t->unregister_channel(g_a2, "alpha.pose") == SCO_OK);
    TickNet();
    CHECK(g_alpha.msgs.size() == 1);
    CHECK(g_t->register_channel(g_a2, "alpha.pose", 0, 64, OnMsg, &g_alpha) == SCO_OK);
}

static void TestLeave(Lab& lab) {
    Remote& r = lab.remote;
    g_events.peers.clear();
    g_events.states.clear();
    r.core.Leave("going home");
    CHECK(lab.Until([] { return PeerCount() == 1; }));
    TickNet();
    CHECK(g_events.peers.size() == 1 && g_events.peers[0].what == SCO_NET_PEER_LEFT && g_events.peers[0].peer.peer_id == 2 &&
          g_events.peers[0].peer.entity_id == 0xCAFE);
    Leave("done");
    CHECK(GetState() == State::Idle && g_t->is_active() == 0 && g_t->self_peer() == 0 && PeerCount() == 0);
    CHECK(SendStr(g_a2, "alpha.pose", "x") == SCO_UNAVAILABLE);
    TickNet();
    CHECK(g_events.states.size() == 1 && g_events.states[0].active == 0 && g_events.states[0].reason[0] != '\0');
    CHECK(!LastReason().empty());
    Leave("again");   // no session: a no-op
    CHECK(GetState() == State::Idle);
}

// ---- joining a host over the in-memory network --------------------------------------------------

static void TestJoinSession() {
    Lab lab;
    Remote& r = lab.remote;
    CHECK(r.core.RegisterChannel("alpha.pose", 0, 64));
    CHECK(r.core.RegisterChannel("alpha.cmd", kFromHost, 64));
    CHECK(g_t->register_channel(g_a2, "alpha.cmd", SCO_NET_FROM_HOST, 64, OnMsg, &g_alpha) == SCO_OK);
    Options ho = RemoteOpts();
    ho.playerName = "Hostess";
    CHECK(r.core.Host(ho, g_clock));

    JoinOptions bad;
    bad.address = "8.8.8.8";   // not LAN: refused before anything is opened
    bad.passphrase = kPass;
    bad.playerName = "Me";
    CHECK(Join(bad) == Result::BadArg);
    bad.address = "not an address";
    CHECK(Join(bad) == Result::BadArg);

    JoinOptions j;
    j.transport = &lab.svc;
    j.endpoint = lab.other.Self();
    j.passphrase = kPass;
    j.playerName = "Me";
    CHECK(Join(j) == Result::Ok && GetState() == State::Joining && g_t->is_active() == 0);
    CHECK(lab.Until([] { return g_t->is_active() == 1; }));
    CHECK(GetState() == State::Joined && g_t->self_peer() == 2 && PeerCount() == 2);
    char name[16];
    CHECK(g_t->get_peer_name(kHostPeer, name, sizeof name) == SCO_OK && std::strcmp(name, "Hostess") == 0);
    // A joiner never sends FROM_HOST; it receives it.
    CHECK(SendStr(g_a2, "alpha.cmd", "x") == SCO_BAD_ARG);
    g_alpha.msgs.clear();
    CHECK(r.core.Send("alpha.cmd", "spawn", 5) == SendResult::Ok);
    CHECK(SendStr(g_a2, "alpha.pose", "pose") == SCO_OK);
    CHECK(lab.Until([&] { return WaitForDelivery(0) && !r.got.empty(); }));
    TickNet();
    CHECK(g_alpha.msgs.size() == 1 && g_alpha.msgs[0].from == kHostPeer && g_alpha.msgs[0].data == "spawn");
    CHECK(r.got.size() == 1 && r.got[0].from == 2 && r.got[0].data == "pose");
    // The host goes away: the session ends by itself.
    g_events.states.clear();
    r.core.Leave("host quit");
    CHECK(lab.Until([] { return GetState() == State::Idle; }));
    TickNet();
    CHECK(g_t->is_active() == 0 && !g_events.states.empty() && g_events.states.back().active == 0);

    // A wrong passphrase: refused, and the reason says so.
    Lab lab2;
    CHECK(lab2.remote.core.Host(RemoteOpts(), g_clock));
    JoinOptions w = j;
    w.transport = &lab2.svc;
    w.endpoint = lab2.other.Self();
    w.passphrase = "wrong";
    g_events.states.clear();
    CHECK(Join(w) == Result::Ok);
    CHECK(lab2.Until([] { return GetState() == State::Idle; }));
    TickNet();
    CHECK(g_t->is_active() == 0 && g_events.states.size() == 1 && g_events.states[0].active == 0);
    CHECK(!LastReason().empty());
    CHECK(g_t->unregister_channel(g_a2, "alpha.cmd") == SCO_OK);
}

// ---- a full session: 51 players through the service ----------------------------------------------

// The service hosts a session at the limit, SCO_NET_SESSION_MAX_PEERS (51) players with the host, and 50
// Cores join it over the in-memory network, all starting in the same moment. The remotes keep the default
// maxPeers: a joiner never reads the limit, so they stand for joiners built for the old limit of 16. A plugin
// that sized its get_peers buffer by SCO_NET_MAX_PEERS (still 16) gets SCO_TOO_MANY with the first 16 peers
// written and the true count; a buffer that asks first gets all 51.
static void TestFullSession() {
    constexpr uint32_t kSeats = SCO_NET_SESSION_MAX_PEERS;
    static_assert(kSeats == 51, "the session limit is 51 players");

    MemNet net(51);
    MemTransport svc(net, 1);
    MemTransport extraTr(net, static_cast<uint16_t>(kSeats + 1));
    Remote extra(extraTr);   // the player who finds the session full
    std::vector<std::unique_ptr<MemTransport>> transports;
    std::vector<std::unique_ptr<Remote>> remotes;
    for (uint32_t i = 1; i < kSeats; ++i) {
        transports.push_back(std::make_unique<MemTransport>(net, static_cast<uint16_t>(i + 1)));
        remotes.push_back(std::make_unique<Remote>(*transports.back()));
        CHECK(remotes.back()->core.RegisterChannel("fifty.say", kReliable, 64));
    }
    auto step = [&] {
        g_clock += 10;
        net.now = g_clock;
        PumpNetwork();
        for (auto& r : remotes) r->core.Pump(g_clock);
        extra.core.Pump(g_clock);
    };
    auto until = [&](const std::function<bool()>& done, int steps = 3000) {
        for (int i = 0; i < steps; ++i) {
            if (done()) return true;
            step();
        }
        return done();
    };

    sco_plugin* p = sco::host::NewPlugin("fifty");
    Got seen;
    CHECK(g_t->register_channel(p, "fifty.say", SCO_NET_RELIABLE, 64, OnMsg, &seen) == SCO_OK);
    TickNet();
    const size_t events0 = g_events.peers.size();

    HostOptions h;
    h.transport = &svc;
    h.passphrase = kPass;
    h.playerName = "Host";
    h.maxPeers = kSeats;
    CHECK(Host(h) == Result::Ok);
    for (size_t i = 0; i < remotes.size(); ++i) {
        Options o = RemoteOpts();
        // Two steps for the GCC 12 reason given in TestSessionLimit (tests/test_net.cpp).
        std::string name = "R";
        name += std::to_string(i + 1);
        o.playerName = name;
        CHECK(remotes[i]->core.Join(o, svc.Self(), g_clock));
    }
    CHECK(until([&] {
        for (auto& r : remotes)
            if (!r->core.Active()) return false;
        return PeerCount() == kSeats;
    }));
    TickNet();
    CHECK(g_events.peers.size() == events0 + (kSeats - 1));   // one "net.peer" JOINED per remote

    // get_peers: a buffer of SCO_NET_SESSION_MAX_PEERS holds everyone, one of the 1.0 size does not.
    CHECK(PeerCount() == kSeats);
    sco_net_peer all[SCO_NET_SESSION_MAX_PEERS] = {};
    uint32_t n = SCO_NET_SESSION_MAX_PEERS;
    CHECK(g_t->get_peers(all, &n) == SCO_OK && n == kSeats);
    CHECK(all[0].peer_id == kHostPeer && all[kSeats - 1].peer_id == kSeats);
    sco_net_peer old[SCO_NET_MAX_PEERS] = {};   // a plugin that sized its buffer by the 1.0 macro
    n = SCO_NET_MAX_PEERS;
    CHECK(g_t->get_peers(old, &n) == SCO_TOO_MANY && n == kSeats);
    CHECK(old[0].peer_id == kHostPeer && old[SCO_NET_MAX_PEERS - 1].peer_id == SCO_NET_MAX_PEERS);
    sco::sdk::Net wrapper;
    CHECK(wrapper.Open(g_api, p) == SCO_OK && wrapper.Peers().size() == kSeats);
    char name[16];
    CHECK(g_t->get_peer_name(kSeats, name, sizeof name) == SCO_OK && std::strcmp(name, "R50") == 0);

    // The plugin speaks: all 50 remotes hear it once, from the host.
    CHECK(SendStr(p, "fifty.say", "all") == SCO_OK);
    CHECK(until([&] {
        for (auto& r : remotes)
            if (r->got.size() != 1) return false;
        return true;
    }));
    for (auto& r : remotes) CHECK(r->got.size() == 1 && r->got[0].from == kHostPeer && r->got[0].data == "all");

    // The last remote speaks: the plugin hears it on the game thread, the 49 others get the relayed copy.
    CHECK(remotes.back()->core.Send("fifty.say", "last", 4) == SendResult::Ok);
    CHECK(until([] { return WaitForDelivery(0); }));
    TickNet();
    CHECK(seen.msgs.size() == 1 && seen.msgs[0].from == kSeats && seen.msgs[0].data == "last" && !seen.offTick);
    CHECK(until([&] {
        for (size_t i = 0; i + 1 < remotes.size(); ++i)
            if (remotes[i]->got.size() != 2) return false;
        return true;
    }));

    // One more is refused: the session is full.
    const uint64_t refused0 = GetServiceStats().core.joinsRefused;
    CHECK(extra.core.Join(RemoteOpts(), svc.Self(), g_clock));
    CHECK(until([&] { return extra.core.GetState() == State::Idle; }));
    CHECK(extra.core.LastReason() == "refused: the session is full");
    CHECK(GetServiceStats().core.joinsRefused == refused0 + 1 && PeerCount() == kSeats);

    // A freed seat can be taken again. Ids are never reused, so the newcomer is id 52 in a 51-seat session.
    remotes[9]->core.Leave("seat");
    CHECK(until([&] { return PeerCount() == kSeats - 1; }));
    CHECK(extra.core.Join(RemoteOpts(), svc.Self(), g_clock));
    CHECK(until([&] { return extra.core.Active() && PeerCount() == kSeats; }));
    CHECK(extra.core.Self() == kSeats + 1);

    Leave("done");
    CHECK(GetState() == State::Idle);
    TickNet();
    g_events.peers.clear();
    g_events.states.clear();
    CHECK(sco::Release(p) == Result::Ok);
}

// ---- an allocation fails while the held events move to the inbox ------------------------------------

#if SCO_TEST_TSAN
static void TestHeldTransferFailure() {
    std::printf("  TestHeldTransferFailure: not run, ThreadSanitizer owns operator new\n");
}
#else
namespace {
constexpr size_t kBurst = 12;   // messages the remote sends in one go

struct BurstRun {
    long     allocations = 0;     // allocations the service's step made while it took the burst in
    bool     fired = false;       // the injected failure happened
    bool     escaped = false;     // ... and PumpNetwork let it out: it was not inside the move to the inbox
    uint64_t dropped = 0;         // inboxDropped the step added
    size_t   delivered = 0;       // of the burst, how many reached the plugin at the next Tick
    bool     quotaFree = false;   // the plugin's inbox then took in a full quota of messages
};

// One run. The plugin "qt" owns an unreliable channel, a remote joins and sends kBurst messages, and the
// service takes them in with one PumpNetwork (the held messages all move to the inbox in that step's
// Snapshot). failAt > 0 makes the failAt-th allocation of that step throw std::bad_alloc; 0 only counts.
// Afterwards the remote sends kMaxInboxPerPlugin more messages with no Tick in between. They all fit only if
// nothing is still charged to the plugin's inbox quota for a message that never reached the inbox.
BurstRun RunBurst(long failAt) {
    BurstRun out;
    Lab lab;
    sco_plugin* p = sco::host::NewPlugin("qt");
    Got got;
    CHECK(g_t->register_channel(p, "qt.m", 0, 8, OnMsg, &got) == SCO_OK);
    HostOptions h;
    h.transport = &lab.svc;
    h.passphrase = kPass;
    h.playerName = "Host";
    CHECK(Host(h) == Result::Ok);
    Remote& r = lab.remote;
    CHECK(r.core.RegisterChannel("qt.m", 0, 8));
    CHECK(r.core.Join(RemoteOpts(), lab.svc.Self(), g_clock));
    CHECK(lab.Until([&] { return r.core.Active() && PeerCount() == 2; }));
    TickNet();   // an empty inbox, so the burst starts a fresh one the same way in every run
    got.msgs.clear();

    for (size_t i = 0; i < kBurst; ++i) CHECK(r.core.Send("qt.m", "m", 1) == SendResult::Ok);
    g_clock += 10;
    lab.net.now = g_clock;
    r.core.Pump(g_clock);   // the remote sends the burst
    g_clock += 10;
    lab.net.now = g_clock;  // and it has arrived
    const uint64_t droppedBefore = GetServiceStats().inboxDropped;
    allocfail::Arm(failAt);
    try {
        PumpNetwork();
    } catch (const std::bad_alloc&) {
        out.escaped = true;
    }
    out.allocations = allocfail::Disarm();
    out.fired = allocfail::t_fired;
    if (out.escaped) {
        Leave("test");
        TickNet();
        CHECK(sco::Release(p) == Result::Ok);
        return out;
    }
    out.dropped = GetServiceStats().inboxDropped - droppedBefore;
    TickNet();
    out.delivered = got.msgs.size();

    // The quota probe: a full quota of messages, none taken out by a Tick until all are in.
    got.msgs.clear();
    const uint64_t dropBase = GetServiceStats().inboxDropped;
    size_t sent = 0;
    uint64_t seen = 0;
    int steady = 0;
    CHECK(lab.Until([&] {
        while (sent < kMaxInboxPerPlugin && r.core.Send("qt.m", "m", 1) == SendResult::Ok) ++sent;
        const uint64_t in = GetServiceStats().core.datagramsIn;
        steady = sent == kMaxInboxPerPlugin && in == seen ? steady + 1 : 0;
        seen = in;
        return steady >= 5;   // everything sent, and the service has stopped receiving
    }));
    out.quotaFree = GetServiceStats().inboxDropped == dropBase;
    TickNet();
    out.quotaFree = out.quotaFree && got.msgs.size() == kMaxInboxPerPlugin;

    Leave("done");
    TickNet();
    CHECK(sco::Release(p) == Result::Ok);
    g_events.states.clear();
    g_events.stateSeen.clear();
    g_events.peers.clear();
    g_events.peerFlags.clear();
    g_events.order.clear();
    return out;
}
}  // namespace

// ReleaseHeld moves what the Core raised in a step to the inbox, and an allocation can fail half way. The
// messages that did not move are lost, and the quota they were charged must be given back, or the plugin's
// inbox shrinks for good by that much each time. The run that fails the last allocation of the step lands
// there: the last allocations of a step that took twelve messages in are the inbox's own growth.
static void TestHeldTransferFailure() {
    // Two runs without a failure. The first leaves the containers as a later run finds them; the second counts
    // the allocations of the step.
    const BurstRun warm = RunBurst(0);
    const BurstRun probe = RunBurst(0);
    const int failBefore = g_fail.load();
    CHECK(warm.delivered == kBurst && warm.dropped == 0 && warm.quotaFree);
    CHECK(probe.delivered == kBurst && probe.dropped == 0 && probe.quotaFree && probe.allocations > 0);

    // Fail one of the last allocations of the step per run, from the very last backwards, until one lands in
    // the move to the inbox: that is the one PumpNetwork does not let out.
    bool exercised = false;
    BurstRun run;
    for (long back = 0; back < 8 && !exercised; ++back) {
        run = RunBurst(probe.allocations - back);
        if (!run.fired || run.escaped) continue;
        exercised = true;
        CHECK(run.dropped == 1);                              // the failure path ran once
        CHECK(run.delivered > 0 && run.delivered < kBurst);   // part of the burst had moved, the rest did not
        CHECK(run.quotaFree);                                 // and the quota of the rest was given back
    }
    CHECK(exercised);
    if (g_fail.load() != failBefore)
        std::printf("  warm %zu/%llu/%d, probe %zu/%llu/%d/%ld allocations, last failing run %d/%d %zu/%llu/%d "
                    "(delivered/dropped/quota free)\n",
                    warm.delivered, static_cast<unsigned long long>(warm.dropped), warm.quotaFree ? 1 : 0,
                    probe.delivered, static_cast<unsigned long long>(probe.dropped), probe.quotaFree ? 1 : 0,
                    probe.allocations, run.fired ? 1 : 0, run.escaped ? 1 : 0, run.delivered,
                    static_cast<unsigned long long>(run.dropped), run.quotaFree ? 1 : 0);
}
#endif   // SCO_TEST_TSAN

// ---- the LAN rule ---------------------------------------------------------------------------------

namespace {
// Hands out queued datagrams from given addresses; records what was sent.
struct FakeTransport final : Transport {
    std::vector<Endpoint> incoming;
    std::vector<Endpoint> sent;
    bool Send(const Endpoint& to, const uint8_t*, size_t) override {
        sent.push_back(to);
        return true;
    }
    bool Receive(Endpoint* from, uint8_t*, size_t, size_t* len) override {
        if (incoming.empty()) return false;
        *from = incoming.front();
        incoming.erase(incoming.begin());
        *len = 1;
        return true;
    }
};
Endpoint V4(const char* s, uint16_t port = 1) {
    Endpoint e;
    ParseAddress(s, port, &e);
    return e;
}
Endpoint V6(const char* s) {
    Cidr c;
    Endpoint e;
    if (ParseCidr(s, &c) && c.family == Endpoint::kIpv6) {
        e.family = Endpoint::kIpv6;
        std::memcpy(e.addr, c.addr, 16);
    }
    return e;
}
}  // namespace

static void TestScope() {
    Endpoint e;
    CHECK(ParseAddress("192.168.1.20", 64091, &e) && e.family == Endpoint::kIpv4 && e.port == 64091 && e.addr[0] == 192 &&
          e.addr[3] == 20);
    CHECK(!ParseAddress("192.168.1", 1, &e) && !ParseAddress("192.168.1.256", 1, &e) && !ParseAddress("01.2.3.4", 1, &e) &&
          !ParseAddress("1.2.3.4.5", 1, &e) && !ParseAddress("", 1, &e) && !ParseAddress("host.lan", 1, &e) &&
          !ParseAddress("1.2.3.4 ", 1, &e));
    for (const char* lan : { "127.0.0.1", "10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.0.1", "169.254.9.9" })
        CHECK(IsLan(V4(lan)));
    for (const char* wan : { "8.8.8.8", "172.32.0.1", "172.15.255.255", "100.64.0.1", "192.169.0.1", "11.0.0.1" })
        CHECK(!IsLan(V4(wan)));
    CHECK(IsLan(V6("::1")) && IsLan(V6("fe80::1")) && IsLan(V6("fd12:3456::1")) && !IsLan(V6("2001:db8::1")));

    Cidr c;
    CHECK(ParseCidr("100.64.0.0/10", &c) && c.family == Endpoint::kIpv4 && c.bits == 10);
    CHECK(!ParseCidr("100.64.0.1/10", &c));   // host bits set
    CHECK(!ParseCidr("100.64.0.0/33", &c) && !ParseCidr("100.64.0.0/", &c) && !ParseCidr("/8", &c) && !ParseCidr("x/8", &c));
    CHECK(ParseCidr("fd00::/8", &c) && c.family == Endpoint::kIpv6 && c.bits == 8);
    CHECK(ParseCidr("2001:db8::5", &c) && c.bits == 128);
    CHECK(!ParseCidr("1:2:3", &c) && !ParseCidr("1::2::3", &c) && !ParseCidr("12345::", &c) && !ParseCidr("fd00::/129", &c));

    Scope lan;
    CHECK(InScope(lan, V4("192.168.4.4")) && !InScope(lan, V4("100.101.102.103")) && !InScope(lan, V4("8.8.8.8")));
    CHECK(InScope(lan, nettest::MemEndpoint(3)) && !InScope(lan, Endpoint()));
    Scope vpn;
    CHECK(ParseCidr("100.64.0.0/10", &c));
    vpn.allow.push_back(c);
    CHECK(InScope(vpn, V4("100.101.102.103")) && InScope(vpn, V4("10.0.0.1")) && !InScope(vpn, V4("100.128.0.1")) &&
          !InScope(vpn, V4("8.8.8.8")));
    Scope any;
    any.any = true;
    CHECK(InScope(any, V4("8.8.8.8")));

    // The transport wrapper drops out-of-scope sources before the session sees them, and refuses
    // to send there.
    FakeTransport inner;
    inner.incoming = { V4("8.8.8.8"), V4("192.168.1.2"), V4("1.1.1.1"), V4("100.64.1.1") };
    ScopedTransport scoped(inner, lan);
    Endpoint from;
    uint8_t buf[4];
    size_t len = 0;
    CHECK(scoped.Receive(&from, buf, sizeof buf, &len) && from == V4("192.168.1.2"));
    CHECK(!scoped.Receive(&from, buf, sizeof buf, &len) && scoped.Dropped() == 3);
    CHECK(!scoped.Send(V4("8.8.8.8"), buf, 1) && scoped.Send(V4("10.0.0.9"), buf, 1) && scoped.Refused() == 1 &&
          inner.sent.size() == 1);
}

// ---- real UDP: two Cores over loopback, one process ---------------------------------------------

static void TestUdpCores() {
    std::string why;
    auto sa = udp::Socket::Open(0, true, &why);
    auto sb = udp::Socket::Open(0, true, &why);
    CHECK(sa && sb);
    if (!sa || !sb) {
        std::printf("  (no UDP sockets: %s)\n", why.c_str());
        return;
    }
    CHECK(sa->Port() != 0 && sb->Port() != 0 && sa->Port() != sb->Port());
    // Datagrams both ways, and a cut one is reported longer than the buffer.
    Endpoint toB;
    CHECK(ParseAddress("127.0.0.1", sb->Port(), &toB));
    const uint8_t ping[3] = { 1, 2, 3 };
    CHECK(sa->Send(toB, ping, sizeof ping));
    CHECK(sb->Wait(5000));
    Endpoint from;
    uint8_t buf[2048];
    size_t len = 0;
    CHECK(sb->Receive(&from, buf, sizeof buf, &len) && len == 3 && from.port == sa->Port() && from.addr[0] == 127);
    CHECK(!sb->Receive(&from, buf, sizeof buf, &len));   // nothing more waiting
    const std::vector<uint8_t> blob(100, 7);
    CHECK(sa->Send(toB, blob.data(), blob.size()));
    CHECK(sb->Wait(5000));
    CHECK(sb->Receive(&from, buf, 10, &len) && len > 10);

    Remote host(*sa), joiner(*sb);
    Options ho = RemoteOpts();
    ho.playerName = "UdpHost";
    CHECK(host.core.RegisterChannel("udp.data", kReliable, 8192) && joiner.core.RegisterChannel("udp.data", kReliable, 8192));
    CHECK(host.core.Host(ho, SteadyMs()));
    Endpoint hostEp;
    CHECK(ParseAddress("127.0.0.1", sa->Port(), &hostEp));
    CHECK(joiner.core.Join(RemoteOpts(), hostEp, SteadyMs()));
    auto until = [&](const std::function<bool()>& done) {
        const uint64_t deadline = SteadyMs() + 10000;
        while (!done() && SteadyMs() < deadline) {
            host.core.Pump(SteadyMs());
            joiner.core.Pump(SteadyMs());
            if (!sa->Wait(5)) sb->Wait(5);   // bounded waits for the next datagram
        }
        return done();
    };
    CHECK(until([&] { return joiner.core.GetState() == State::Joined && joiner.core.Active() && host.core.Peers().size() == 2; }));
    std::string data(5000, 'u');
    CHECK(joiner.core.Send("udp.data", data.data(), static_cast<uint32_t>(data.size())) == SendResult::Ok);
    CHECK(until([&] { return !host.got.empty(); }));
    CHECK(host.got.size() == 1 && host.got[0].from == 2 && host.got[0].data == data);
    joiner.core.Leave("done");
    CHECK(until([&] { return host.core.Peers().size() == 1; }));
}

// ---- the service with its network thread, over UDP loopback -------------------------------------

static void TestThreadedService() {
    ServiceOptions so;
    so.pbkdf2Iters = kIters;
    CHECK(Start(so) == Result::Ok && Started());
    sco_plugin* p = sco::host::NewPlugin("live");
    Got got;
    CHECK(g_t->register_channel(p, "live.pose", 0, 64, OnMsg, &got) == SCO_OK);
    HostOptions h;
    h.port = 0;
    h.loopbackOnly = true;
    h.passphrase = kPass;
    h.playerName = "Host";
    CHECK(Host(h) == Result::Ok);
    const uint16_t port = BoundPort();
    CHECK(port != 0);

    std::string why;
    auto sock = udp::Socket::Open(0, true, &why);
    CHECK(sock != nullptr);
    if (!sock) {
        Leave("no socket");
        Stop();
        CHECK(sco::Release(p) == Result::Ok);
        return;
    }
    Remote r(*sock);
    CHECK(r.core.RegisterChannel("live.pose", 0, 64));
    Endpoint ep;
    CHECK(ParseAddress("127.0.0.1", port, &ep));
    CHECK(r.core.Join(RemoteOpts(), ep, SteadyMs()));
    auto until = [&](const std::function<bool()>& done) {
        const uint64_t deadline = SteadyMs() + 10000;
        while (!done() && SteadyMs() < deadline) {
            r.core.Pump(SteadyMs());
            if (!sock->Wait(5)) WaitForDelivery(5);   // bounded waits for a datagram or a delivery
            TickNet();
        }
        return done();
    };
    CHECK(until([&] { return r.core.Active() && g_t->is_active() == 1 && PeerCount() == 2; }));
    CHECK(SendStr(p, "live.pose", "from host") == SCO_OK);
    CHECK(until([&] { return !r.got.empty(); }));
    CHECK(!r.got.empty() && r.got[0].data == "from host" && r.got[0].from == kHostPeer);
    CHECK(r.core.Send("live.pose", "from joiner", 11) == SendResult::Ok);
    CHECK(until([&] { return !got.msgs.empty(); }));
    CHECK(got.msgs.size() == 1 && got.msgs[0].from == 2 && got.msgs[0].data == "from joiner" && !got.offTick);

    // Table calls from 8 threads while the network thread runs: each answers OK or the quota.
    std::atomic<int> ok{ 0 }, tooMany{ 0 }, other{ 0 };
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 40; ++i) {
                const std::string s = "t" + std::to_string(t) + "." + std::to_string(i);
                const sco_result res = SendStr(p, "live.pose", s);
                if (res == SCO_OK) ++ok;
                else if (res == SCO_TOO_MANY) ++tooMany;
                else ++other;
                uint32_t n = 0;
                g_t->get_peers(nullptr, &n);
                char name[8];
                g_t->get_peer_name(2, name, sizeof name);
                (void)g_t->is_active();
            }
        });
    }
    for (auto& t : threads) t.join();
    CHECK(other.load() == 0 && ok.load() + tooMany.load() == 320 && ok.load() >= 1);
    CHECK(until([&] { return r.got.size() >= 2; }));

    Leave("done");
    CHECK(GetState() == State::Idle && BoundPort() == 0 && g_t->is_active() == 0);
    TickNet();
    Stop();
    CHECK(!Started());
    CHECK(sco::Release(p) == Result::Ok);
}

// ---- servers: the role of the host, read through the table ------------------------------------------------

// A remote Core hosts as a server and the service joins it. The role is readable from the table, from the
// wrapper and from inside the "net.peer" handler of the host's JOINED event (OnEvent records what the table
// answered there). A player host marks no one, and neither does the service when it hosts.
static void TestServerFlags() {
    const void* table = nullptr;
    CHECK(g_api->query_service(SCO_NET_NAME, SCO_NET_VERSION_1_1, &table) == SCO_OK && table == g_t);
    CHECK(g_api->query_service(SCO_NET_NAME, SCO_NET_VERSION_1_0, &table) == SCO_OK && table == g_t);
    CHECK(g_api->query_service(SCO_NET_NAME, 0x00010002u, &table) == SCO_UNAVAILABLE);
    CHECK(g_t->size == sizeof(sco_net_v1) && g_t->size == 72);
    uint32_t flags = 99;
    CHECK(g_t->get_peer_flags(kHostPeer, &flags) == SCO_NOT_FOUND && flags == 0);   // no session yet
    CHECK(g_t->get_peer_flags(kHostPeer, nullptr) == SCO_BAD_ARG);

    {
        Lab lab;
        Options ho = RemoteOpts();
        ho.playerName = "Relay";
        ho.server = true;
        CHECK(lab.remote.core.Host(ho, g_clock));
        JoinOptions j;
        j.transport = &lab.svc;
        j.endpoint = lab.other.Self();
        j.passphrase = kPass;
        j.playerName = "Me";
        g_events.states.clear();
        g_events.stateSeen.clear();
        g_events.peers.clear();
        g_events.peerFlags.clear();
        CHECK(Join(j) == Result::Ok);
        CHECK(lab.Until([] { return g_t->is_active() == 1; }));
        TickNet();
        CHECK(g_events.peers.size() == 1 && g_events.peers[0].what == SCO_NET_PEER_JOINED &&
              g_events.peers[0].peer.peer_id == kHostPeer);
        CHECK(g_events.peerFlags.size() == 1 && g_events.peerFlags[0].first == SCO_OK &&
              g_events.peerFlags[0].second == SCO_NET_PEER_F_SERVER);
        CHECK(g_events.stateSeen.size() == 1 && g_events.stateSeen[0].first == 1 && g_events.stateSeen[0].second == 1);
        CHECK(g_t->get_peer_flags(kHostPeer, &flags) == SCO_OK && flags == SCO_NET_PEER_F_SERVER);
        CHECK(g_t->self_peer() == 2 && g_t->get_peer_flags(2, &flags) == SCO_OK && flags == 0);
        flags = 99;
        CHECK(g_t->get_peer_flags(9, &flags) == SCO_NOT_FOUND && flags == 0);
        CHECK(PeerCount() == 2);   // get_peers still lists the server: it is a member of the session
        sco::sdk::Net net;
        CHECK(net.Open(g_api, g_a2) == SCO_OK);
        uint32_t seen = 99;
        bool isServer = false;
        CHECK(net.PeerFlags(kHostPeer, seen) == SCO_OK && seen == SCO_NET_PEER_F_SERVER);
        CHECK(net.IsServer(kHostPeer, isServer) == SCO_OK && isServer);
        CHECK(net.IsServer(net.SelfPeer(), isServer) == SCO_OK && !isServer);
        seen = 99;
        CHECK(net.PeerFlags(9, seen) == SCO_NOT_FOUND && seen == 0);   // not known is not "a player"
        CHECK(net.IsServer(9, isServer) == SCO_NOT_FOUND && !isServer);
        lab.remote.core.Leave("done");
        CHECK(lab.Until([] { return GetState() == State::Idle; }));
        TickNet();
        flags = 99;
        CHECK(g_t->get_peer_flags(kHostPeer, &flags) == SCO_NOT_FOUND && flags == 0);   // the session is over
    }
    {
        // A player host, as every 2.2.0 host is, marks no one.
        Lab lab;
        CHECK(lab.remote.core.Host(RemoteOpts(), g_clock));
        JoinOptions j;
        j.transport = &lab.svc;
        j.endpoint = lab.other.Self();
        j.passphrase = kPass;
        j.playerName = "Me";
        CHECK(Join(j) == Result::Ok);
        CHECK(lab.Until([] { return g_t->is_active() == 1; }));
        sco::sdk::Net net;
        CHECK(net.Open(g_api, g_a2) == SCO_OK);
        CHECK(g_t->get_peer_flags(kHostPeer, &flags) == SCO_OK && flags == 0);
        uint32_t seen = 99;
        bool isServer = true;
        CHECK(net.PeerFlags(kHostPeer, seen) == SCO_OK && seen == 0);
        CHECK(net.IsServer(kHostPeer, isServer) == SCO_OK && !isServer);
        Leave("done");
        CHECK(GetState() == State::Idle);
    }
    {
        // The service as the host is a player, and so is every joiner.
        Lab lab;
        HostOptions h;
        h.transport = &lab.svc;
        h.passphrase = kPass;
        h.playerName = "Host";
        CHECK(Host(h) == Result::Ok);
        CHECK(lab.remote.core.Join(RemoteOpts(), lab.svc.Self(), g_clock));
        CHECK(lab.Until([&] { return lab.remote.core.Active() && PeerCount() == 2; }));
        flags = 99;
        CHECK(g_t->get_peer_flags(kHostPeer, &flags) == SCO_OK && flags == 0);
        flags = 99;
        CHECK(g_t->get_peer_flags(2, &flags) == SCO_OK && flags == 0);
        Leave("done");
        CHECK(GetState() == State::Idle);
    }
}

// ---- the threaded service: an event never outruns the session it reports -----------------------------------

// Wraps the service's transport and parks the network thread at one chosen moment: on the Receive call that
// follows the second control-channel DATA datagram the joiner gets. For a server that is HOST_ROLE and then
// SYNC, so the Core has just raised the host's JOINED event and the step has not reached its Snapshot. That
// is the schedule in which the game thread's Tick used to deliver the event before the session was
// published, and get_peer_flags(1) answered SCO_NOT_FOUND in the handler.
class GateTransport : public Transport {
public:
    explicit GateTransport(Transport& inner) : inner_(inner) {}

    bool Send(const Endpoint& to, const uint8_t* data, size_t len) override { return inner_.Send(to, data, len); }

    bool Receive(Endpoint* from, uint8_t* buf, size_t cap, size_t* len) override {
        if (parkNext_) {
            parkNext_ = false;
            if (throwAtGate_) throw std::bad_alloc();   // the failure the network thread's catch is for
            Park();
        }
        const bool got = inner_.Receive(from, buf, cap, len);
        // Kind DATA on channel index 0, the control channel; the header is in the clear.
        if (got && armed_.load() && *len >= SC_NET_HEADER_BYTES && buf[SC_NET_OFF_KIND] == SC_NET_DATA &&
            buf[SC_NET_OFF_CHANNEL] == 0 && buf[SC_NET_OFF_CHANNEL + 1] == 0 && ++controlSeen_ == 2)
            parkNext_ = true;
        return got;
    }

    void Arm() { armed_ = true; }
    // At the gate, fail with std::bad_alloc instead of parking.
    void ThrowAtGate() { throwAtGate_ = true; }
    bool WaitParked(uint32_t ms) {
        std::unique_lock<std::mutex> hold(m_);
        return cv_.wait_for(hold, std::chrono::milliseconds(ms), [this] { return parked_; });
    }
    void Release() {
        std::lock_guard<std::mutex> hold(m_);
        release_ = true;
        cv_.notify_all();
    }

private:
    void Park() {
        std::unique_lock<std::mutex> hold(m_);
        parked_ = true;
        cv_.notify_all();
        cv_.wait_for(hold, std::chrono::seconds(10), [this] { return release_; });   // bounded: a failed test must not hang
        parked_ = false;
    }

    Transport&              inner_;
    std::atomic<bool>       armed_{ false };
    bool                    parkNext_ = false;    // network thread only
    bool                    throwAtGate_ = false; // set before the session starts
    int                     controlSeen_ = 0;     // network thread only
    std::mutex              m_;
    std::condition_variable cv_;
    bool                    parked_ = false, release_ = false;
};

// The service, with its network thread, joins a server over UDP loopback. The network thread is parked right
// after it handled SYNC and before it published the session; the game thread ticks while it is parked and
// must see nothing yet. Once the thread goes on, the JOINED handler must find the host and read the server
// flag, and the state handler must find the session active.
static void TestThreadedPeerSnapshot() {
    ServiceOptions so;
    so.pbkdf2Iters = kIters;
    CHECK(Start(so) == Result::Ok && Started());
    sco_plugin* p = sco::host::NewPlugin("gate");
    std::string why;
    auto hostSock = udp::Socket::Open(0, true, &why);
    auto joinSock = udp::Socket::Open(0, true, &why);
    CHECK(hostSock != nullptr && joinSock != nullptr);
    if (!hostSock || !joinSock) {
        Stop();
        CHECK(sco::Release(p) == Result::Ok);
        return;
    }

    Remote host(*hostSock);
    Options ho = RemoteOpts();
    ho.playerName = "Relay";
    ho.server = true;
    CHECK(host.core.Host(ho, SteadyMs()));
    Endpoint hostEp;
    CHECK(ParseAddress("127.0.0.1", hostSock->Port(), &hostEp));

    GateTransport gate(*joinSock);
    JoinOptions j;
    j.transport = &gate;
    j.endpoint = hostEp;
    j.passphrase = kPass;
    j.playerName = "Me";
    g_events.states.clear();
    g_events.stateSeen.clear();
    g_events.peers.clear();
    g_events.peerFlags.clear();
    gate.Arm();
    CHECK(Join(j) == Result::Ok);

    // The remote host runs on this thread, the service's network thread on its own.
    auto pumpUntil = [&](const std::function<bool()>& done) {
        const uint64_t deadline = SteadyMs() + 10000;
        while (!done() && SteadyMs() < deadline) {
            host.core.Pump(SteadyMs());
            hostSock->Wait(5);
        }
        return done();
    };
    CHECK(pumpUntil([&] { return gate.WaitParked(0); }));

    // Parked between the Core's JOINED and the snapshot: the game thread has nothing to deliver yet.
    TickNet();
    CHECK(g_events.peers.empty() && g_events.states.empty());
    CHECK(g_t->is_active() == 0 && PeerCount() == 0);

    gate.Release();
    CHECK(pumpUntil([&] {
        TickNet();
        return !g_events.peers.empty();
    }));
    CHECK(g_events.states.size() == 1 && g_events.states[0].active == 1);
    CHECK(g_events.stateSeen.size() == 1 && g_events.stateSeen[0].first == 1 && g_events.stateSeen[0].second == 1);
    CHECK(g_events.peers.size() == 1 && g_events.peers[0].what == SCO_NET_PEER_JOINED &&
          g_events.peers[0].peer.peer_id == kHostPeer);
    CHECK(g_events.peerFlags.size() == 1 && g_events.peerFlags[0].first == SCO_OK &&
          g_events.peerFlags[0].second == SCO_NET_PEER_F_SERVER);

    sco::sdk::Net net;
    CHECK(net.Open(g_api, p) == SCO_OK);
    bool isServer = false;
    CHECK(net.IsServer(kHostPeer, isServer) == SCO_OK && isServer);
    CHECK(PeerCount() == 2);

    Leave("done");
    host.core.Leave("done");
    TickNet();
    Stop();
    CHECK(!Started());
    CHECK(sco::Release(p) == Result::Ok);
}

// ---- the network thread fails: "inactive" stays the last event ---------------------------------------------

// The network thread's Receive throws right after it handled SYNC (the gate of GateTransport). The Core has
// raised the host's JOINED and "active" in that step, and the step never reaches its Snapshot, so both are
// still held when Run's catch queues the terminal "inactive". They must reach the game thread before it, not
// after it: a plugin that was told the session is over must not be told next that it is up.
static void TestThreadFailure() {
    ServiceOptions so;
    so.pbkdf2Iters = kIters;
    CHECK(Start(so) == Result::Ok && Started());
    sco_plugin* p = sco::host::NewPlugin("boom");
    std::string why;
    auto hostSock = udp::Socket::Open(0, true, &why);
    auto joinSock = udp::Socket::Open(0, true, &why);
    CHECK(hostSock != nullptr && joinSock != nullptr);
    if (!hostSock || !joinSock) {
        Stop();
        CHECK(sco::Release(p) == Result::Ok);
        return;
    }

    Remote host(*hostSock);
    Options ho = RemoteOpts();
    ho.playerName = "Relay";
    ho.server = true;
    CHECK(host.core.Host(ho, SteadyMs()));
    Endpoint hostEp;
    CHECK(ParseAddress("127.0.0.1", hostSock->Port(), &hostEp));

    GateTransport gate(*joinSock);
    gate.ThrowAtGate();
    JoinOptions j;
    j.transport = &gate;
    j.endpoint = hostEp;
    j.passphrase = kPass;
    j.playerName = "Me";
    g_events.states.clear();
    g_events.stateSeen.clear();
    g_events.peers.clear();
    g_events.peerFlags.clear();
    g_events.order.clear();
    gate.Arm();
    CHECK(Join(j) == Result::Ok);

    // The remote host runs on this thread; the game thread ticks until the network thread has ended the session.
    const uint64_t deadline = SteadyMs() + 10000;
    while (GetState() != State::Idle && SteadyMs() < deadline) {
        host.core.Pump(SteadyMs());
        hostSock->Wait(5);
        TickNet();
    }
    CHECK(GetState() == State::Idle);
    TickNet();

    const std::string& order = g_events.order;
    CHECK(order.size() == 3 && order.back() == 'I');   // JOINED and "active" in the Core's order, then "inactive"
    CHECK(order.find('I') == order.size() - 1);
    CHECK(order.find('J') != std::string::npos && order.find('A') != std::string::npos);
    CHECK(LastReason() == "out of memory");
    CHECK(g_t->is_active() == 0 && PeerCount() == 0);

    host.core.Leave("done");
    Stop();
    CHECK(!Started());
    CHECK(sco::Release(p) == Result::Ok);
    g_events.order.clear();
}

static void Run(const char* name, void (*fn)()) {
    const int before = g_fail.load();
    fn();
    std::printf("%s %s\n", g_fail.load() == before ? "ok  " : "FAIL", name);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    sco::SetGameThread();
    g_api = sco::host::BuildApi({ "test_net_service" });
    CHECK(g_api != nullptr);

    ServiceOptions bad;
    bad.quotaMsgs = 0;
    CHECK(Start(bad) == Result::BadArg && !Started());
    bad = ServiceOptions();
    bad.quotaBytes = SCO_NET_MAX_RELIABLE - 1;
    CHECK(Start(bad) == Result::BadArg);
    CHECK(!sco::caps::Has("sco.net"));
    const void* none = nullptr;
    CHECK(g_api->query_service(SCO_NET_NAME, SCO_NET_VERSION_1_0, &none) == SCO_NOT_FOUND);
    HostOptions early;
    early.passphrase = kPass;
    early.playerName = "Host";
    CHECK(Host(early) == Result::Unavailable);

    ServiceOptions so;
    so.thread = false;
    so.clock = [] { return g_clock.load(); };
    so.pbkdf2Iters = kIters;
    CHECK(Start(so) == Result::Ok && Started());
    CHECK(Start(so) == Result::BadArg);
    g_t = Table();
    CHECK(sco::Subscribe(&g_eventsOwner, SCO_NET_EVENT_STATE, OnEvent, nullptr) == Result::Ok);
    CHECK(sco::Subscribe(&g_eventsOwner, SCO_NET_EVENT_PEER, OnEvent, nullptr) == Result::Ok);

    Run("TestTable", TestTable);
    Run("TestOwnership", TestOwnership);
    {
        Lab lab;
        const int before = g_fail.load();
        TestHostSession(lab);
        TestQuotas(lab);
        TestReleaseOnUnload(lab);
        TestLeave(lab);
        std::printf("%s TestHostSession/Quotas/ReleaseOnUnload/Leave\n", g_fail.load() == before ? "ok  " : "FAIL");
    }
    Run("TestJoinSession", TestJoinSession);
    Run("TestServerFlags", TestServerFlags);
    Run("TestFullSession", TestFullSession);
    Run("TestHeldTransferFailure", TestHeldTransferFailure);
    Run("TestScope", TestScope);
    Run("TestUdpCores", TestUdpCores);

    Stop();
    CHECK(!Started() && !sco::caps::Has("sco.net"));
    CHECK(g_api->query_service(SCO_NET_NAME, SCO_NET_VERSION_1_0, &none) == SCO_NOT_FOUND);
    CHECK(g_t->is_active() == 0 && SendStr(g_a2, "alpha.pose", "x") == SCO_UNAVAILABLE);
    CHECK(g_t->register_channel(g_a2, "alpha.x", 0, 8, OnMsg, &g_alpha) == SCO_UNAVAILABLE);
    Stop();   // twice: a no-op

    Run("TestThreadedService", TestThreadedService);
    Run("TestThreadedPeerSnapshot", TestThreadedPeerSnapshot);
    Run("TestThreadFailure", TestThreadFailure);

    CHECK(sco::Release(g_a2) == Result::Ok && sco::Release(g_b) == Result::Ok);
    std::printf("sco-core net service tests: %d passed, %d failed\n", g_pass.load(), g_fail.load());
    return g_fail.load() ? 1 : 0;
}
