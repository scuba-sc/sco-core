// The host fixture owns one game registry. Each resolution publishes a new snapshot; the
// lease remains held while this test reads results from it.
static std::shared_ptr<sco::SignatureRegistry> g_gameRegistry;
static std::shared_ptr<const sco::SignatureSnapshot> g_gameSnapshot;
static bool RegisterGameFixture() {
    if (g_gameRegistry) return true;
    g_gameRegistry = std::make_shared<sco::SignatureRegistry>();
    return sco::game::RegisterGameSignatures(*g_gameRegistry).status == sco::SigRegistryStatus::Ok &&
           g_gameRegistry->Freeze() == sco::SigRegistryStatus::Ok;
}
static void ResolveGameFixture(const sco::Image& img) {
    if (g_gameSnapshot) {
        // This synthetic fixture has no active host services; retire its old owner first.
        CHECK(sco::ClearHostSignatureRegistry(g_gameRegistry, nullptr));
        g_gameSnapshot.reset();
        g_gameRegistry.reset();
        CHECK(RegisterGameFixture());
    }
    CHECK(g_gameRegistry->Resolve(img) == sco::SigRegistryStatus::Ok);
    g_gameSnapshot = g_gameRegistry->Snapshot();
    CHECK(g_gameSnapshot != nullptr);
    CHECK(sco::InstallHostSignatureRegistry(g_gameRegistry, nullptr));
}
static const sco::SigResult* GameResult(const char* id) {
    return g_gameSnapshot ? g_gameSnapshot->Lookup(id) : nullptr;
}
// SPDX-License-Identifier: GPL-3.0-only
// Game-only test cases; included by the corresponding test when the game pack is enabled.
// A fake image with CSystem::Quit's log string and a function laid out like build 4.10.193.11644:
// the bytes system.quit checks, the string's lea r9 at +0xA3 and +0x176, 0xCC everywhere else.
// `flip` changes one byte of the function (offset from its start), < 0 for none.
static uint8_t g_quit[0x600];
static uint8_t* QuitImage(sco::Image& img, bool withString, int flip) {
    memset(g_quit, 0xCC, sizeof(g_quit));
    img = {};
    img.base = g_quit;
    img.size = sizeof(g_quit);
    img.text = { g_quit + 0x100, 0x300 };
    img.rdata = { g_quit + 0x400, 0x200 };
    memset(img.rdata.base, 0, img.rdata.size);
    const char* fmt = "CSystem::Quit invoked with - cause=$$, reason=$$, exitCode=$$, thread id=$$, main thread id=$$";
    uint8_t* s = img.rdata.base + 0x20;
    if (withString) strcpy(reinterpret_cast<char*>(s), fmt);
    uint8_t* f = img.text.base + 0x20;
    static const struct { size_t off; std::vector<uint8_t> bytes; } kBytes[] = {
        { 0x000, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20 } },
        { 0x00F, { 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 } },
        { 0x018, { 0x48, 0x8D, 0xAC, 0x24, 0x00, 0xFF, 0xFF, 0xFF } },
        { 0x020, { 0x48, 0x81, 0xEC, 0x00, 0x02, 0x00, 0x00 } },
        { 0x027, { 0x45, 0x8B, 0xF8, 0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xF1 } },
        { 0x035, { 0x41, 0x83, 0xF8, 0x4E } },
        { 0x0BB, { 0x4C, 0x8D, 0x05, 0, 0, 0, 0 } },
        { 0x0DD, { 0xE8, 0, 0, 0, 0 } },
    };
    for (const auto& b : kBytes) memcpy(f + b.off, b.bytes.data(), b.bytes.size());
    for (size_t off : { size_t{ 0xA3 }, size_t{ 0x176 } }) {
        uint8_t* lea = f + off;
        lea[0] = 0x4C; lea[1] = 0x8D; lea[2] = 0x0D;
        const int32_t rel = static_cast<int32_t>(s - (lea + 7));
        memcpy(lea + 3, &rel, 4);
    }
    if (flip >= 0) f[flip] = static_cast<uint8_t>(f[flip] ^ 0x01);
    return f;
}

static void TestSystemQuit() {
    using S = sco::SigState;
    CHECK(RegisterGameFixture());
    sco::Image img;
    uint8_t* f = QuitImage(img, true, -1);
    ResolveGameFixture(img);
    CHECK(GameResult("system.quit")->state == S::Ok && sco::Sig("system.quit") == f);
    sco::game::QuitHook h;
    CHECK(sco::game::QuitFunction(h) && h.fn == f && h.stolenBytes == 15);
    static_assert(sco::game::kQuitStolenBytes == 15, "quit hook stolen extent");
    CHECK(!sco::Sig("teleport.to_camera"));   // the other game rows don't find this image

    QuitImage(img, true, 0x0B);   // one prologue byte, inside the stolen bytes
    ResolveGameFixture(img);
    const sco::SigResult* r = GameResult("system.quit");
    CHECK(r->state == S::Failed && strcmp(r->why, "layout changed at +0x000") == 0);
    sco::game::QuitHook untouched;
    CHECK(!sco::game::QuitFunction(untouched) && untouched.fn == nullptr && untouched.stolenBytes == 0);
    g_lines.clear();
    sco::LogSignatureReport(*g_gameSnapshot, false);
    CHECK(Logged("[core] FAILED   system.quit (layout changed at +0x000)"));

    QuitImage(img, true, 0x179);   // the second reference no longer reads the log string
    ResolveGameFixture(img);
    r = GameResult("system.quit");
    CHECK(r->state == S::Failed && strcmp(r->why, "second Quit log line not at +0x176") == 0);

    QuitImage(img, false, -1);
    ResolveGameFixture(img);
    CHECK(GameResult("system.quit")->state == S::Missing && !sco::Sig("system.quit"));
    g_lines.clear();
    sco::LogSignatureReport(*g_gameSnapshot, false);
    CHECK(Logged("[core] MISSING  system.quit"));
}

// A fake image for the pak.* rows: the DataCore loader laid out like build 4.10.193 (prologue,
// the CryPak FOpen load at +0x40, read/seek/close calls), with the "DCB file is smaller than
// expected" lea r9 in a chained funclet (.pdata entry 1, chained to entry 0, the loader).
static uint8_t g_pakImg[0x4000];
struct PakLayout { uint8_t* loader; uint8_t* global; uint8_t* readCall; };
static PakLayout PakImage(sco::Image& img, bool withString) {
    memset(g_pakImg, 0xCC, sizeof(g_pakImg));
    img = {};
    img.base = g_pakImg;
    img.size = sizeof(g_pakImg);
    img.text = { g_pakImg + 0x1000, 0x2800 };
    img.rdata = { g_pakImg + 0x3800, 0x400 };
    img.pdata = { g_pakImg + 0x3C00, 3 * 12 };
    memset(img.rdata.base, 0, img.rdata.size);
    uint8_t* msg = img.rdata.base + 0x20;
    if (withString) strcpy(reinterpret_cast<char*>(msg), "DCB file is smaller than expected");
    PakLayout l{ img.text.base + 0x100, img.rdata.base + 0x200, nullptr };
    static const uint8_t kPrologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18,
                                         0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
    memcpy(l.loader, kPrologue, sizeof kPrologue);
    static const uint8_t kOpen[] = { 0x48, 0x8B, 0x0D, 0, 0, 0, 0, 0x4C, 0x8D, 0x05, 0, 0, 0, 0, 0x48, 0x8B, 0x55, 0x10,
                                     0x45, 0x33, 0xC9, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0x48, 0x01, 0x00, 0x00 };
    uint8_t* open = l.loader + 0x40;
    memcpy(open, kOpen, sizeof kOpen);
    const int32_t g = static_cast<int32_t>(l.global - (open + 7));
    memcpy(open + 3, &g, 4);
    const auto call = [](uint8_t* at, uint32_t slot) { at[0] = 0xFF; at[1] = 0x90; memcpy(at + 2, &slot, 4); };
    l.readCall = l.loader + 0x200;
    call(l.readCall, 0x160);
    call(l.loader + 0x300, 0x1D0);
    call(l.loader + 0x2300, 0x1E0);
    uint8_t* lea = l.loader + 0x1000;   // in the funclet
    lea[0] = 0x4C; lea[1] = 0x8D; lea[2] = 0x0D;
    const int32_t m = static_cast<int32_t>(msg - (lea + 7));
    memcpy(lea + 3, &m, 4);
    // .pdata: [0x1100, 0x1800) the loader, [0x1800, 0x3600) its funclet (chained), [0x3600, 0x3700) another.
    // Unwind info at rdata+0x300 (primary) and rdata+0x310 (UNW_FLAG_CHAININFO, then the parent entry).
    const uint32_t u0 = 0x3B00, u1 = 0x3B10;
    const uint32_t pdata[9] = { 0x1100, 0x1800, u0, 0x1800, 0x3600, u1, 0x3600, 0x3700, u0 };
    memcpy(img.pdata.base, pdata, sizeof pdata);
    g_pakImg[u0] = 0x01;
    g_pakImg[u1] = 0x01 | (4 << 3);
    memcpy(g_pakImg + u1 + 4, pdata, 12);
    return l;
}

static void TestPakRows() {
    using S = sco::SigState;
    CHECK(RegisterGameFixture());
    sco::Image img;
    PakLayout l = PakImage(img, true);
    // FunctionStart: the primary entry, through the chain; nothing outside .pdata's ranges.
    CHECK(sco::FunctionStart(img, l.loader + 0x10) == l.loader);
    CHECK(sco::FunctionStart(img, l.loader + 0x1000) == l.loader);         // chained funclet
    CHECK(sco::FunctionStart(img, g_pakImg + 0x3650) == g_pakImg + 0x3600);
    CHECK(sco::FunctionStart(img, g_pakImg + 0x1000) == nullptr);           // before the first entry
    CHECK(sco::FunctionStart(img, g_pakImg + sizeof(g_pakImg)) == nullptr); // outside the image
    sco::Image noPdata = img;
    noPdata.pdata = {};
    CHECK(sco::FunctionStart(noPdata, l.loader) == nullptr);

    ResolveGameFixture(img);
    CHECK(GameResult("pak.datacore_loader")->state == S::Ok && sco::Sig("pak.datacore_loader") == l.loader);
    CHECK(GameResult("pak.crypak")->state == S::Ok && sco::Sig("pak.crypak") == l.global);
    CHECK(GameResult("pak.slots")->state == S::Ok && sco::Sig("pak.slots") == l.readCall);
    sco::game::pak::Targets t;
    CHECK(sco::game::pak::Resolve(t) && t.loader == l.loader && reinterpret_cast<uint8_t*>(t.cryPak) == l.global);
    static_assert(sco::game::pak::kOpenSlot == 0x148 && sco::game::pak::kReadSlot == 0x160);
    static_assert(sco::game::pak::kSeekSlot == 0x1D0 && sco::game::pak::kCloseSlot == 0x1E0);

    l.loader[3] ^= 1;   // the prologue
    ResolveGameFixture(img);
    const sco::SigResult* r = GameResult("pak.datacore_loader");
    CHECK(r->state == S::Failed && strcmp(r->why, "loader prologue changed") == 0);
    CHECK(GameResult("pak.crypak")->state == S::Blocked && GameResult("pak.slots")->state == S::Blocked);
    sco::game::pak::Targets untouched;
    CHECK(!sco::game::pak::Resolve(untouched) && !untouched.loader && !untouched.cryPak);

    l = PakImage(img, true);
    img.pdata = {};
    ResolveGameFixture(img);
    r = GameResult("pak.datacore_loader");
    CHECK(r->state == S::Failed && strcmp(r->why, "invalid .pdata/image") == 0);

    l = PakImage(img, true);
    l.loader[0x2300] = 0xCC;   // no close call
    ResolveGameFixture(img);
    r = GameResult("pak.slots");
    CHECK(r->state == S::Failed && strcmp(r->why, "no call [rax+0x1e0] (CryPak close) in the loader") == 0);
    CHECK(GameResult("pak.crypak")->state == S::Ok && !sco::game::pak::Resolve(untouched));

    l = PakImage(img, true);
    l.loader[0x41] = 0x8A;   // the CryPak load
    ResolveGameFixture(img);
    r = GameResult("pak.crypak");
    CHECK(r->state == S::Failed && strcmp(r->why, "CryPak FOpen call not in the loader's first 0x400 bytes") == 0);

    l = PakImage(img, true);   // a second lea r9 of the message, in another function
    uint8_t* lea2 = g_pakImg + 0x3650;
    memcpy(lea2, l.loader + 0x1000, 3);
    const int32_t m2 = static_cast<int32_t>((img.rdata.base + 0x20) - (lea2 + 7));
    memcpy(lea2 + 3, &m2, 4);
    ResolveGameFixture(img);
    CHECK(GameResult("pak.datacore_loader")->state == S::Ambiguous && GameResult("pak.datacore_loader")->matches == 2);

    PakImage(img, false);
    ResolveGameFixture(img);
    CHECK(GameResult("pak.datacore_loader")->state == S::Missing);
    g_lines.clear();
    sco::LogSignatureReport(*g_gameSnapshot, false);
    CHECK(Logged("[core] MISSING  pak.datacore_loader") && Logged("[core] BLOCKED  pak.crypak (needs pak.datacore_loader)"));
}

// A fake image for the asop.on_request_open rows: the prologue, shard gate site 1 at +0x21 and the
// guards of build 4.10.196 (+0x189, +0x616, +0x958, the jmp at +0x95E to +0xDC0, the client gate
// at +0x963) with the two globals in .rdata.
static uint8_t g_asopImg[0x1800];
struct AsopLayout { uint8_t* open; uint8_t* shardPersisted; uint8_t* clientGate; };
static AsopLayout AsopImage(sco::Image& img) {
    memset(g_asopImg, 0xCC, sizeof(g_asopImg));
    img = {};
    img.base = g_asopImg;
    img.size = sizeof(g_asopImg);
    img.text = { g_asopImg + 0x100, 0x1000 };
    img.rdata = { g_asopImg + 0x1100, 0x700 };
    memset(img.rdata.base, 0, img.rdata.size);
    AsopLayout l{ img.text.base + 0x20, img.rdata.base + 0x40, img.rdata.base + 0x48 };
    const auto put = [&](size_t off, std::vector<uint8_t> b) { memcpy(l.open + off, b.data(), b.size()); };
    const auto rel = [&](size_t insn, size_t disp, size_t len, const uint8_t* to) {
        const int32_t r = static_cast<int32_t>(to - (l.open + insn + len));
        memcpy(l.open + insn + disp, &r, 4);
    };
    put(0x000, { 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0x80 });
    put(0x021, { 0x44, 0x89, 0xAD, 0xD0, 0x00, 0x00, 0x00, 0x44, 0x38, 0x2D, 0, 0, 0, 0, 0x0F, 0x85, 0x10, 0x00, 0x00, 0x00,
                 0x48, 0x8B, 0x51, 0x08, 0x48, 0x8D, 0x8D, 0xD0, 0x00, 0x00, 0x00, 0xE8 });
    rel(0x028, 3, 7, l.shardPersisted);
    put(0x189, { 0x48, 0x8B, 0x9E, 0xF8, 0x09, 0x00, 0x00 });
    put(0x616, { 0x48, 0x8B, 0xBD, 0xD8, 0x00, 0x00, 0x00 });
    put(0x958, { 0x89, 0xBE, 0xE8, 0x09, 0x00, 0x00 });
    put(0x95E, { 0xE9, 0, 0, 0, 0 });
    rel(0x95E, 1, 5, l.open + 0xDC0);
    put(0x963, { 0x80, 0x3D, 0, 0, 0, 0, 0x00 });
    rel(0x963, 2, 7, l.clientGate);
    return l;
}

static void TestAsopRows() {
    using S = sco::SigState;
    CHECK(RegisterGameFixture());
    sco::Image img;
    AsopLayout l = AsopImage(img);
    ResolveGameFixture(img);
    CHECK(GameResult("asop.on_request_open")->state == S::Ok && sco::Sig("asop.on_request_open") == l.open);
    CHECK(sco::Sig("asop.shard_persisted") == l.shardPersisted && sco::Sig("asop.client_gate") == l.clientGate);
    CHECK(sco::Sig("asop.shard_gate_open") == l.open + 0x21);
    CHECK(GameResult("asop.shard_gate_validation")->state == S::Blocked);   // no Deliver handler here
    CHECK(GameResult("asop.validate_caller")->state == S::Blocked);
    CHECK(GameResult("asop.rm_request_deliver")->state == S::Missing);

    l.open[0x95F] ^= 1;   // the server half's jmp no longer reaches the exit
    ResolveGameFixture(img);
    const sco::SigResult* r = GameResult("asop.on_request_open");
    CHECK(r->state == S::Failed && strcmp(r->why, "jmp at +0x95e doesn't reach +0xdc0") == 0);
    CHECK(GameResult("asop.client_gate")->state == S::Blocked && GameResult("asop.shard_gate_open")->state == S::Blocked);

    l = AsopImage(img);
    l.open[0x618] = 0xB5;   // mov rsi instead of mov rdi
    ResolveGameFixture(img);
    r = GameResult("asop.on_request_open");
    CHECK(r->state == S::Failed && strcmp(r->why, "layout changed at +0x616") == 0);

    l = AsopImage(img);
    memcpy(l.open + 0x41, l.open + 0x21, 32);   // a second shard gate site 1, not at +0x21
    ResolveGameFixture(img);
    CHECK(GameResult("asop.on_request_open")->state == S::Ok);
    CHECK(GameResult("asop.shard_gate_open")->state == S::Ambiguous && GameResult("asop.shard_gate_open")->matches == 2);

    // Every capability names registered rows only, and every asop/atc/hangar row is in one.
    size_t n = 0;
    const sco::game::asop::Capability* caps = sco::game::asop::Capabilities(n);
    CHECK(n == 11);
    for (size_t i = 0; i < n; ++i) {
        CHECK(caps[i].count > 0);
        for (size_t j = 0; j < caps[i].count; ++j) CHECK(GameResult(caps[i].rows[j]) != nullptr);
        for (size_t j = 0; j < i; ++j) CHECK(strcmp(caps[i].name, caps[j].name) != 0);
    }
    size_t feature = 0;
    for (size_t i = 0; i < g_gameSnapshot->Count(); ++i) {
        const char* id = g_gameSnapshot->IdAt(i);
        bool ours = false;
        for (const char* p : { "asop.", "atc.", "hangar.", "lift.", "landing.", "respawn.", "insurance." })
            ours |= strncmp(id, p, strlen(p)) == 0;
        if (!ours) continue;
        ++feature;
        bool listed = false;
        for (size_t c = 0; c < n && !listed; ++c)
            for (size_t j = 0; j < caps[c].count && !listed; ++j) listed = strcmp(caps[c].rows[j], id) == 0;
        CHECK(listed);
    }
    CHECK(feature == 62);
}

// sc-offline's own features (sco/game/features.h): every capability row is registered, and every
// row of the table is in a capability, with the site counts the header promises.
// A row of the actors table (sco/game/actors.h): listed by one of its capabilities.
static bool ActorsRow(const char* id) {
    size_t n = 0;
    const sco::game::actors::Capability* caps = sco::game::actors::Capabilities(n);
    for (size_t c = 0; c < n; ++c)
        for (size_t j = 0; j < caps[c].count; ++j)
            if (strcmp(caps[c].rows[j], id) == 0 && strncmp(id, "teleport.", 9) != 0) return true;
    return false;
}

static void TestActorsRows() {
    CHECK(RegisterGameFixture());
    size_t n = 0;
    const sco::game::actors::Capability* caps = sco::game::actors::Capabilities(n);
    CHECK(caps && n == 12);
    for (size_t c = 0; c < n; ++c)
        for (size_t j = 0; j < caps[c].count; ++j) CHECK(GameResult(caps[c].rows[j]) != nullptr);
    size_t rows = 0;
    for (size_t i = 0; i < g_gameSnapshot->Count(); ++i) {
        const char* id = g_gameSnapshot->IdAt(i);
        bool ours = false;
        for (const char* p : { "spawn.", "npc.", "ammo.", "loadout." }) ours |= strncmp(id, p, strlen(p)) == 0;
        if (!ours || strcmp(id, "spawn.request_fly_mode") == 0 || strcmp(id, "npc.remove_entity_call") == 0) continue;
        ++rows;
        CHECK(ActorsRow(id));
    }
    CHECK(rows == 38);
}

static bool WorldRow(const char* id) {
    size_t n = 0;
    const sco::game::world::Capability* caps = sco::game::world::Capabilities(n);
    for (size_t c = 0; c < n; ++c)
        for (size_t j = 0; j < caps[c].count; ++j)
            if (strcmp(caps[c].rows[j], id) == 0 && strncmp(id, "teleport.", 9) != 0) return true;
    return false;
}

static void TestWorldRows() {
    CHECK(RegisterGameFixture());
    size_t n = 0;
    const sco::game::world::Capability* caps = sco::game::world::Capabilities(n);
    CHECK(caps && n == 11);
    for (size_t c = 0; c < n; ++c)
        for (size_t j = 0; j < caps[c].count; ++j) CHECK(GameResult(caps[c].rows[j]) != nullptr);
    size_t rows = 0;
    for (size_t i = 0; i < g_gameSnapshot->Count(); ++i) {
        const char* id = g_gameSnapshot->IdAt(i);
        bool ours = false;
        for (const char* p : { "build.", "missions.", "cvars.", "quantum." }) ours |= strncmp(id, p, strlen(p)) == 0;
        if (!ours || strcmp(id, "quantum.send_effect_tag") == 0) continue;   // TestFeatureRows
        ++rows;
        CHECK(WorldRow(id));
    }
    CHECK(rows == 42);
}

static void TestMiningRows() {
    CHECK(RegisterGameFixture());
    size_t n = 0;
    const sco::game::mining::Capability* caps = sco::game::mining::Capabilities(n);
    CHECK(caps && n == 1 && strcmp(caps[0].name, "game.mining") == 0 && caps[0].count == 1);
    CHECK(GameResult("mining.cell") != nullptr);
}

// game.events.* (sco/game/events.h): one detour, two events. game.actors.died and game.player.died
// are backed by the very same signature row, which is what makes the ownership fix sound: a consumer
// needing every death subscribes to the new event instead of detouring the game's death function a
// second time, which sco::hook would refuse (one detour per target) and which is exactly the
// collision that took game.player.died down.
static void TestEventRows() {
    CHECK(RegisterGameFixture());
    size_t n = 0;
    const sco::game::events::Capability* caps = sco::game::events::Capabilities(n);
    CHECK(caps && n == 6);
    for (size_t c = 0; c < n; ++c)
        for (size_t j = 0; j < caps[c].count; ++j) CHECK(GameResult(caps[c].rows[j]) != nullptr);
    const sco::game::events::Capability* died = nullptr, *actorDied = nullptr;
    for (size_t c = 0; c < n; ++c) {
        if (strcmp(caps[c].name, "game.events.player_died") == 0) died = &caps[c];
        if (strcmp(caps[c].name, "game.events.actor_died") == 0) actorDied = &caps[c];
    }
    CHECK(died && actorDied);
    CHECK(died && died->count == 1 && strcmp(died->rows[0], "event.player_death") == 0);
    CHECK(actorDied && actorDied->count == 1 && strcmp(actorDied->rows[0], "event.player_death") == 0);
}

static void TestFeatureRows() {
    CHECK(RegisterGameFixture());
    size_t n = 0;
    const sco::game::features::Capability* caps = sco::game::features::Capabilities(n);
    CHECK(caps && n == 5);
    for (size_t c = 0; c < n; ++c)
        for (size_t j = 0; j < caps[c].count; ++j) CHECK(GameResult(caps[c].rows[j]) != nullptr);
    size_t rows = 0, reputation = 0, orLoop = 0;
    for (size_t i = 0; i < g_gameSnapshot->Count(); ++i) {
        const char* id = g_gameSnapshot->IdAt(i);
        bool ours = false;
        for (const char* p : { "spawn.", "npc.", "quantum.", "contracts.reputation", "offline.or_loop_bound." }) ours |= strncmp(id, p, strlen(p)) == 0;
        if (!ours || (ActorsRow(id) && strcmp(id, "npc.remove_entity_call") != 0) || WorldRow(id)) continue;   // TestActorsRows, TestWorldRows
        ++rows;
        reputation += strncmp(id, "contracts.reputation_check.", 27) == 0;
        orLoop += strncmp(id, "offline.or_loop_bound.", 22) == 0;
        bool listed = false;
        for (size_t c = 0; c < n && !listed; ++c)
            for (size_t j = 0; j < caps[c].count && !listed; ++j) listed = strcmp(caps[c].rows[j], id) == 0;
        CHECK(listed);
    }
    CHECK(rows == 18);
    CHECK(reputation == static_cast<size_t>(sco::game::features::kReputationChecks));
    CHECK(orLoop == static_cast<size_t>(sco::game::features::kOrLoopSites));
}

// sc-offline's boot / offline patches and startup hooks (sco/game/offline.h): every capability row
// is registered, every row of the table is in a capability, and the numbered rows match the header.
#include "sco/game/offline.h"
namespace sco::game { extern const SigDef kOfflineSignatures[]; extern const size_t kOfflineSignatureCount; }
static void TestOfflineRows() {
    CHECK(RegisterGameFixture());
    size_t n = 0;
    const sco::game::offline::Capability* caps = sco::game::offline::Capabilities(n);
    CHECK(caps && n == 19);
    for (size_t c = 0; c < n; ++c) {
        CHECK(caps[c].count > 0);
        for (size_t j = 0; j < caps[c].count; ++j) CHECK(GameResult(caps[c].rows[j]) != nullptr);
        for (size_t j = 0; j < c; ++j) CHECK(strcmp(caps[c].name, caps[j].name) != 0);
    }
    CHECK(sco::game::kOfflineSignatureCount == 60);
    size_t handshake = 0, db = 0, social = 0, echo = 0;
    for (size_t i = 0; i < sco::game::kOfflineSignatureCount; ++i) {
        const char* id = sco::game::kOfflineSignatures[i].id;
        CHECK(GameResult(id) != nullptr);
        handshake += strncmp(id, "offline.handshake_gate.", 23) == 0;
        db += strncmp(id, "offline.offline_db_path.", 24) == 0;
        social += strncmp(id, "offline.social_group.", 21) == 0;
        echo += strncmp(id, "offline.service_stream.echo.", 28) == 0;
        bool listed = false;
        for (size_t c = 0; c < n && !listed; ++c)
            for (size_t j = 0; j < caps[c].count && !listed; ++j)
                if (strcmp(caps[c].rows[j], id) == 0) listed = true;
        CHECK(listed);
    }
    CHECK(handshake == static_cast<size_t>(sco::game::offline::kHandshakeSites));
    CHECK(db == static_cast<size_t>(sco::game::offline::kOfflineDbSites));
    CHECK(social == static_cast<size_t>(sco::game::offline::kSocialGroupSites));
    CHECK(echo == static_cast<size_t>(sco::game::offline::kEchoStreams));
}

// sc-offline's contracts (sco/game/contracts.h): every capability row is registered, and every
// contracts.* row of that table is in a capability (the reputation rows are features.h's).
static void TestContractsRows() {
    CHECK(RegisterGameFixture());
    size_t n = 0;
    const sco::game::contracts::Capability* caps = sco::game::contracts::Capabilities(n);
    CHECK(caps && n == 15);
    for (size_t c = 0; c < n; ++c)
        for (size_t j = 0; j < caps[c].count; ++j) CHECK(GameResult(caps[c].rows[j]) != nullptr);
    size_t rows = 0, autoAccept = 0;
    for (size_t i = 0; i < g_gameSnapshot->Count(); ++i) {
        const char* id = g_gameSnapshot->IdAt(i);
        if (strncmp(id, "contracts.", 10) != 0 || strncmp(id, "contracts.reputation", 20) == 0) continue;
        ++rows;
        autoAccept += strncmp(id, "contracts.auto_accept_caller.", 29) == 0;
        bool listed = false;
        for (size_t c = 0; c < n && !listed; ++c)
            for (size_t j = 0; j < caps[c].count && !listed; ++j) listed = strcmp(caps[c].rows[j], id) == 0;
        CHECK(listed);
    }
    CHECK(rows == 82);   // contracts.actor_kill is gone: deaths are the game pack's event now
    CHECK(autoAccept == static_cast<size_t>(sco::game::contracts::kAutoAcceptCallers));
}
// game.entities (sco/game/entities.h): its rows are registered and listed by a group, and the watch
// type filter, a pure function the host runs before calling a plugin, refuses and matches exactly
// what docs/game-services.md says.
static void TestEntitiesRows() {
    CHECK(RegisterGameFixture());
    size_t n = 0;
    const sco::game::entities::Capability* caps = sco::game::entities::Capabilities(n);
    CHECK(caps && n == 3);
    for (size_t c = 0; c < n; ++c)
        for (size_t j = 0; j < caps[c].count; ++j) CHECK(GameResult(caps[c].rows[j]) != nullptr);
    size_t rows = 0;
    for (size_t i = 0; i < g_gameSnapshot->Count(); ++i) {
        const char* id = g_gameSnapshot->IdAt(i);
        if (strncmp(id, "entities.", 9) != 0) continue;
        ++rows;
        bool listed = false;
        for (size_t c = 0; c < n && !listed; ++c)
            for (size_t j = 0; j < caps[c].count && !listed; ++j) listed = strcmp(caps[c].rows[j], id) == 0;
        CHECK(listed);
    }
    CHECK(rows == 6);
    // Both hooks and the walk stay unconfirmed until the in-game run (docs/design/game-world-spikes.md).
    static_assert(!sco::game::entities::kQueryRadiusConfirmed && !sco::game::entities::kWatchConfirmed,
                  "unconfirmed entity reads stay off");
}

static const char* Refused(const char* type) {
    sco::game::entities::TypeFilter f;
    return sco::game::entities::ParseType(type, f);
}

static bool Matches(const char* type, const char* cls) {
    sco::game::entities::TypeFilter f;
    return !sco::game::entities::ParseType(type, f) && sco::game::entities::MatchesType(f, cls);
}

static void TestEntityFilter() {
    using namespace sco::game::entities;
    // refused, each with a reason
    CHECK(Refused(nullptr) && Refused("") && Refused("*"));
    CHECK(Refused("**") && Refused("A**") && Refused("A*B") && Refused("*A") && Refused("*A*"));
    CHECK(Refused("A B") && Refused("A\tB") && Refused("A?") && Refused("A\x01"));
    CHECK(Refused("Caf\xC3\xA9") && Refused("\xC3\xA9*"));   // non-ASCII
    CHECK(strstr(Refused("*"), "lone") && strstr(Refused(""), "empty"));
    const std::string max(kMaxTypeLen, 'A');
    CHECK(!Refused(max.c_str()));                       // 63: the longest
    CHECK(Refused((max + "A").c_str()));                // 64: overlong
    CHECK(Refused((max + "*").c_str()));                // 63 + '*' is 64 characters
    CHECK(!Refused((max.substr(1) + "*").c_str()));     // 62 + '*' is 63
    CHECK(Refused(std::string(100000, 'A').c_str()));   // far past the cap: still refused, not scanned whole
    // accepted
    CHECK(!Refused("AEGS_Avenger_Titan") && !Refused("AEGS_*") && !Refused("a-b.c:d"));
    {
        TypeFilter f;
        CHECK(!ParseType("AEGS_*", f) && f.prefix && f.len == 5 && strcmp(f.text, "AEGS_") == 0);
        CHECK(!ParseType("AEGS_Avenger", f) && !f.prefix && f.len == 12 && strcmp(f.text, "AEGS_Avenger") == 0);
    }
    // matching: exact is exact and case-sensitive; a prefix matches the prefix itself and anything longer
    CHECK(Matches("AEGS_Avenger", "AEGS_Avenger"));
    CHECK(!Matches("AEGS_Avenger", "AEGS_Avenger_Titan") && !Matches("AEGS_Avenger", "AEGS_Aveng"));
    CHECK(!Matches("AEGS_Avenger", "aegs_avenger") && !Matches("AEGS_*", "aegs_avenger"));
    CHECK(Matches("AEGS_*", "AEGS_Avenger_Titan") && Matches("AEGS_*", "AEGS_"));
    CHECK(!Matches("AEGS_*", "AEGS") && !Matches("AEGS_*", "XAEGS_Avenger") && !Matches("AEGS_*", ""));
    CHECK(!Matches("A*", nullptr) && !Matches("AEGS_Avenger", nullptr));
    CHECK(!Matches("AEGS_*", "AEGS*"));   // '*' in a class name isn't a wildcard
    // a filter that was never parsed matches nothing
    CHECK(!MatchesType(TypeFilter{}, "AEGS_Avenger"));
}
