// Unit tests for the platform-free core of sco.net (sco/net/sha2.h, wire.h, reliable.h, core.h):
// the crypto against the published vectors, the wire parser against hostile input, the replay
// window, the reliable streams, and whole sessions of several Cores in one process over the seeded
// in-memory network of tests/net_mem.h. Time is a virtual clock the test advances: nothing sleeps
// or reads a clock, so every run with a seed is the same run.
//   test_net                     (tools/test.sh, CTest test_net)
#include "sc_net.h"
#include "sco/net/core.h"
#include "sco/net/reliable.h"
#include "sco/net/sha2.h"
#include "sco/net/wire.h"
#include "net_mem.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace sco::net;
using nettest::MemEndpoint;
using nettest::MemNet;
using nettest::MemTransport;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- helpers ------------------------------------------------------------------------------------

static std::vector<uint8_t> Bytes(const char* s) { return std::vector<uint8_t>(s, s + std::strlen(s)); }

static std::string Hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

static std::string Sha(const std::vector<uint8_t>& m) {
    uint8_t out[32];
    sco_sha256_digest(m.data(), m.size(), out);
    return Hex(out, 32);
}

static std::string Hmac(const std::vector<uint8_t>& k, const std::vector<uint8_t>& m, size_t n = 32) {
    uint8_t out[32];
    sco_hmac_sha256_mac(k.data(), k.size(), m.data(), m.size(), out);
    return Hex(out, n);
}

static std::string Pbkdf2(const std::vector<uint8_t>& p, const std::vector<uint8_t>& s, uint32_t c, size_t n) {
    std::vector<uint8_t> out(n);
    if (!sco_pbkdf2_hmac_sha256(p.data(), p.size(), s.data(), s.size(), c, out.data(), n)) return "failed";
    return Hex(out.data(), n);
}

// ---- crypto -------------------------------------------------------------------------------------

static void TestSha256() {
    // FIPS 180-4 examples (NIST CSRC "SHA256.pdf", "SHA2_Additional.pdf").
    CHECK(Sha(Bytes("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha(Bytes("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha(Bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(Sha(Bytes("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu")) ==
          "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    // One million 'a', fed in uneven pieces so every buffering path runs.
    const std::vector<uint8_t> a(1000000, 'a');
    CHECK(Sha(a) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    sco_sha256 c;
    sco_sha256_init(&c);
    const size_t pieces[] = { 1, 63, 64, 65, 127, 3, 1000 };
    size_t off = 0, k = 0;
    while (off < a.size()) {
        size_t n = pieces[k++ % 7];
        if (n > a.size() - off) n = a.size() - off;
        sco_sha256_update(&c, a.data() + off, n);
        off += n;
    }
    uint8_t out[32];
    sco_sha256_final(&c, out);
    CHECK(Hex(out, 32) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // Lengths around the padding boundary (55, 56, 63, 64 bytes) agree between one call and bytewise.
    for (size_t len : { 55u, 56u, 57u, 63u, 64u, 65u, 119u, 120u }) {
        std::vector<uint8_t> m(len);
        for (size_t i = 0; i < len; ++i) m[i] = static_cast<uint8_t>(i * 7 + 1);
        sco_sha256_init(&c);
        for (uint8_t b : m) sco_sha256_update(&c, &b, 1);
        sco_sha256_final(&c, out);
        CHECK(Hex(out, 32) == Sha(m));
    }
}

static void TestHmac() {
    // RFC 4231 section 4, test cases 1-7.
    CHECK(Hmac(std::vector<uint8_t>(20, 0x0b), Bytes("Hi There")) ==
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    CHECK(Hmac(Bytes("Jefe"), Bytes("what do ya want for nothing?")) ==
          "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    CHECK(Hmac(std::vector<uint8_t>(20, 0xaa), std::vector<uint8_t>(50, 0xdd)) ==
          "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    std::vector<uint8_t> k4(25);
    for (size_t i = 0; i < 25; ++i) k4[i] = static_cast<uint8_t>(i + 1);
    CHECK(Hmac(k4, std::vector<uint8_t>(50, 0xcd)) ==
          "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
    CHECK(Hmac(std::vector<uint8_t>(20, 0x0c), Bytes("Test With Truncation"), 16) == "a3b6167473100ee06e0c796c2955552b");
    CHECK(Hmac(std::vector<uint8_t>(131, 0xaa), Bytes("Test Using Larger Than Block-Size Key - Hash Key First")) ==
          "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    CHECK(Hmac(std::vector<uint8_t>(131, 0xaa),
               Bytes("This is a test using a larger than block-size key and a larger than block-size data. The key "
                     "needs to be hashed before being used by the HMAC algorithm.")) ==
          "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
    // Incremental updates equal one call.
    sco_hmac_sha256 c;
    sco_hmac_sha256_init(&c, "Jefe", 4);
    sco_hmac_sha256_update(&c, "what do ya ", 11);
    sco_hmac_sha256_update(&c, "want for nothing?", 17);
    uint8_t out[32];
    sco_hmac_sha256_final(&c, out);
    CHECK(Hex(out, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

static void TestPbkdf2() {
    // RFC 7914 section 11 (PBKDF2-HMAC-SHA256).
    CHECK(Pbkdf2(Bytes("passwd"), Bytes("salt"), 1, 64) ==
          "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
          "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
    CHECK(Pbkdf2(Bytes("Password"), Bytes("NaCl"), 80000, 64) ==
          "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
          "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d");
    // The RFC 6070 inputs with SHA-256 (the widely published PBKDF2-HMAC-SHA256 vectors).
    CHECK(Pbkdf2(Bytes("password"), Bytes("salt"), 1, 32) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK(Pbkdf2(Bytes("password"), Bytes("salt"), 2, 32) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    CHECK(Pbkdf2(Bytes("password"), Bytes("salt"), 4096, 32) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    CHECK(Pbkdf2(Bytes("passwordPASSWORDpassword"), Bytes("saltSALTsaltSALTsaltSALTsaltSALTsalt"), 4096, 40) ==
          "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9");
    const std::vector<uint8_t> p7{ 'p', 'a', 's', 's', 0, 'w', 'o', 'r', 'd' }, s7{ 's', 'a', 0, 'l', 't' };
    CHECK(Pbkdf2(p7, s7, 4096, 16) == "89b69d0516f829893c696226650a8687");
    uint8_t out[32];
    CHECK(sco_pbkdf2_hmac_sha256("p", 1, "s", 1, 0, out, 32) == 0);
    CHECK(sco_pbkdf2_hmac_sha256("p", 1, "s", 1, 1, out, 0) == 0);
    CHECK(sco_pbkdf2_hmac_sha256("p", 1, "s", 1, 1, nullptr, 32) == 0);
    CHECK(kPbkdf2Iters == 200000u);
}

static void TestCtEqual() {
    uint8_t a[16] = {}, b[16] = {};
    CHECK(sco_ct_equal(a, b, 16) == 1);
    CHECK(sco_ct_equal(a, b, 0) == 1);
    for (int i = 0; i < 16; ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            b[i] = static_cast<uint8_t>(1u << bit);
            CHECK(sco_ct_equal(a, b, 16) == 0);
            b[i] = 0;
        }
    }
    b[15] = 0xFF;
    CHECK(sco_ct_equal(a, b, 15) == 1);
    CHECK(sco_ct_equal(a, b, 16) == 0);
}

// ---- wire ---------------------------------------------------------------------------------------

static MacKey KeyOf(uint8_t fill) {
    uint8_t k[kKeyBytes];
    std::memset(k, fill, sizeof(k));
    MacKey m;
    m.Set(k);
    return m;
}

static std::vector<uint8_t> MakeData(const MacKey& key, const char* fqn, uint16_t ch, uint64_t sender, uint64_t seq,
                                     const std::vector<uint8_t>& body) {
    Header h;
    h.kind = Kind::Data;
    h.channel = ch;
    h.sender = sender;
    h.seq = seq;
    std::vector<uint8_t> out(kMaxDatagram);
    const size_t n = Encode(h, body.data(), static_cast<uint32_t>(body.size()), &key, fqn, out.data(), out.size());
    out.resize(n);
    return out;
}

static void TestWire() {
    const MacKey key = KeyOf(0x42);
    std::vector<uint8_t> body(100);
    for (size_t i = 0; i < body.size(); ++i) body[i] = static_cast<uint8_t>(i);
    const std::vector<uint8_t> pkt = MakeData(key, "game.pose", 1, 7, 99, body);
    CHECK(pkt.size() == kHeaderBytes + 100 + kTagBytes);
    CHECK(pkt[0] == 'S' && pkt[1] == 'C' && pkt[2] == 'O' && pkt[3] == 'N');

    Packet p;
    CHECK(Parse(pkt.data(), pkt.size(), &p) == ParseError::Ok);
    CHECK(p.h.version == kProtocolVersion && p.h.kind == Kind::Data && p.h.channel == 1 && p.h.sender == 7 &&
          p.h.seq == 99 && p.h.bodyLen == 100);
    CHECK(p.body && std::memcmp(p.body, body.data(), 100) == 0 && p.tag == pkt.data() + kHeaderBytes + 100);
    CHECK(VerifyTag(key, "game.pose", p));
    CHECK(!VerifyTag(key, "game.spawn", p));   // the tag binds the channel's full name
    CHECK(!VerifyTag(KeyOf(0x43), "game.pose", p));

    // Every truncation, and one byte too many, refused.
    for (size_t n = 0; n < pkt.size(); ++n) {
        std::vector<uint8_t> cut(pkt.begin(), pkt.begin() + static_cast<std::ptrdiff_t>(n));
        const ParseError e = Parse(cut.data(), cut.size(), &p);
        CHECK(e == (n < kHeaderBytes ? ParseError::Short : ParseError::Truncated));
    }
    std::vector<uint8_t> longer = pkt;
    longer.push_back(0);
    CHECK(Parse(longer.data(), longer.size(), &p) == ParseError::Trailing);
    CHECK(Parse(nullptr, 100, &p) == ParseError::Short);

    // Lengths: body_len over the cap or absurd, and datagrams over kMaxDatagram.
    std::vector<uint8_t> m = pkt;
    m[24] = 0xFF; m[25] = 0xFF; m[26] = 0xFF; m[27] = 0xFF;
    CHECK(Parse(m.data(), m.size(), &p) == ParseError::Oversize);
    m = pkt;
    const uint32_t over = kMaxBody + 1;
    std::memcpy(m.data() + 24, &over, 4);   // little-endian hosts only run these tests
    CHECK(Parse(m.data(), m.size(), &p) == ParseError::Oversize);
    std::vector<uint8_t> big(kMaxDatagram + 1, 0);
    std::memcpy(big.data(), pkt.data(), kHeaderBytes);
    CHECK(Parse(big.data(), big.size(), &p) == ParseError::Oversize);

    // Magic, version, kind.
    m = pkt;
    m[0] ^= 1;
    CHECK(Parse(m.data(), m.size(), &p) == ParseError::BadMagic);
    m = pkt;
    m[4] = 2;
    CHECK(Parse(m.data(), m.size(), &p) == ParseError::BadVersion && p.h.version == 2 && p.h.kind == Kind::Data);
    m = pkt;
    m[5] = 0;
    CHECK(Parse(m.data(), m.size(), &p) == ParseError::BadKind);
    m[5] = 10;
    CHECK(Parse(m.data(), m.size(), &p) == ParseError::BadKind);
    // A handshake kind has no tag, so the same bytes are 16 too many.
    m = pkt;
    m[5] = static_cast<uint8_t>(Kind::Hello);
    CHECK(Parse(m.data(), m.size(), &p) == ParseError::Trailing);

    // Any single flipped bit is caught: by the framing, or by the tag under the name the (possibly
    // changed) channel index resolves to.
    const std::map<int, std::string> table{ { 0, "sco.net" }, { 1, "game.pose" }, { 3, "game.spawn" } };
    int caught = 0, flips = 0;
    for (size_t i = 0; i < pkt.size(); ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            m = pkt;
            m[i] = static_cast<uint8_t>(m[i] ^ (1u << bit));
            ++flips;
            if (Parse(m.data(), m.size(), &p) != ParseError::Ok) {
                ++caught;
                continue;
            }
            auto it = table.find(p.h.channel);
            if (it == table.end() || !VerifyTag(key, it->second, p)) ++caught;
        }
    }
    CHECK(caught == flips);

    // Encode's own refusals.
    Header h;
    h.kind = Kind::Data;
    uint8_t out[kMaxDatagram];
    std::vector<uint8_t> huge(kMaxBody + 1);
    CHECK(Encode(h, huge.data(), kMaxBody + 1, &key, "a.b", out, sizeof(out)) == 0);
    CHECK(Encode(h, huge.data(), kMaxBody, &key, "a.b", out, sizeof(out)) == kMaxDatagram);
    CHECK(Encode(h, body.data(), 10, nullptr, "a.b", out, sizeof(out)) == 0);   // tagged kind, no key
    h.kind = Kind::Hello;
    CHECK(Encode(h, body.data(), 10, &key, "", out, sizeof(out)) == 0);         // handshake kind, a key
    CHECK(Encode(h, body.data(), 10, nullptr, "", out, 20) == 0);              // cap
    CHECK(Encode(h, body.data(), 10, nullptr, "", out, sizeof(out)) == kHeaderBytes + 10);

    // Seeded fuzz: random datagrams and mutations of a valid one never read out of bounds (the
    // sanitizers watch) and never pass the tag.
    std::mt19937_64 rng(1234);
    int passed = 0;
    for (int i = 0; i < 20000; ++i) {
        std::vector<uint8_t> f;
        if (i % 2) {
            f.resize(rng() % 1500);
            for (uint8_t& b : f) b = static_cast<uint8_t>(rng());
            if (f.size() >= 4 && i % 4 == 1) std::memcpy(f.data(), pkt.data(), 4);   // past the magic
        } else {
            f = pkt;
            const int edits = 1 + static_cast<int>(rng() % 4);
            for (int e = 0; e < edits; ++e) f[rng() % f.size()] = static_cast<uint8_t>(rng());
            if (rng() % 3 == 0) f.resize(rng() % (f.size() + 40), 0);
        }
        if (Parse(f.data(), f.size(), &p) == ParseError::Ok && p.tag && f != pkt) {
            auto it = table.find(p.h.channel);
            if (it != table.end() && VerifyTag(key, it->second, p)) ++passed;
        }
    }
    CHECK(passed == 0);

    // Reader: sticky failure, nothing read past the end.
    const uint8_t few[3] = { 1, 2, 3 };
    Reader r(few, 3);
    CHECK(r.U16() == 0x0201);
    CHECK(r.U32() == 0 && !r.ok() && r.Left() == 0);
    CHECK(r.U8() == 0 && r.Bytes(0) == nullptr);

    // Names.
    CHECK(ValidFqn("game.pose") && ValidFqn("a.b.c") && ValidFqn("my_plugin.pose-2"));
    CHECK(!ValidFqn("ab") && !ValidFqn("nodot") && !ValidFqn(".a.b") && !ValidFqn("a.b.") && !ValidFqn("a..b"));
    CHECK(!ValidFqn("a.b c") && !ValidFqn("a/b.c") && !ValidFqn(std::string(kMaxFqn - 1, 'a') + ".b"));
    CHECK(ValidFqn(std::string(kMaxFqn - 2, 'a') + ".b"));
    CHECK(ValidName("Pilot One") && ValidName("\xc3\x89lodie") && !ValidName("") && !ValidName("a\nb") &&
          !ValidName(std::string(kMaxName + 1, 'x')));
}

// A program outside sco-core: sc_net.h alone, with a plain HMAC over the raw link key.
static void RawHmac(void*, const void* key, const uint8_t* a, size_t aLen, const uint8_t* b, size_t bLen, uint8_t out[32]) {
    std::vector<uint8_t> m(a, a + aLen);
    m.insert(m.end(), b, b + bLen);
    sco_hmac_sha256_mac(key, kKeyBytes, m.data(), m.size(), out);
}

static void TestScNet() {
    uint8_t raw[kKeyBytes];
    std::memset(raw, 0x42, sizeof(raw));   // KeyOf(0x42)
    const std::vector<uint8_t> body(50, 7);
    sc_net_header h{};
    h.version = SC_NET_PROTOCOL_VERSION;
    h.kind = SC_NET_DATA;
    h.channel = 3;
    h.sender = 5;
    h.seq = 11;
    h.body_len = 50;
    std::vector<uint8_t> pkt(SC_NET_HEADER_BYTES + 50 + SC_NET_TAG_BYTES);
    sc_net_write_header(&h, pkt.data());
    std::memcpy(pkt.data() + SC_NET_HEADER_BYTES, body.data(), 50);
    CHECK(sc_net_tag(RawHmac, nullptr, raw, &h, "game.spawn", 10, body.data(), pkt.data() + 78) == 1);
    // Byte for byte what sco-core sends.
    CHECK(pkt == MakeData(KeyOf(0x42), "game.spawn", 3, 5, 11, body));
    // And sco-core's packet parses and verifies with sc_net.h alone.
    sc_net_header g{};
    const uint8_t* b = nullptr;
    const uint8_t* t = nullptr;
    CHECK(sc_net_parse(pkt.data(), pkt.size(), &g, &b, &t) == SC_NET_OK && g.seq == 11 && g.channel == 3 &&
          g.sender == 5 && b == pkt.data() + 28 && t == pkt.data() + 78);
    uint8_t want[SC_NET_TAG_BYTES];
    CHECK(sc_net_tag(RawHmac, nullptr, raw, &g, "game.spawn", 10, b, want) == 1 && sc_net_tag_equal(want, t) == 1);
    want[15] ^= 1;
    CHECK(sc_net_tag_equal(want, t) == 0);
    CHECK(sc_net_tag(RawHmac, nullptr, raw, &g, "game.pose", 9, b, want) == 1 && sc_net_tag_equal(want, t) == 0);
    // The MAC input before the body, field by field.
    uint8_t head[SC_NET_MAC_HEAD_MAX];
    CHECK(sc_net_mac_head(&h, "game.spawn", 10, head, sizeof(head)) == 34);
    CHECK(head[0] == SC_NET_PROTOCOL_VERSION && head[1] == SC_NET_DATA && head[2] == 10 && head[3] == 0 &&
          std::memcmp(head + 4, "game.spawn", 10) == 0 && head[14] == 5 && head[22] == 11 && head[30] == 50);
    CHECK(sc_net_mac_head(&h, "game.spawn", 10, head, 33) == 0);
    CHECK(sc_net_mac_head(&h, "game.spawn", SC_NET_MAX_FQN + 1, head, sizeof(head)) == 0);
    std::vector<uint8_t> bad = pkt;
    bad[SC_NET_OFF_VERSION] = 2;
    CHECK(sc_net_parse(bad.data(), bad.size(), &g, &b, &t) == SC_NET_E_BAD_VERSION && g.version == 2 && !b && !t);
    sc_net_replay w{};
    CHECK(sc_net_replay_check(&w, 1) == SC_NET_REPLAY_NEW && sc_net_replay_check(&w, 0) == SC_NET_REPLAY_TOO_OLD);
    CHECK(sc_net_valid_fqn("game.spawn", 10) == 1 && sc_net_valid_fqn(nullptr, 3) == 0);
}

static void TestReplay() {
    ReplayWindow w;
    using V = ReplayWindow::Verdict;
    CHECK(w.Check(0) == V::TooOld);
    CHECK(w.Check(1) == V::New);
    w.Accept(1);
    CHECK(w.Check(1) == V::Duplicate && w.Check(2) == V::New);
    w.Accept(5);
    CHECK(w.Check(3) == V::New && w.Check(5) == V::Duplicate && w.Check(1) == V::Duplicate);
    w.Accept(3);
    CHECK(w.Check(3) == V::Duplicate && w.Check(4) == V::New);
    w.Accept(5 + kReplayWindow - 1);   // 5 is now at the far edge of the window
    CHECK(w.Check(5) == V::Duplicate && w.Check(4) == V::TooOld && w.Check(6) == V::New);
    w.Accept(1000000);
    CHECK(w.Highest() == 1000000 && w.Check(999999) == V::New && w.Check(5 + kReplayWindow - 1) == V::TooOld);

    // Against a model over a random reordered stream with duplicates.
    std::mt19937_64 rng(7);
    ReplayWindow r;
    std::set<uint64_t> seen;
    uint64_t top = 0;
    int mismatches = 0;
    for (int i = 0; i < 50000; ++i) {
        const uint64_t base = static_cast<uint64_t>(i) * 3 + 1;
        const uint64_t s = base + rng() % 2000 - (rng() % 2 ? 1500 : 0) + 2000;
        V want;
        if (top && s + kReplayWindow <= top) want = V::TooOld;
        else want = seen.count(s) ? V::Duplicate : V::New;
        const V got = r.Check(s);
        if (got != want) ++mismatches;
        if (got == V::New) {
            r.Accept(s);
            seen.insert(s);
            if (s > top) top = s;
        }
    }
    CHECK(mismatches == 0);
}

// ---- reliable streams ---------------------------------------------------------------------------

static void TestStreams() {
    using V = RecvStream::Verdict;
    std::vector<RecvStream::Delivery> out;
    {
        RecvStream rs;
        const uint8_t d[1200] = {};
        CHECK(rs.OnUnit(0, 9, 10, 0, d, 5, 100, &out) == V::Accepted && out.empty() && rs.ackDue);
        CHECK(rs.OnUnit(0, 9, 10, 0, d, 5, 100, &out) == V::Duplicate);
        CHECK(rs.OnUnit(1, 9, 10, 5, d, 5, 100, &out) == V::Accepted && out.size() == 1 && out[0].data.size() == 10 &&
              out[0].origin == 9);
        CHECK(rs.Next() == 2 && rs.Mask() == 0);
        // Fields no honest sender produces.
        CHECK(rs.OnUnit(2, 9, 2000, 0, d, 1201, 5000, &out) == V::Malformed);
        CHECK(rs.OnUnit(2, 9, 10, 11, d, 0, 100, &out) == V::Malformed);
        CHECK(rs.OnUnit(2, 9, 10, 6, d, 5, 100, &out) == V::Malformed);
        CHECK(rs.OnUnit(2, 9, kMaxReliable + 1, 0, d, 5, kMaxReliable, &out) == V::Malformed);
        CHECK(rs.OnUnit(2, 9, 5, 0, d, 0, 100, &out) == V::Malformed);
        CHECK(rs.OnUnit(2 + kWindow, 9, 5, 0, d, 5, 100, &out) == V::Ahead);
        // Out of order inside the window: buffered, acknowledged in the mask, delivered in order.
        out.clear();
        CHECK(rs.OnUnit(4, 9, 3, 0, d, 3, 100, &out) == V::Accepted && out.empty());
        CHECK(rs.Mask() == 0x2u);   // unit Next()+2
        CHECK(rs.OnUnit(3, 9, 0, 0, d, 0, 100, &out) == V::Accepted && out.empty());
        CHECK(rs.Mask() == 0x3u);
        CHECK(rs.OnUnit(2, 9, 1, 0, d, 1, 100, &out) == V::Accepted);
        CHECK(out.size() == 3 && out[0].data.size() == 1 && out[1].data.empty() && out[2].data.size() == 3);
        CHECK(rs.Next() == 5 && rs.Mask() == 0);
    }
    {
        // Over the receiver's max_len: consumed and dropped without allocating it; the next is fine.
        RecvStream rs;
        const uint8_t d[1200] = {};
        out.clear();
        CHECK(rs.OnUnit(0, 2, 2400, 0, d, 1200, 4, &out) == V::Accepted);
        CHECK(rs.OnUnit(1, 2, 2400, 1200, d, 1200, 4, &out) == V::Accepted && out.empty() && rs.Refused() == 1);
        CHECK(rs.OnUnit(2, 2, 4, 0, d, 4, 4, &out) == V::Accepted && out.size() == 1);
        // Fragments that don't line up: the message is abandoned, a new one starts cleanly.
        out.clear();
        CHECK(rs.OnUnit(3, 2, 2400, 0, d, 1200, 5000, &out) == V::Accepted);
        CHECK(rs.OnUnit(4, 2, 3, 0, d, 3, 5000, &out) == V::Accepted && rs.Broken() == 1);
        CHECK(out.size() == 1 && out[0].data.size() == 3);
        CHECK(rs.OnUnit(5, 2, 2400, 1200, d, 1200, 5000, &out) == V::Accepted && rs.Broken() == 2 && out.size() == 1);
    }
    {
        SendStream ss;
        std::vector<uint8_t> msg(kMaxReliable, 7);
        CHECK(ss.Push(1, false, msg.data(), kMaxReliable) && ss.Queued() == (kMaxReliable + kFragBytes - 1) / kFragBytes);
        CHECK(!ss.Push(1, false, msg.data(), kMaxReliable + 1));
        int pushes = 0;
        while (ss.Push(1, false, msg.data(), kMaxReliable)) ++pushes;
        CHECK(pushes >= 1 && ss.Queued() <= kMaxQueuedUnits);
        CHECK(ss.Push(1, false, nullptr, 0) || ss.Queued() == kMaxQueuedUnits);
        // The window and the doubling timeout.
        auto due = ss.Due(0, 200, 2000);
        CHECK(due.size() == kWindow && due[0]->seq == 0 && due[0]->offset == 0 && due[1]->offset == kFragBytes);
        CHECK(ss.Due(199, 200, 2000).empty());
        CHECK(ss.Due(200, 200, 2000).size() == kWindow);
        CHECK(ss.Due(599, 200, 2000).empty());
        CHECK(ss.Due(600, 200, 2000).size() == kWindow);
        // A bogus ack (past anything assigned) changes nothing; a real one slides the window.
        const size_t before = ss.Queued();
        ss.OnAck(ss.NextSeq() + 1, ~0ull);
        CHECK(ss.Queued() == before);
        ss.OnAck(10, 0x1u);   // 0..9 and 11
        CHECK(ss.Queued() == before - 10);
        due = ss.Due(600, 200, 2000);
        CHECK(due.size() == 10 && due[0]->seq == kWindow);   // only the newly opened slots
    }
}

// ---- sessions -----------------------------------------------------------------------------------

struct Got {
    std::string channel;
    PeerId from;
    std::vector<uint8_t> data;
};

struct Node {
    MemTransport tr;
    Core core;
    std::vector<Got> got;
    std::vector<std::string> events;   // "J2:Ann", "L2:left: bye"
    int hostFlagsAtJoin = -1;          // the host's flags in Peers() at the moment its JOINED event fired
    int ups = 0, downs = 0;
    std::string lastDown;
    std::function<bool(std::string_view, const Endpoint&)> admit;

    Node(MemNet& net, uint16_t id) : tr(net, id), core(tr, MakeCallbacks()) {}

    Callbacks MakeCallbacks() {
        Callbacks cb;
        cb.message = [this](const Message& m) {
            got.push_back(Got{ std::string(m.channel), m.from, std::vector<uint8_t>(m.data, m.data + m.len) });
        };
        cb.peer = [this](PeerEvent e, PeerId id, std::string_view text) {
            events.push_back((e == PeerEvent::Joined ? "J" : "L") + std::to_string(id) + ":" + std::string(text));
            if (e == PeerEvent::Joined && id == kHostPeer)
                for (const PeerInfo& p : core.Peers())
                    if (p.id == kHostPeer) hostFlagsAtJoin = static_cast<int>(p.flags);
        };
        cb.state = [this](bool active, std::string_view reason) {
            if (active) {
                ++ups;
            } else {
                ++downs;
                lastDown = std::string(reason);
            }
        };
        cb.admit = [this](std::string_view name, const Endpoint& from) { return admit ? admit(name, from) : true; };
        return cb;
    }
    size_t Count(const std::string& ch) const {
        size_t n = 0;
        for (const Got& g : got) n += g.channel == ch;
        return n;
    }
};

static Options Opts(const char* name, const char* pass, uint64_t seed) {
    Options o;
    o.playerName = name;
    o.passphrase = pass;
    o.pbkdf2Iters = 1000;   // the default (kPbkdf2Iters) is exercised once, in TestDefaultIterations
    auto rng = std::make_shared<std::mt19937_64>(seed);
    o.random = [rng](uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>((*rng)());
    };
    return o;
}

// Advances the virtual clock in steps, pumping every node, until done() or untilMs.
static bool RunUntil(MemNet& net, const std::vector<Node*>& nodes, uint64_t untilMs, const std::function<bool()>& done,
                     uint32_t stepMs = 5) {
    for (;;) {
        for (Node* n : nodes) n->core.Pump(net.now);
        if (done()) return true;
        if (net.now >= untilMs) return false;
        net.now += stepMs;
    }
}

static bool Joined(Node& n) { return n.core.Active() && n.core.GetState() == State::Joined; }

static void TestHandshake() {
    MemNet net(1);
    Node host(net, 1), ann(net, 2), bob(net, 3);
    CHECK(host.core.RegisterChannel("game.pose", 0, kMaxUnreliable));
    CHECK(host.core.RegisterChannel("game.spawn", kReliable, 4096));
    CHECK(ann.core.RegisterChannel("game.pose", 0, kMaxUnreliable));
    CHECK(ann.core.RegisterChannel("game.spawn", kReliable, 4096));
    CHECK(!host.core.Host(Opts("", "pw", 1), 0));             // a name is required
    CHECK(!host.core.Host(Opts("Host", "", 1), 0));           // so is a passphrase
    Options o = Opts("Host", "hunter2", 1);
    o.maxPeers = kMaxPeers + 1;
    CHECK(!host.core.Host(o, 0));
    CHECK(host.core.Host(Opts("Host", "hunter2", 1), 0) && host.core.Active() && host.ups == 1);
    CHECK(!host.core.Host(Opts("Host", "hunter2", 1), 0));    // already hosting
    CHECK(host.core.Self() == kHostPeer);

    CHECK(ann.core.Join(Opts("Ann", "hunter2", 2), host.tr.Self(), net.now));
    CHECK(ann.core.GetState() == State::Joining && !ann.core.Active());
    CHECK(RunUntil(net, { &host, &ann }, 5000, [&] { return Joined(ann); }));
    CHECK(ann.core.Self() == 2 && ann.ups == 1);
    auto peers = ann.core.Peers();
    CHECK(peers.size() == 2 && peers[0].id == 2 && peers[0].name == "Ann" && peers[1].id == kHostPeer && peers[1].name == "Host");
    std::string name;
    CHECK(host.core.PeerName(2, &name) && name == "Ann" && !host.core.PeerName(9, &name));
    CHECK(host.events.size() == 1 && host.events[0] == "J2:Ann");
    CHECK(ann.events.size() == 1 && ann.events[0] == "J1:Host");

    // Data both ways once up.
    const uint8_t hello[] = { 'h', 'i' };
    CHECK(ann.core.Send("game.spawn", hello, 2) == SendResult::Ok);
    CHECK(host.core.Send("game.pose", hello, 2) == SendResult::Ok);
    CHECK(RunUntil(net, { &host, &ann }, net.now + 2000, [&] { return host.got.size() == 1 && ann.got.size() == 1; }));
    CHECK(host.got[0].channel == "game.spawn" && host.got[0].from == 2 && host.got[0].data.size() == 2);
    CHECK(ann.got[0].channel == "game.pose" && ann.got[0].from == kHostPeer);

    // A wrong passphrase: the PROOF fails, the host refuses and forgets the join.
    const uint64_t refusedBefore = host.core.GetStats().joinsRefused;
    CHECK(bob.core.Join(Opts("Bob", "hunter3", 3), host.tr.Self(), net.now));
    CHECK(RunUntil(net, { &host, &ann, &bob }, net.now + 5000, [&] { return bob.core.GetState() == State::Idle; }));
    CHECK(bob.downs == 1 && bob.lastDown == "refused: wrong passphrase" && bob.core.LastReason() == bob.lastDown);
    CHECK(host.core.GetStats().joinsRefused == refusedBefore + 1);
    CHECK(host.core.Peers().size() == 2 && host.events.size() == 1);
    // Bob retries with the right one and gets in.
    CHECK(bob.core.Join(Opts("Bob", "hunter2", 4), host.tr.Self(), net.now));
    CHECK(RunUntil(net, { &host, &ann, &bob }, net.now + 5000, [&] { return Joined(bob); }));
    CHECK(bob.core.Self() == 3 && host.core.Peers().size() == 3);
    CHECK(RunUntil(net, { &host, &ann, &bob }, net.now + 2000, [&] { return ann.core.Peers().size() == 3; }));
    CHECK(ann.events.size() == 2 && ann.events[1] == "J3:Bob");

    // Nobody listening: the join gives up after joinTimeoutMs.
    Node lone(net, 9);
    CHECK(lone.core.Join(Opts("Lone", "pw", 9), MemEndpoint(200), net.now));
    const uint64_t start = net.now;
    CHECK(RunUntil(net, { &lone }, net.now + 20000, [&] { return lone.core.GetState() == State::Idle; }));
    CHECK(lone.lastDown == "no answer from the host" && net.now - start >= 10000);
}

static void TestDefaultIterations() {
    MemNet net(2);
    Node host(net, 1), ann(net, 2);
    Options ho = Opts("Host", "correct horse", 5), ao = Opts("Ann", "correct horse", 6);
    ho.pbkdf2Iters = ao.pbkdf2Iters = kPbkdf2Iters;
    CHECK(host.core.Host(ho, 0));
    CHECK(ann.core.Join(ao, host.tr.Self(), 0));
    CHECK(RunUntil(net, { &host, &ann }, 5000, [&] { return Joined(ann); }));
}

static void TestRefusals() {
    // Full session.
    {
        MemNet net(3);
        Node host(net, 1), ann(net, 2), bob(net, 3);
        Options o = Opts("Host", "pw", 1);
        o.maxPeers = 2;
        CHECK(host.core.Host(o, 0));
        CHECK(ann.core.Join(Opts("Ann", "pw", 2), host.tr.Self(), 0));
        CHECK(RunUntil(net, { &host, &ann }, 5000, [&] { return Joined(ann); }));
        CHECK(bob.core.Join(Opts("Bob", "pw", 3), host.tr.Self(), net.now));
        CHECK(RunUntil(net, { &host, &ann, &bob }, net.now + 5000, [&] { return bob.core.GetState() == State::Idle; }));
        CHECK(bob.lastDown == "refused: the session is full");
    }
    // The product says no.
    {
        MemNet net(4);
        Node host(net, 1), ann(net, 2);
        std::string asked;
        host.admit = [&](std::string_view n, const Endpoint&) { asked = std::string(n); return false; };
        CHECK(host.core.Host(Opts("Host", "pw", 1), 0));
        CHECK(ann.core.Join(Opts("Ann", "pw", 2), host.tr.Self(), 0));
        CHECK(RunUntil(net, { &host, &ann }, 5000, [&] { return ann.core.GetState() == State::Idle; }));
        CHECK(asked == "Ann" && ann.lastDown == "refused: not admitted by the host" && host.core.Peers().size() == 1);
    }
    // Protocol versions: a HELLO from another version gets a REFUSE naming ours; a joiner reads a
    // REFUSE from another version as a version mismatch (only if it echoes its nonce).
    {
        MemNet net(5);
        Node host(net, 1);
        MemTransport raw(net, 50);
        CHECK(host.core.Host(Opts("Host", "pw", 1), 0));
        std::vector<uint8_t> hello(kHeaderBytes + 64, 0), hb(64, 0);
        for (int i = 0; i < 16; ++i) hb[i] = static_cast<uint8_t>(0xA0 + i);
        Header h;
        h.kind = Kind::Hello;
        Encode(h, hb.data(), 64, nullptr, {}, hello.data(), hello.size());
        hello[4] = 2;                                     // version 2
        net.Inject(raw.Self(), host.tr.Self(), hello);
        host.core.Pump(net.now);
        net.now += 1;
        uint8_t buf[2048];
        size_t len = 0;
        Endpoint from;
        CHECK(raw.Receive(&from, buf, sizeof(buf), &len));
        Packet p;
        CHECK(Parse(buf, len, &p) == ParseError::Ok && p.h.kind == Kind::Refuse);
        CHECK(p.h.bodyLen >= 18 && p.body[0] == 0xA0 && p.body[16] == static_cast<uint8_t>(Refusal::Version) &&
              p.body[17] == kProtocolVersion);
        CHECK(host.core.GetStats().badVersion == 1);

        Node ann(net, 2);
        MemTransport fake(net, 60);
        CHECK(ann.core.Join(Opts("Ann", "pw", 2), fake.Self(), net.now));
        net.now += 1;
        CHECK(fake.Receive(&from, buf, sizeof(buf), &len));
        CHECK(Parse(buf, len, &p) == ParseError::Ok && p.h.kind == Kind::Hello);
        std::vector<uint8_t> refuse(kHeaderBytes + 20, 0);
        h.kind = Kind::Refuse;
        std::vector<uint8_t> rb(20, 0);
        std::memcpy(rb.data(), p.body, 16);
        rb[16] = static_cast<uint8_t>(Refusal::Version);
        rb[17] = 9;
        Encode(h, rb.data(), 20, nullptr, {}, refuse.data(), refuse.size());
        refuse[4] = 9;
        std::vector<uint8_t> wrongNonce = refuse;
        wrongNonce[kHeaderBytes] ^= 1;
        net.Inject(fake.Self(), ann.tr.Self(), wrongNonce);   // a blind spoof: ignored
        ann.core.Pump(net.now);
        CHECK(ann.core.GetState() == State::Joining);
        net.Inject(fake.Self(), ann.tr.Self(), refuse);
        ann.core.Pump(net.now);
        CHECK(ann.core.GetState() == State::Idle && ann.lastDown == "refused: protocol version mismatch (host v9, this v1)");
    }
    // A host that doesn't know the passphrase can't admit: a forged WELCOME is ignored.
    {
        MemNet net(6);
        Node ann(net, 2);
        MemTransport fake(net, 60);
        CHECK(ann.core.Join(Opts("Ann", "pw", 2), fake.Self(), 0));
        uint8_t buf[2048];
        size_t len = 0;
        Endpoint from;
        net.now = 1;
        CHECK(fake.Receive(&from, buf, sizeof(buf), &len));
        Packet p;
        CHECK(Parse(buf, len, &p) == ParseError::Ok && p.h.kind == Kind::Hello);
        uint8_t cn[16];
        std::memcpy(cn, p.body, 16);
        std::vector<uint8_t> body(48, 0x11);
        std::memcpy(body.data(), cn, 16);
        Header h;
        h.kind = Kind::Challenge;
        std::vector<uint8_t> dg(kMaxDatagram);
        dg.resize(Encode(h, body.data(), 48, nullptr, {}, dg.data(), dg.size()));
        net.Inject(fake.Self(), ann.tr.Self(), dg);
        ann.core.Pump(net.now);
        net.now = 2;
        CHECK(fake.Receive(&from, buf, sizeof(buf), &len) && Parse(buf, len, &p) == ParseError::Ok && p.h.kind == Kind::Proof);
        std::vector<uint8_t> wb(56, 0x22);
        std::memcpy(wb.data(), cn, 16);
        wb[16] = 2;
        for (int i = 17; i < 24; ++i) wb[i] = 0;
        h.kind = Kind::Welcome;
        dg.assign(kMaxDatagram, 0);
        dg.resize(Encode(h, wb.data(), 56, nullptr, {}, dg.data(), dg.size()));
        net.Inject(fake.Self(), ann.tr.Self(), dg);
        ann.core.Pump(net.now);
        CHECK(ann.core.GetState() == State::Joining && ann.core.GetStats().handshakeDropped == 1);
    }
}

static void TestHostile() {
    MemNet net(11);
    Node host(net, 1), ann(net, 2);
    for (Node* n : { &host, &ann }) {
        CHECK(n->core.RegisterChannel("game.pose", 0, kMaxUnreliable));
        CHECK(n->core.RegisterChannel("game.spawn", kReliable, 8192));
    }
    CHECK(host.core.Host(Opts("Host", "pw", 1), 0));
    CHECK(ann.core.Join(Opts("Ann", "pw", 2), host.tr.Self(), 0));
    CHECK(RunUntil(net, { &host, &ann }, 5000, [&] { return Joined(ann); }));
    const Stats& hs = host.core.GetStats();

    // Tampered: the first three DATA datagrams from Ann get one byte flipped in the body. Each fails
    // the tag; the reliable layer resends, and the message arrives exactly once.
    int tamper = 3;
    net.tap = [&](MemNet::Datagram& d) {
        if (d.from == ann.tr.Self() && d.bytes.size() > kHeaderBytes + 4 && d.bytes[5] == static_cast<uint8_t>(Kind::Data) &&
            tamper > 0) {
            --tamper;
            d.bytes[kHeaderBytes + 3] ^= 0x40;
        }
        return true;
    };
    const uint8_t msg[] = { 1, 2, 3, 4, 5 };
    CHECK(ann.core.Send("game.spawn", msg, 5) == SendResult::Ok);
    CHECK(RunUntil(net, { &host, &ann }, net.now + 10000, [&] { return host.Count("game.spawn") == 1 && tamper == 0; }));
    CHECK(RunUntil(net, { &host, &ann }, net.now + 3000, [&] { return false; }) == false);
    CHECK(hs.badTag == 3 && host.Count("game.spawn") == 1 && hs.retransmits == 0);

    // Truncated by one byte: framing refuses it.
    const uint64_t malformed0 = hs.malformed;
    net.tap = [&](MemNet::Datagram& d) {
        if (d.from == ann.tr.Self() && d.bytes[5] == static_cast<uint8_t>(Kind::Data)) d.bytes.pop_back();
        return true;
    };
    CHECK(ann.core.Send("game.pose", msg, 5) == SendResult::Ok);
    RunUntil(net, { &host, &ann }, net.now + 100, [&] { return false; });
    CHECK(hs.malformed == malformed0 + 1 && host.Count("game.pose") == 0);

    // Replayed: an unreliable pose captured on the wire and sent again is dropped by the window.
    std::vector<uint8_t> captured;
    net.tap = [&](MemNet::Datagram& d) {
        if (d.from == ann.tr.Self() && d.bytes[5] == static_cast<uint8_t>(Kind::Data) && captured.empty()) captured = d.bytes;
        return true;
    };
    CHECK(ann.core.Send("game.pose", msg, 5) == SendResult::Ok);
    CHECK(RunUntil(net, { &host, &ann }, net.now + 1000, [&] { return host.Count("game.pose") == 1; }));
    net.tap = nullptr;
    const uint64_t replayed0 = hs.replayed;
    net.Inject(ann.tr.Self(), host.tr.Self(), captured);
    net.Inject(ann.tr.Self(), host.tr.Self(), captured);
    RunUntil(net, { &host, &ann }, net.now + 100, [&] { return false; });
    CHECK(hs.replayed == replayed0 + 2 && host.Count("game.pose") == 1);

    // Spoofed: the same bytes from another address, or with another sender id, are not Ann's.
    const uint64_t spoofed0 = hs.spoofed;
    net.Inject(MemEndpoint(77), host.tr.Self(), captured);
    std::vector<uint8_t> other = captured;
    other[8] = 9;   // sender_peer_id
    net.Inject(ann.tr.Self(), host.tr.Self(), other);
    RunUntil(net, { &host, &ann }, net.now + 100, [&] { return false; });
    CHECK(hs.spoofed == spoofed0 + 2);

    // A channel index nobody assigned can't even be checked; a bad version is counted as such.
    std::vector<uint8_t> unk = captured;
    unk[6] = 200;
    const uint64_t unknown0 = hs.unknownChannel, version0 = hs.badVersion;
    net.Inject(ann.tr.Self(), host.tr.Self(), unk);
    std::vector<uint8_t> ver = captured;
    ver[4] = 7;
    net.Inject(ann.tr.Self(), host.tr.Self(), ver);
    RunUntil(net, { &host, &ann }, net.now + 100, [&] { return false; });
    CHECK(hs.unknownChannel == unknown0 + 1 && hs.badVersion == version0 + 1);

    // Oversize: over kMaxDatagram, and over the receive buffer itself.
    const uint64_t malformed1 = hs.malformed;
    std::vector<uint8_t> big(kMaxDatagram + 1, 0);
    std::memcpy(big.data(), captured.data(), kHeaderBytes);
    net.Inject(ann.tr.Self(), host.tr.Self(), big);
    net.Inject(ann.tr.Self(), host.tr.Self(), std::vector<uint8_t>(64 * 1024, 0xEE));
    RunUntil(net, { &host, &ann }, net.now + 100, [&] { return false; });
    CHECK(hs.malformed == malformed1 + 2);

    // Garbage from Ann's address and from strangers, and a flood of valid HELLOs from spoofed
    // addresses (more than the 32 joins the host keeps half-open): the session carries on and a
    // newcomer still gets in.
    std::mt19937_64 rng(99);
    for (int i = 0; i < 3000; ++i) {
        std::vector<uint8_t> g(rng() % 1500);
        for (uint8_t& b : g) b = static_cast<uint8_t>(rng());
        if (g.size() >= 6 && i % 2) {
            std::memcpy(g.data(), captured.data(), 6);   // valid magic, version and kind
        }
        net.Inject(i % 3 ? ann.tr.Self() : MemEndpoint(static_cast<uint16_t>(300 + i % 50)), host.tr.Self(), g);
    }
    for (int i = 0; i < 40; ++i) {
        std::vector<uint8_t> hello(kHeaderBytes + 64, 0);
        Header h;
        h.kind = Kind::Hello;
        std::vector<uint8_t> body(64, 0);
        body[0] = static_cast<uint8_t>(i);   // the client nonce
        body[16] = 1;                        // name "x", no channels
        body[18] = 'x';
        hello.resize(Encode(h, body.data(), 64, nullptr, {}, hello.data(), hello.size()));
        net.Inject(MemEndpoint(static_cast<uint16_t>(500 + i)), host.tr.Self(), hello);
    }
    RunUntil(net, { &host, &ann }, net.now + 100, [&] { return false; });
    CHECK(host.core.Active() && Joined(ann) && host.core.Peers().size() == 2);
    CHECK(ann.core.Send("game.spawn", msg, 5) == SendResult::Ok);
    CHECK(RunUntil(net, { &host, &ann }, net.now + 5000, [&] { return host.Count("game.spawn") == 2; }));
    Node bob(net, 3);
    CHECK(bob.core.Join(Opts("Bob", "pw", 3), host.tr.Self(), net.now));
    CHECK(RunUntil(net, { &host, &ann, &bob }, net.now + 5000, [&] { return Joined(bob); }));
}

// Message i from peer s on a channel: deterministic bytes, so the receiver can check every byte.
static std::vector<uint8_t> Pattern(uint64_t seed, PeerId s, uint32_t i, uint32_t len) {
    std::vector<uint8_t> v(len);
    std::mt19937_64 r(seed * 1000003u + s * 7919u + i);
    for (uint32_t k = 0; k < len; ++k) v[k] = static_cast<uint8_t>(r());
    if (len >= 4) {
        v[0] = static_cast<uint8_t>(i);
        v[1] = static_cast<uint8_t>(i >> 8);
        v[2] = static_cast<uint8_t>(s);
        v[3] = 0xA5;
    }
    return v;
}

static void TestReliableLossy(uint64_t seed) {
    MemNet net(seed);
    net.loss = 0.2;
    net.dup = 0.1;
    net.minDelayMs = 1;
    net.maxDelayMs = 60;
    std::vector<std::unique_ptr<Node>> nodes;
    for (uint16_t i = 1; i <= 4; ++i) nodes.push_back(std::make_unique<Node>(net, i));
    std::vector<Node*> all;
    for (auto& n : nodes) {
        all.push_back(n.get());
        CHECK(n->core.RegisterChannel("t.rel", kReliable, kMaxReliable));
        CHECK(n->core.RegisterChannel("t.unrel", 0, kMaxUnreliable));
    }
    Node& host = *nodes[0];
    CHECK(host.core.Host(Opts("Host", "pw", seed), 0));
    const char* names[] = { "", "Ann", "Bob", "Cid" };
    for (int i = 1; i < 4; ++i) CHECK(nodes[i]->core.Join(Opts(names[i], "pw", seed + i), host.tr.Self(), 0));
    CHECK(RunUntil(net, all, 30000, [&] {
        for (int i = 1; i < 4; ++i)
            if (!Joined(*nodes[i]) || nodes[i]->core.Peers().size() != 4) return false;
        return true;
    }));

    // Sizes at the edges: empty, one unit exactly, one over, many units, the maximum.
    const uint32_t sizes[] = { 0, 1, kFragBytes - 1, kFragBytes, kFragBytes + 1, 5000, 65536, kMaxReliable, 3, 77 };
    const uint32_t kMsgs = 10;
    uint8_t one = 0;
    for (auto& n : nodes) {
        CHECK(n->core.Send("t.rel", &one, kMaxReliable + 1) == SendResult::TooLarge);
        CHECK(n->core.Send("t.unrel", &one, kMaxUnreliable + 1) == SendResult::TooLarge);
        for (uint32_t i = 0; i < kMsgs; ++i) {
            const std::vector<uint8_t> m = Pattern(seed, n->core.Self(), i, sizes[i]);
            CHECK(n->core.Send("t.rel", m.data(), static_cast<uint32_t>(m.size())) == SendResult::Ok);
            const std::vector<uint8_t> u = Pattern(seed + 1, n->core.Self(), i, 64);
            CHECK(n->core.Send("t.unrel", u.data(), 64) == SendResult::Ok);
        }
    }
    auto relCount = [&](Node& n) { return n.Count("t.rel"); };
    const bool done = RunUntil(net, all, net.now + 300000, [&] {
        for (auto& n : nodes)
            if (relCount(*n) != 3 * kMsgs) return false;
        return true;
    });
    CHECK(done);
    // Exactly once, in order per sender, byte for byte; unreliable at most once each.
    for (auto& n : nodes) {
        std::map<PeerId, uint32_t> next;
        std::set<std::pair<PeerId, uint32_t>> unrel;
        bool ok = true, unrelOk = true;
        for (const Got& g : n->got) {
            if (g.channel == "t.rel") {
                const uint32_t i = next[g.from]++;
                ok = ok && i < kMsgs && g.data == Pattern(seed, g.from, i, sizes[i]) && g.from != n->core.Self();
            } else {
                const uint32_t i = static_cast<uint32_t>(g.data[0] | (g.data[1] << 8));
                unrelOk = unrelOk && g.data == Pattern(seed + 1, g.from, i, 64) && unrel.insert({ g.from, i }).second;
            }
        }
        CHECK(ok && next.size() == 3);
        CHECK(unrelOk);
    }
    CHECK(host.core.GetStats().retransmits > 0);
    if (!done || g_fail) std::printf("  seed %llu: done=%d at %llu ms\n", static_cast<unsigned long long>(seed), done ? 1 : 0,
                                     static_cast<unsigned long long>(net.now));
}

static void TestPeers() {
    MemNet net(21);
    Node host(net, 1), ann(net, 2), bob(net, 3), cid(net, 4);
    std::vector<Node*> all{ &host, &ann, &bob, &cid };
    // Registration rules.
    CHECK(!host.core.RegisterChannel("sco.net", 0, 10));
    CHECK(!host.core.RegisterChannel("nodot", 0, 10));
    CHECK(!host.core.RegisterChannel("a.b", 0x8, 10));
    CHECK(!host.core.RegisterChannel("a.b", kFromHost | kToHost, 10));
    CHECK(!host.core.RegisterChannel("a.b", 0, 0));
    CHECK(!host.core.RegisterChannel("a.b", 0, kMaxUnreliable + 1));
    CHECK(!host.core.RegisterChannel("a.b", kReliable, kMaxReliable + 1));
    CHECK(host.core.RegisterChannel("a.b", 0, 10) && !host.core.RegisterChannel("a.b", kReliable, 10));
    CHECK(host.core.UnregisterChannel("a.b") && !host.core.UnregisterChannel("a.b"));

    for (Node* n : all) {
        CHECK(n->core.RegisterChannel("h.cmd", kReliable | kFromHost, 1000));
        CHECK(n->core.RegisterChannel("g.chat", kReliable, 1000));
    }
    CHECK(host.core.RegisterChannel("h.report", 0, 100));
    for (Node* n : { &ann, &bob, &cid }) CHECK(n->core.RegisterChannel("h.report", kToHost, 100));
    CHECK(host.core.RegisterChannel("h.only", 0, 100));
    CHECK(cid.core.RegisterChannel("t.small", kReliable, 10));
    CHECK(ann.core.RegisterChannel("t.small", kReliable, 1000));

    const uint8_t x[600] = {};
    CHECK(host.core.Send("g.chat", x, 1) == SendResult::NoSession);
    CHECK(host.core.Host(Opts("Host", "pw", 1), 0));
    CHECK(host.core.Send("g.chat", x, 1) == SendResult::NotRemote);
    CHECK(ann.core.Join(Opts("Ann", "pw", 10), host.tr.Self(), 0));
    CHECK(bob.core.Join(Opts("Bob", "pw", 11), host.tr.Self(), 0));
    CHECK(cid.core.Join(Opts("Cid", "pw", 12), host.tr.Self(), 0));
    CHECK(RunUntil(net, all, 5000, [&] {
        return Joined(ann) && Joined(bob) && Joined(cid) && ann.core.Peers().size() == 4 && cid.core.Peers().size() == 4;
    }));

    // FROM_HOST: the host speaks, joiners hear it from the host; a joiner may not send on it.
    CHECK(host.core.Send("h.cmd", x, 3) == SendResult::Ok);
    CHECK(ann.core.Send("h.cmd", x, 3) == SendResult::WrongDirection);
    // TO_HOST: only the host hears it; the host can't send on it from its side when it is TO_HOST
    // there, and here the host's own registration has no flags.
    CHECK(ann.core.Send("h.report", x, 4) == SendResult::Ok);
    // Relayed chat: everyone else hears Ann, once, from Ann.
    CHECK(ann.core.Send("g.chat", x, 5) == SendResult::Ok);
    // Nobody but the host registered h.only.
    CHECK(host.core.Send("h.only", x, 1) == SendResult::NotRemote);
    CHECK(ann.core.Send("h.only", x, 1) == SendResult::UnknownChannel);
    CHECK(ann.core.Send("g.chat", x, 1001) == SendResult::TooLarge);
    CHECK(RunUntil(net, all, net.now + 3000, [&] {
        return bob.Count("h.cmd") == 1 && cid.Count("h.cmd") == 1 && ann.Count("h.cmd") == 1 && host.Count("h.report") == 1 &&
               host.Count("g.chat") == 1 && bob.Count("g.chat") == 1 && cid.Count("g.chat") == 1;
    }));
    RunUntil(net, all, net.now + 500, [&] { return false; });
    CHECK(bob.Count("h.report") == 0 && cid.Count("h.report") == 0 && ann.Count("g.chat") == 0);
    CHECK(bob.got.size() == 2 && bob.got[0].from == kHostPeer);
    for (const Got& g : cid.got)
        if (g.channel == "g.chat") CHECK(g.from == 2 && g.data.size() == 5);

    // A receiver's max_len is its own: Cid allows 10 bytes on t.small, Ann sends 500 then 5.
    const uint64_t refused0 = cid.core.GetStats().refused;
    CHECK(ann.core.Send("t.small", x, 500) == SendResult::Ok);
    CHECK(ann.core.Send("t.small", x, 5) == SendResult::Ok);
    CHECK(RunUntil(net, all, net.now + 3000, [&] { return cid.Count("t.small") == 1; }));
    CHECK(cid.core.GetStats().refused == refused0 + 1 && cid.got.back().data.size() == 5);

    // Late registration: Bob and Cid register a new channel mid-session; Bob's message reaches Cid.
    CHECK(bob.core.RegisterChannel("late.x", 0, 100));
    CHECK(cid.core.RegisterChannel("late.x", 0, 100));
    CHECK(bob.core.Send("late.x", x, 2) == SendResult::UnknownChannel);   // the table update hasn't arrived
    // Unreliable, so Bob sends each step until Cid has one: until both registrations reached the
    // host, a send may find no one to relay to.
    CHECK(RunUntil(net, all, net.now + 3000, [&] {
        bob.core.Send("late.x", x, 2);
        return cid.Count("late.x") >= 1;
    }));

    // A clean leave: everyone sees Bob go with his reason.
    bob.core.Leave("dinner");
    CHECK(bob.core.GetState() == State::Idle && bob.lastDown == "dinner");
    CHECK(RunUntil(net, all, net.now + 3000, [&] { return host.core.Peers().size() == 3 && ann.core.Peers().size() == 3; }));
    CHECK(host.events.back() == "L3:left: dinner" && ann.events.back() == "L3:left: dinner");

    // A silent peer times out: Cid stops pumping; after peerTimeoutMs the host drops it.
    std::vector<Node*> live{ &host, &ann };
    const uint64_t t0 = net.now;
    CHECK(RunUntil(net, live, net.now + 40000, [&] { return host.core.Peers().size() == 2; }, 50));
    CHECK(net.now - t0 >= 29000 && host.events.back() == "L4:timed out");
    CHECK(RunUntil(net, live, net.now + 3000, [&] { return ann.core.Peers().size() == 2; }));

    // Bob rejoins and gets a new id.
    CHECK(bob.core.Join(Opts("Bob", "pw", 40), host.tr.Self(), net.now));
    CHECK(RunUntil(net, { &host, &ann, &bob }, net.now + 5000, [&] { return Joined(bob); }));
    CHECK(bob.core.Self() == 5);

    // The host leaves: the joiners are told why.
    host.core.Leave("closing");
    CHECK(RunUntil(net, { &host, &ann, &bob }, net.now + 3000, [&] {
        return ann.core.GetState() == State::Idle && bob.core.GetState() == State::Idle;
    }));
    CHECK(ann.lastDown == "the host ended the session: closing" && host.core.Peers().empty());
    CHECK(ann.core.Send("g.chat", x, 1) == SendResult::NoSession);

    // Cid wakes up much later. It drains what the host sent before dropping it (that counts as
    // hearing from the host), then nothing more comes: it ends on its own timeout.
    CHECK(RunUntil(net, { &cid }, net.now + 40000, [&] { return cid.core.GetState() == State::Idle; }, 50));
    CHECK(cid.lastDown == "the host timed out");
}

// A session at the limit: the host and 50 joiners, 51 players in all (the limit was 16). Nothing on the
// wire is sized by the limit (docs/net-wire.md, "Session size"), so the joiners keep the default
// maxPeers of 8, which a joiner never reads: they stand for joiners built for the old limit. They all
// start in the same moment (TestJoinBurst looks at that closely).
static void TestSessionLimit() {
    constexpr uint32_t kSeats = 51;
    static_assert(SC_NET_MAX_PEERS == kSeats && kMaxPeers == kSeats, "the session limit is 51 players");
    MemNet net(51);
    std::vector<std::unique_ptr<Node>> nodes;   // [0] the host, [1..50] the joiners, [51] one too many
    for (uint32_t i = 0; i <= kSeats; ++i) nodes.push_back(std::make_unique<Node>(net, static_cast<uint16_t>(i + 1)));
    for (auto& n : nodes) CHECK(n->core.RegisterChannel("g.chat", kReliable, 100));
    Node& host = *nodes[0];
    Options ho = Opts("Host", "pw", 1);
    ho.maxPeers = kSeats;
    CHECK(host.core.Host(ho, 0));

    std::vector<Node*> live{ &host };
    for (uint32_t i = 1; i < kSeats; ++i) {
        // Two steps: GCC 12 at -O2 reports a false -Wrestrict for "P" + std::to_string(i).
        std::string name = "P";
        name += std::to_string(i);
        CHECK(nodes[i]->core.Join(Opts(name.c_str(), "pw", 1000 + i), host.tr.Self(), net.now));
        live.push_back(nodes[i].get());
    }
    CHECK(RunUntil(net, live, net.now + 20000, [&] {
        for (uint32_t i = 1; i < kSeats; ++i)
            if (!Joined(*nodes[i])) return false;
        return host.core.Peers().size() == kSeats;
    }));

    // Every player ends up with all 51 on its list, and the ids run 1..51 in join order.
    CHECK(RunUntil(net, live, net.now + 5000, [&] {
        for (Node* n : live)
            if (n->core.Peers().size() != kSeats) return false;
        return true;
    }));
    const std::vector<PeerInfo> roster = host.core.Peers();
    CHECK(roster.size() == kSeats && roster.front().id == kHostPeer && roster.back().id == kSeats);
    for (uint32_t i = 1; i < kSeats; ++i)
        CHECK(nodes[i]->core.Self() == i + 1 && nodes[i]->core.Peers().size() == kSeats);
    std::string name;
    CHECK(nodes[1]->core.PeerName(kSeats, &name) && name == "P50");

    // The host speaks: all 50 joiners hear it once, from the host.
    const uint8_t hi[2] = { 'h', 'i' };
    CHECK(host.core.Send("g.chat", hi, 2) == SendResult::Ok);
    CHECK(RunUntil(net, live, net.now + 3000, [&] {
        for (uint32_t i = 1; i < kSeats; ++i)
            if (nodes[i]->Count("g.chat") != 1) return false;
        return true;
    }));
    for (uint32_t i = 1; i < kSeats; ++i)
        CHECK(nodes[i]->got.size() == 1 && nodes[i]->got[0].from == kHostPeer && nodes[i]->got[0].data.size() == 2);

    // The last joiner speaks: the host and the 49 other joiners hear it once, from it, and it hears nothing back.
    Node& last = *nodes[kSeats - 1];
    CHECK(last.core.Send("g.chat", hi, 2) == SendResult::Ok);
    CHECK(RunUntil(net, live, net.now + 3000, [&] {
        if (host.Count("g.chat") != 1) return false;
        for (uint32_t i = 1; i < kSeats - 1; ++i)
            if (nodes[i]->Count("g.chat") != 2) return false;
        return true;
    }));
    RunUntil(net, live, net.now + 500, [] { return false; });   // a stray copy would show up now
    CHECK(last.Count("g.chat") == 1 && host.Count("g.chat") == 1 && host.got[0].from == last.core.Self());
    for (uint32_t i = 1; i < kSeats - 1; ++i)
        CHECK(nodes[i]->Count("g.chat") == 2 && nodes[i]->got.back().from == last.core.Self());

    // One more is refused: the session is full, nobody else notices.
    Node& extra = *nodes[kSeats];
    const uint64_t refused0 = host.core.GetStats().joinsRefused;
    const size_t hostEvents = host.events.size();
    CHECK(extra.core.Join(Opts("Extra", "pw", 2000), host.tr.Self(), net.now));
    live.push_back(&extra);
    CHECK(RunUntil(net, live, net.now + 5000, [&] { return extra.core.GetState() == State::Idle; }));
    CHECK(extra.lastDown == "refused: the session is full" && host.core.GetStats().joinsRefused == refused0 + 1);
    CHECK(host.core.Peers().size() == kSeats && host.events.size() == hostEvents);

    // A freed seat can be taken again. Ids are never reused, so the newcomer is id 52 in a 51-seat session.
    nodes[10]->core.Leave("seat");
    CHECK(RunUntil(net, live, net.now + 3000, [&] { return host.core.Peers().size() == kSeats - 1; }));
    CHECK(extra.core.Join(Opts("Extra", "pw", 2001), host.tr.Self(), net.now));
    CHECK(RunUntil(net, live, net.now + 5000, [&] { return Joined(extra) && host.core.Peers().size() == kSeats; }));
    CHECK(extra.core.Self() == kSeats + 1);
}

// Joiners that start in the same moment. Every HELLO reaches the host before any PROOF does (a joiner sends
// its PROOF only after the CHALLENGE, which the host sends while it is still reading the HELLOs), so the host
// has that many half-open joins at once. It used to remember 32 and forget the oldest of the rest: with 50
// joiners, 18 never got in and gave up after their join timeout. Now every joiner that fits is seated, every
// one that does not is refused as full (promptly, not by timing out), and no handshake packet is dropped.
static void BurstCase(uint32_t joiners) {
    const uint32_t seats = std::min<uint32_t>(joiners, kMaxPeers - 1);
    const uint32_t refused = joiners - seats;
    MemNet net(joiners);
    std::vector<std::unique_ptr<Node>> nodes;   // [0] the host, then the joiners
    for (uint32_t i = 0; i <= joiners; ++i) nodes.push_back(std::make_unique<Node>(net, static_cast<uint16_t>(i + 1)));
    Node& host = *nodes[0];
    Options ho = Opts("Host", "pw", 1);
    ho.maxPeers = kMaxPeers;
    CHECK(host.core.Host(ho, 0));

    // The tap sees each datagram as it is sent: count the HELLOs and the CHALLENGEs, and how many CHALLENGEs
    // had gone out when the first PROOF was sent.
    uint32_t hellos = 0, challenges = 0, challengesAtFirstProof = 0;
    bool sawProof = false;
    net.tap = [&](MemNet::Datagram& d) {
        const uint8_t kind = d.bytes.size() > SC_NET_OFF_KIND ? d.bytes[SC_NET_OFF_KIND] : 0;
        if (kind == SC_NET_HELLO) ++hellos;
        if (kind == SC_NET_CHALLENGE) ++challenges;
        if (kind == SC_NET_PROOF && !sawProof) {
            sawProof = true;
            challengesAtFirstProof = challenges;
        }
        return true;
    };

    std::vector<Node*> live{ &host };
    for (uint32_t i = 1; i <= joiners; ++i) {
        // Two steps for the same GCC 12 reason as TestSessionLimit's names.
        std::string name = "B";
        name += std::to_string(i);
        CHECK(nodes[i]->core.Join(Opts(name.c_str(), "pw", 3000 + i), host.tr.Self(), net.now));
        live.push_back(nodes[i].get());
    }
    CHECK(RunUntil(net, live, net.now + 20000, [&] {
        uint32_t in = 0, out = 0;
        for (uint32_t i = 1; i <= joiners; ++i) {
            if (Joined(*nodes[i])) ++in;
            if (nodes[i]->core.GetState() == State::Idle) ++out;
        }
        return in == seats && out == refused;
    }));

    CHECK(hellos == joiners && challengesAtFirstProof == joiners);   // all the HELLOs were in before the first PROOF
    const Stats& st = host.core.GetStats();
    CHECK(st.handshakeDropped == 0 && st.joinsRefused == refused);
    CHECK(host.core.Peers().size() == seats + 1);
    uint32_t full = 0;
    std::set<PeerId> ids;
    for (uint32_t i = 1; i <= joiners; ++i) {
        if (Joined(*nodes[i])) ids.insert(nodes[i]->core.Self());
        else if (nodes[i]->lastDown == "refused: the session is full") ++full;
    }
    CHECK(ids.size() == seats && full == refused);
}

static void TestJoinBurst() {
    BurstCase(kMaxPeers - 1);   // 50 joiners for 50 seats: all of them get in
    BurstCase(kMaxPeers + 9);   // 60 for 50: 50 get in, 10 are told the session is full
}

// ---- servers: a host that takes no player seat --------------------------------------------------------------

// A real server host and a real player host, each with two joiners. The 2.2.0 host sends no HOST_ROLE, so a
// player host here stands for it: a joiner that gets no role reads a player. Everyone must agree on who is
// the server, the joiners must know the role by the time the host's JOINED event fires, nothing may be
// counted malformed on either side, and a joiner that moves to a host of the other kind keeps no memory of
// the first.
static void TestServerHost() {
    for (const bool server : { true, false }) {
        MemNet net(server ? 31 : 32);
        Node host(net, 1), ann(net, 2), bob(net, 3);
        std::vector<Node*> all{ &host, &ann, &bob };
        for (Node* n : all) CHECK(n->core.RegisterChannel("g.chat", kReliable, 100));
        Options ho = Opts("Host", "pw", 1);
        ho.server = server;
        CHECK(host.core.Host(ho, 0));
        CHECK(ann.core.Join(Opts("Ann", "pw", 10), host.tr.Self(), 0));
        CHECK(bob.core.Join(Opts("Bob", "pw", 11), host.tr.Self(), 0));
        CHECK(RunUntil(net, all, 5000, [&] {
            return Joined(ann) && Joined(bob) && host.core.Peers().size() == 3 && ann.core.Peers().size() == 3 &&
                   bob.core.Peers().size() == 3;
        }));
        const uint32_t hostFlags = server ? kPeerServer : 0u;
        for (Node* n : all) {
            const std::vector<PeerInfo> peers = n->core.Peers();
            CHECK(peers.size() == 3 && peers[0].id == n->core.Self());
            for (const PeerInfo& p : peers) CHECK(p.flags == (p.id == kHostPeer ? hostFlags : 0u));
        }
        CHECK(ann.hostFlagsAtJoin == static_cast<int>(hostFlags) && bob.hostFlagsAtJoin == static_cast<int>(hostFlags));
        CHECK(host.core.Self() == kHostPeer && ann.core.Self() == 2 && bob.core.Self() == 3);
        for (Node* n : all) CHECK(n->core.GetStats().malformed == 0);

        // A server relays like any host: Bob's message reaches the host and Ann, once, from Bob.
        const uint8_t hi[2] = { 'h', 'i' };
        CHECK(bob.core.Send("g.chat", hi, 2) == SendResult::Ok);
        CHECK(RunUntil(net, all, net.now + 3000, [&] { return host.Count("g.chat") == 1 && ann.Count("g.chat") == 1; }));
        CHECK(host.got[0].from == 3 && ann.got[0].from == 3);

        // Ann moves to a host of the other kind. Options::server means nothing to a joiner: it never marks
        // her own entry.
        Node other(net, 4);
        Options oo = Opts("Other", "pw", 2);
        oo.server = !server;
        CHECK(other.core.Host(oo, net.now));
        all.push_back(&other);
        ann.core.Leave("moving");
        ann.hostFlagsAtJoin = -1;
        Options jo = Opts("Ann", "pw", 12);
        jo.server = true;
        CHECK(ann.core.Join(jo, other.tr.Self(), net.now));
        CHECK(RunUntil(net, all, net.now + 5000, [&] { return Joined(ann) && ann.core.Peers().size() == 2; }));
        const uint32_t otherFlags = server ? 0u : kPeerServer;
        const std::vector<PeerInfo> moved = ann.core.Peers();
        CHECK(moved.size() == 2 && moved[0].flags == 0 && moved[1].id == kHostPeer && moved[1].flags == otherFlags);
        CHECK(ann.hostFlagsAtJoin == static_cast<int>(otherFlags));
        CHECK(other.core.Peers().front().flags == otherFlags);
    }
}

// Joiners fill a session until the host says it is full: `admits` must get in, and the next one must be
// refused with "the session is full" without anyone else noticing. They join in waves of 25 because the
// host remembers at most kMaxPending (32) half-open joins at once; players arrive one by one in real use.
// Every roster must agree on which entry, if any, is the server.
static void FillSeats(const char* label, bool server, uint32_t maxPeers, uint32_t admits) {
    const int before = g_fail;
    MemNet net(500 + maxPeers + admits + (server ? 100 : 0));
    std::vector<std::unique_ptr<Node>> nodes;   // [0] the host, [1..admits] the joiners, [admits + 1] one too many
    for (uint32_t i = 0; i <= admits + 1; ++i) nodes.push_back(std::make_unique<Node>(net, static_cast<uint16_t>(i + 1)));
    Node& host = *nodes[0];
    Options ho = Opts("Host", "pw", 1);
    ho.maxPeers = maxPeers;
    ho.server = server;
    CHECK(host.core.Host(ho, 0));

    std::vector<Node*> live{ &host };
    size_t joined = 0;
    while (joined < admits) {
        const size_t wave = std::min<size_t>(25, admits - joined);
        for (size_t k = 0; k < wave; ++k) {
            Node& n = *nodes[++joined];
            const std::string name = "P" + std::to_string(joined);
            CHECK(n.core.Join(Opts(name.c_str(), "pw", 1000 + joined), host.tr.Self(), net.now));
            live.push_back(&n);
        }
        CHECK(RunUntil(net, live, net.now + 20000, [&] {
            for (size_t i = 1; i <= joined; ++i)
                if (!Joined(*nodes[i])) return false;
            return host.core.Peers().size() == joined + 1;
        }));
    }
    CHECK(RunUntil(net, live, net.now + 5000, [&] {
        for (Node* n : live)
            if (n->core.Peers().size() != admits + 1) return false;
        return true;
    }));
    for (Node* n : live) {
        const std::vector<PeerInfo> peers = n->core.Peers();
        CHECK(peers.size() == admits + 1);
        for (const PeerInfo& p : peers) CHECK(p.flags == (p.id == kHostPeer && server ? kPeerServer : 0u));
    }

    Node& extra = *nodes[admits + 1];
    const uint64_t refused0 = host.core.GetStats().joinsRefused;
    const size_t hostEvents = host.events.size();
    CHECK(extra.core.Join(Opts("Extra", "pw", 2000), host.tr.Self(), net.now));
    live.push_back(&extra);
    CHECK(RunUntil(net, live, net.now + 5000, [&] { return extra.core.GetState() == State::Idle; }));
    CHECK(extra.lastDown == "refused: the session is full" && host.core.GetStats().joinsRefused == refused0 + 1);
    CHECK(host.core.Peers().size() == admits + 1 && host.events.size() == hostEvents);
    if (g_fail != before) std::printf("  seat run failed: %s\n", label);
}

// Player seats and the transport bound are two counts. With 50 seats a player host admits 49 joiners and a
// server admits 50. With a seat budget over the transport bound (51) a server still admits 50, because its
// own slot is the 51st endpoint of the session. Every run refuses the next join.
static void TestSeatAccounting() {
    FillSeats("player host, 50 seats", false, 50, 49);
    FillSeats("server host, 50 seats", true, 50, 50);
    FillSeats("server host, 51 seats: the transport bound holds", true, kMaxPeers, kMaxPeers - 1);
    FillSeats("server host, one seat", true, 1, 1);

    // The options: a player host needs a second seat, a server only one; nobody passes the transport bound.
    MemNet net(41);
    Node n(net, 1);
    auto starts = [&](uint32_t maxPeers, bool server) {
        Options o = Opts("Host", "pw", 1);
        o.maxPeers = maxPeers;
        o.server = server;
        const bool ok = n.core.Host(o, 0);
        n.core.Leave("test");
        return ok;
    };
    CHECK(!starts(0, false) && !starts(1, false) && starts(2, false) && starts(kMaxPeers, false) &&
          !starts(kMaxPeers + 1, false));
    CHECK(!starts(0, true) && starts(1, true) && starts(kMaxPeers, true) && !starts(kMaxPeers + 1, true));
}

// ---- a host played by the test ---------------------------------------------------------------------------

// HMAC under a handshake label: the NUL ends the string and is hashed with it (sc_net.h).
static void LabelMac(const uint8_t key[kKeyBytes], const char* label, const uint8_t cn[kNonceBytes],
                     const uint8_t hn[kNonceBytes], const uint8_t* extra, size_t extraLen, uint8_t out[32]) {
    sco_hmac_sha256 c;
    sco_hmac_sha256_init(&c, key, kKeyBytes);
    sco_hmac_sha256_update(&c, label, std::strlen(label) + 1);
    sco_hmac_sha256_update(&c, cn, kNonceBytes);
    sco_hmac_sha256_update(&c, hn, kNonceBytes);
    if (extraLen) sco_hmac_sha256_update(&c, extra, extraLen);
    sco_hmac_sha256_final(&c, out);
}

// A real Core never sends a bad HOST_ROLE, so to see how a joiner treats one the test plays the host. It
// answers the joiner's HELLO and PROOF with the real handshake (the joiner's own checks run on it), and
// right after WELCOME sends a script of control messages, in order, as reliable DATA units on channel 0.
// The network is lossless here, so it never resends and ignores the joiner's ACKs. The joiner is peer 2.
class FakeHost {
public:
    MemTransport tr;

    FakeHost(MemNet& net, uint16_t id, const char* pass, uint32_t iters, std::vector<std::vector<uint8_t>> script)
        : tr(net, id), script_(std::move(script)) {
        for (uint32_t i = 0; i < kSaltBytes; ++i) salt_[i] = static_cast<uint8_t>(0x30 + i);
        for (uint32_t i = 0; i < kNonceBytes; ++i) hn_[i] = static_cast<uint8_t>(0x70 + i);
        CHECK(sco_pbkdf2_hmac_sha256(pass, std::strlen(pass), salt_, kSaltBytes, iters, k_, kKeyBytes) == 1);
    }

    void Pump() {
        Endpoint from;
        uint8_t buf[kMaxDatagram + 1];
        size_t len = 0;
        while (tr.Receive(&from, buf, sizeof(buf), &len)) {
            Packet p;
            if (len > kMaxDatagram || Parse(buf, len, &p) != ParseError::Ok) continue;
            if (p.h.kind == Kind::Hello && p.h.bodyLen >= kNonceBytes) {
                peer_ = from;
                std::memcpy(cn_, p.body, kNonceBytes);
                std::vector<uint8_t> b;
                Writer w(b);
                w.Bytes(cn_, kNonceBytes);
                w.Bytes(hn_, kNonceBytes);
                w.Bytes(salt_, kSaltBytes);
                SendPlain(Kind::Challenge, b);
            } else if (p.h.kind == Kind::Proof) {
                // The proof itself is not checked: this test is about what comes after WELCOME.
                const uint8_t idb[8] = { 2, 0, 0, 0, 0, 0, 0, 0 };
                uint8_t mac[32];
                LabelMac(k_, SC_NET_LABEL_WELCOME, cn_, hn_, idb, sizeof(idb), mac);
                std::vector<uint8_t> b;
                Writer w(b);
                w.Bytes(cn_, kNonceBytes);
                w.U64(2);
                w.Bytes(mac, sizeof(mac));
                SendPlain(Kind::Welcome, b);
                if (!welcomed_) {
                    welcomed_ = true;
                    uint8_t link[32];
                    LabelMac(k_, SC_NET_LABEL_LINK, cn_, hn_, nullptr, 0, link);
                    key_.Set(link);
                    for (const std::vector<uint8_t>& m : script_) SendControl(m);
                }
            }
        }
    }

private:
    void SendPlain(Kind kind, const std::vector<uint8_t>& body) {
        Header h;
        h.kind = kind;
        uint8_t out[kMaxDatagram];
        const size_t n = Encode(h, body.data(), static_cast<uint32_t>(body.size()), nullptr, {}, out, sizeof(out));
        tr.Send(peer_, out, n);
    }

    void SendControl(const std::vector<uint8_t>& msg) {
        std::vector<uint8_t> b;
        Writer w(b);
        w.U8(static_cast<uint8_t>(SC_NET_DATA_F_RELIABLE));
        w.U8(0);
        w.U16(0);
        w.U64(kHostPeer);                           // the origin: the host speaks
        w.U64(unit_++);                             // the stream's unit number, from 0
        w.U32(static_cast<uint32_t>(msg.size()));   // the whole message is one unit
        w.U32(0);
        w.Bytes(msg.data(), msg.size());
        Header h;
        h.kind = Kind::Data;
        h.channel = 0;
        h.sender = kHostPeer;
        h.seq = ++seq_;
        uint8_t out[kMaxDatagram];
        const size_t n = Encode(h, b.data(), static_cast<uint32_t>(b.size()), &key_, kControlFqn, out, sizeof(out));
        tr.Send(peer_, out, n);
    }

    Endpoint peer_;
    uint8_t salt_[kSaltBytes] = {}, hn_[kNonceBytes] = {}, cn_[kNonceBytes] = {}, k_[kKeyBytes] = {};
    MacKey key_;
    bool welcomed_ = false;
    uint64_t unit_ = 0, seq_ = 0;
    std::vector<std::vector<uint8_t>> script_;
};

static std::vector<uint8_t> RoleMsg(uint8_t flags) {
    return std::vector<uint8_t>{ static_cast<uint8_t>(SC_NET_CTL_HOST_ROLE), flags };
}

// SYNC with no channels and one peer, the host (the joiner's own id is left out, as a real host does).
static std::vector<uint8_t> SyncMsg() {
    std::vector<uint8_t> b;
    Writer w(b);
    w.U8(static_cast<uint8_t>(SC_NET_CTL_SYNC));
    w.U16(0);
    w.U16(1);
    w.U64(kHostPeer);
    w.U16(4);
    w.Bytes("Fake", 4);
    return b;
}

struct RoleRun {
    bool active = false;
    uint32_t hostFlags = 0;
    int flagsAtJoin = -1;
    uint64_t malformed = 0;
    size_t peers = 0;
};

// A real joiner against the scripted host: what it ends up believing, and what it counted malformed.
static RoleRun RunRoleScript(uint64_t seed, std::vector<std::vector<uint8_t>> script) {
    MemNet net(seed);
    FakeHost fake(net, 1, "pw", 1000, std::move(script));
    Node ann(net, 2);
    CHECK(ann.core.Join(Opts("Ann", "pw", seed), fake.tr.Self(), 0));
    for (int step = 0; step < 200; ++step) {   // one second of virtual time, far more than the exchange needs
        fake.Pump();
        ann.core.Pump(net.now);
        net.now += 5;
    }
    RoleRun r;
    r.active = Joined(ann);
    for (const PeerInfo& p : ann.core.Peers()) {
        ++r.peers;
        if (p.id == kHostPeer) r.hostFlags = p.flags;
    }
    r.flagsAtJoin = ann.hostFlagsAtJoin;
    r.malformed = ann.core.GetStats().malformed;
    return r;
}

// What a joiner does with HOST_ROLE messages a real host never sends. Every bad one is counted malformed,
// changes nothing, and leaves the session to come up.
static void TestHostRoleHostile() {
    // No role: a player host, as every 2.2.0 host is.
    RoleRun r = RunRoleScript(51, { SyncMsg() });
    CHECK(r.active && r.peers == 2 && r.hostFlags == 0 && r.flagsAtJoin == 0 && r.malformed == 0);
    // The role, then SYNC: known when the host's JOINED event fires.
    r = RunRoleScript(52, { RoleMsg(1), SyncMsg() });
    CHECK(r.active && r.peers == 2 && r.hostFlags == kPeerServer && r.flagsAtJoin == 1 && r.malformed == 0);
    // Said to be no server: harmless.
    r = RunRoleScript(53, { RoleMsg(0), SyncMsg() });
    CHECK(r.active && r.hostFlags == 0 && r.flagsAtJoin == 0 && r.malformed == 0);
    // Twice: the second is malformed, the first stands, even when the second disagrees.
    r = RunRoleScript(54, { RoleMsg(1), RoleMsg(1), SyncMsg() });
    CHECK(r.active && r.hostFlags == kPeerServer && r.flagsAtJoin == 1 && r.malformed == 1);
    r = RunRoleScript(55, { RoleMsg(1), RoleMsg(0), SyncMsg() });
    CHECK(r.active && r.hostFlags == kPeerServer && r.flagsAtJoin == 1 && r.malformed == 1);
    // After SYNC it is too late, the roster was already told: malformed and not applied.
    r = RunRoleScript(56, { SyncMsg(), RoleMsg(1) });
    CHECK(r.active && r.hostFlags == 0 && r.flagsAtJoin == 0 && r.malformed == 1);
    // A bit nobody defined, a message too short and one too long: malformed, not applied.
    r = RunRoleScript(57, { RoleMsg(0x03), SyncMsg() });
    CHECK(r.active && r.hostFlags == 0 && r.malformed == 1);
    r = RunRoleScript(58, { RoleMsg(0x80), SyncMsg() });
    CHECK(r.active && r.hostFlags == 0 && r.malformed == 1);
    r = RunRoleScript(59, { std::vector<uint8_t>{ 6 }, SyncMsg() });
    CHECK(r.active && r.hostFlags == 0 && r.malformed == 1);
    r = RunRoleScript(60, { std::vector<uint8_t>{ 6, 1, 0 }, SyncMsg() });
    CHECK(r.active && r.hostFlags == 0 && r.malformed == 1);
    // A bad one first does not block a good one after it.
    r = RunRoleScript(61, { RoleMsg(0x02), RoleMsg(1), SyncMsg() });
    CHECK(r.active && r.hostFlags == kPeerServer && r.flagsAtJoin == 1 && r.malformed == 1);
    // A control type this joiner has never heard of, before SYNC. A joiner built before HOST_ROLE takes the
    // same branch for type 6 (the default case of its switch, see docs/net-wire.md, "Host role"): one
    // malformed message, nothing else, and the SYNC behind it is accepted.
    r = RunRoleScript(62, { std::vector<uint8_t>{ 200, 1 }, SyncMsg() });
    CHECK(r.active && r.peers == 2 && r.hostFlags == 0 && r.flagsAtJoin == 0 && r.malformed == 1);
}

static void Run(const char* name, void (*fn)()) {
    const int before = g_fail;
    fn();
    std::printf("%s %s\n", g_fail == before ? "ok  " : "FAIL", name);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Run("TestSha256", TestSha256);
    Run("TestHmac", TestHmac);
    Run("TestPbkdf2", TestPbkdf2);
    Run("TestCtEqual", TestCtEqual);
    Run("TestWire", TestWire);
    Run("TestScNet", TestScNet);
    Run("TestReplay", TestReplay);
    Run("TestStreams", TestStreams);
    Run("TestHandshake", TestHandshake);
    Run("TestDefaultIterations", TestDefaultIterations);
    Run("TestRefusals", TestRefusals);
    Run("TestHostile", TestHostile);
    Run("TestReliableLossy(1)", [] { TestReliableLossy(1); });
    Run("TestReliableLossy(2)", [] { TestReliableLossy(2); });
    Run("TestReliableLossy(3)", [] { TestReliableLossy(3); });
    Run("TestReliableLossy(4)", [] { TestReliableLossy(4); });
    Run("TestPeers", TestPeers);
    Run("TestSessionLimit", TestSessionLimit);
    Run("TestJoinBurst", TestJoinBurst);
    Run("TestServerHost", TestServerHost);
    Run("TestSeatAccounting", TestSeatAccounting);
    Run("TestHostRoleHostile", TestHostRoleHostile);
    std::printf("sco-core net tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
