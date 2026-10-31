// Tests for the host kit (sco/app.h) and built-in plugins: sco::app::Start / Tick / Stop with
// built-ins defined here, fake native plugins (the tests/out/plugins layout tools/test.sh and the
// CMake build make for test_plugins), the SDK's greeter Lua example and sample_pack data pack,
// all through the real loader, host table and sco-lua. Host build, no game; this test's main
// thread is the game thread. Off Windows the crash guard is a signal handler installed here (as
// in test_plugins.cpp); on Windows the real __try/__except guard.
//   tools/test.sh
#include "sco/app.h"
#ifndef SCO_KERNEL_ONLY
#include "game_contracts/app_adapter_headers.inc"
#endif
#include "sco/caps.h"
#include "sco/host.h"
#include "sco/log.h"
#include "sco/net/session.h"
#include "sco/plugins.h"
#include "sco/runtime.h"
#include "sco/scan.h"
#include "sco/settings.h"
#include "sco/ui.h"
#include "sco_api.h"
#include "sco_lua.h"
#ifdef SCO_GAME_SERVICES
#include "sco/game/services.h"
#include "game_contracts/app_service_headers.inc"
#endif
#include <algorithm>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include "../src/api/internal.h"   // owned task/drop regressions, no public ABI additions
#include <vector>

namespace fs = std::filesystem;
namespace P = sco::plugins;
using P::State;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::vector<std::string> g_log;
static void Sink(const char* line) { g_log.emplace_back(line); }
// Index of the first log line containing needle, or -1.
static long LogIndex(const char* needle) {
    for (size_t i = 0; i < g_log.size(); ++i) if (g_log[i].find(needle) != std::string::npos) return static_cast<long>(i);
    return -1;
}
static bool Logged(const char* needle) { return LogIndex(needle) >= 0; }

// ---- crash guard (POSIX) --------------------------------------------------------------------

#ifndef _WIN32
static sigjmp_buf g_jump;
static volatile sig_atomic_t g_inGuard = 0;
static void OnFault(int sig) {
    if (g_inGuard) siglongjmp(g_jump, sig);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
static uint32_t SignalGuard(void (*thunk)(void*), void* ctx) {
    struct sigaction sa {}, oldSegv {}, oldBus {};
    sa.sa_handler = OnFault;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &sa, &oldSegv);
    sigaction(SIGBUS, &sa, &oldBus);
    uint32_t code = 0;
    if (sigsetjmp(g_jump, 1)) {
        code = 0xC0000005u;   // report like an access violation on Windows
    } else {
        g_inGuard = 1;
        thunk(ctx);
    }
    g_inGuard = 0;
    sigaction(SIGSEGV, &oldSegv, nullptr);
    sigaction(SIGBUS, &oldBus, nullptr);
    return code;
}
#endif

// A real fault in built-in code. Not instrumented, so the sanitizers don't report the store
// before the guard sees the signal.
#if defined(__clang__) || defined(__GNUC__)
__attribute__((noinline, no_sanitize("address", "undefined")))
#endif
static void Fault() {
    volatile int* volatile p = nullptr;
    *p = 1;
}

// ---- built-in "core1": a command, tick, game.ready and game.exit ------------------------------

static std::vector<std::string> g_events;   // what core1 saw, in order, ticks excluded
static int g_coreTicks = 0;

static sco_result CoreCount(const sco_arg*, uint32_t, void*, char* reply, uint32_t replySize) {
    std::snprintf(reply, replySize, "ticks=%d", g_coreTicks);
    return SCO_OK;
}
static void CoreEvent(const char* event, const void*, void*) {
    if (std::strcmp(event, "tick") == 0) ++g_coreTicks;
    else g_events.push_back(std::string("core1:") + event);
}
static const sco_plugin_info* CoreQuery() {
    static const sco_plugin_info info = { sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "core1", "1.0.0", "tests" };
    return &info;
}
static sco_result CoreLoad(const sco_api* api, sco_plugin* self) {
    g_coreTicks = 0;
    sco_command cmd{};
    cmd.size = sizeof(cmd);
    cmd.name = "core1.count";
    cmd.title = "Count";
    cmd.arg_def_size = sizeof(sco_arg_def);
    cmd.fn = CoreCount;
    sco_result r = api->register_command(self, &cmd);
    for (const char* e : { "game.ready", "tick", "game.exit" })
        if (r == SCO_OK) r = api->subscribe(self, e, CoreEvent, nullptr);
    g_events.push_back("core1:load");
    return r;
}
static void CoreUnload() { g_events.push_back("core1:unload"); }

// ---- built-in "faulty": its tick faults -----------------------------------------------------

static bool g_faultOnTick = false;
static void FaultyTick(const char*, const void*, void*) { if (g_faultOnTick) Fault(); }
static const sco_plugin_info* FaultyQuery() {
    static const sco_plugin_info info = { sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "faulty", "0.1.0", "tests" };
    return &info;
}
static sco_result FaultyLoad(const sco_api* api, sco_plugin* self) { return api->subscribe(self, "tick", FaultyTick, nullptr); }
static void FaultyUnload() {}

static const P::Builtin kBuiltins[] = {
    { "core1", CoreQuery, CoreLoad, CoreUnload },
    { "faulty", FaultyQuery, FaultyLoad, FaultyUnload },
};
static const P::ScriptRuntime kLua{ sco_lua_load, sco_lua_unload };

static void SetCaps() {
    CHECK(!sco::app::Start(sco::app::Platform{})); // synchronous startup reentry
    g_events.push_back("caps");
    sco::caps::Set("teleport", true);
}

// ---- helpers --------------------------------------------------------------------------------

struct Reply { bool called = false; sco::Result r = sco::Result::Ok; std::string text; };
static void OnDone(sco::Result r, const char* reply, void* ctx) {
    auto* d = static_cast<Reply*>(ctx);
    d->called = true; d->r = r; d->text = reply ? reply : "";
}
static Reply Invoke(const char* name, std::vector<sco::Arg> args = {}) {
    Reply d;
    const sco::Result r = sco::Invoke(name, args.empty() ? nullptr : args.data(), static_cast<uint32_t>(args.size()), OnDone, &d);
    if (!d.called) d.r = r;
    return d;
}
static sco::Arg Str(const char* s) { sco::Arg a{}; a.type = sco::ArgType::String; a.v.s = s; return a; }
static sco::Arg Bool(bool b) { sco::Arg a{}; a.type = sco::ArgType::Bool; a.v.i = b; return a; }

static const P::Plugin* Find(const char* id) {
    for (const auto& p : sco::app::Plugins()) if (p.folder == id) return &p;
    return nullptr;
}
static State StateOf(const char* id) { const P::Plugin* p = Find(id); return p ? p->state : State::Off; }
static int FakeTicks(const char* id) {
    const P::Plugin* p = Find(id);
    if (!p || !p->module) return -1;
    const int* t = static_cast<const int*>(P::PlatformModuleOps().symbol(p->module, "fake_ticks"));
    return t ? *t : -1;
}
static int FakeUnloads(const char* id) {   // how often the plugin's sco_plugin_unload ran
    const P::Plugin* p = Find(id);
    if (!p || !p->module) return -1;
    const int* u = static_cast<const int*>(P::PlatformModuleOps().symbol(p->module, "fake_unload_calls"));
    return u ? *u : -1;
}
static std::vector<std::string> Unloaded() {   // "[plugin] unloaded <id>" lines, in order
    std::vector<std::string> ids;
    for (const auto& l : g_log) if (l.rfind("[plugin] unloaded ", 0) == 0) ids.push_back(l.substr(18));
    return ids;
}

// ---- built-ins through the loader directly --------------------------------------------------

// One configurable built-in for the refusal cases: g_mode picks what it does.
enum Mode { kOk, kWrongName, kApi2, kLoadFails, kLoadCrashes, kSmallInfo };
static Mode g_mode = kOk;
static const char* g_name = "";
static int g_tUnloads = 0;
static void Noop(const char*, const void*, void*) {}
static const sco_plugin_info* TQuery() {
    static sco_plugin_info info;
    info = { sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, g_name, "2.3.4", "tests" };
    if (g_mode == kWrongName) info.name = "other";
    if (g_mode == kApi2) info.api_major = 2;
    if (g_mode == kSmallInfo) info.size = 8;
    return &info;
}
static sco_result TLoad(const sco_api* api, sco_plugin* self) {
    const sco_result r = api->subscribe(self, "tick", Noop, nullptr);
    if (g_mode == kLoadCrashes) Fault();
    return g_mode == kLoadFails ? SCO_UNAVAILABLE : r;
}
static void TUnload() { ++g_tUnloads; }

static P::Plugin TryLoad(const char* id, Mode mode, const sco_api* api) {
    g_mode = mode;
    g_name = id;
    P::Plugin p = P::FromBuiltin({ id, TQuery, TLoad, TUnload });
    P::LoadBuiltin(p, api, sco::host::NewPlugin(id), P::Options{});
    return p;
}

static void TestBuiltinLoader(const sco_api* api, const fs::path& plugins) {
    CHECK(std::strcmp(P::KindName(P::Kind::Builtin), "builtin") == 0);

    // Entries.
    P::Plugin bad = P::FromBuiltin({ "Bad-Id", TQuery, TLoad, TUnload });
    CHECK(bad.state == State::Refused && bad.reason == "built-in id 'Bad-Id' is not valid");
    CHECK(P::FromBuiltin({ "game", TQuery, TLoad, TUnload }).state == State::Refused);   // reserved
    CHECK(P::FromBuiltin({ nullptr, TQuery, TLoad, TUnload }).state == State::Refused);
    P::Plugin none = P::FromBuiltin({ "nothing", TQuery, nullptr, TUnload });
    CHECK(none.state == State::Refused && none.reason == "built-in nothing has no sco_plugin_load");
    P::Plugin fresh = P::FromBuiltin({ "fresh", TQuery, TLoad, TUnload });
    CHECK(fresh.state == State::Ready && fresh.manifest.kind == P::Kind::Builtin && fresh.manifest.id == "fresh");
    CHECK(fresh.dir.empty() && !fresh.module && P::Describe(fresh) == "fresh ? builtin ready");
    CHECK(!P::LoadBuiltin(fresh, nullptr, sco::host::NewPlugin("fresh"), P::Options{}) &&
          fresh.reason == "host passed no api or owner");

    // The same checks as a native plugin, without a module.
    const size_t subs = sco::SubscriptionCount();
    P::Plugin p = TryLoad("wrongname", kWrongName, api);
    CHECK(p.state == State::Refused && p.reason == "built-in name 'other' does not match id 'wrongname'");
    p = TryLoad("newapi", kApi2, api);
    CHECK(p.state == State::Refused && p.reason == "built-in built for api 2." + std::to_string(SCO_API_MINOR));
    p = TryLoad("small", kSmallInfo, api);
    CHECK(p.state == State::Refused && p.reason == "sco_plugin_info.size too small");
    p = TryLoad("fails", kLoadFails, api);
    CHECK(p.state == State::Refused && p.reason == "sco_plugin_load returned UNAVAILABLE");
    CHECK(sco::SubscriptionCount() == subs);                                // released
    p = TryLoad("crashes", kLoadCrashes, api);
    CHECK(p.state == State::Crashed && p.reason == "crashed in sco_plugin_load (0xC0000005)");
    CHECK(sco::SubscriptionCount() == subs);
    CHECK(Logged(("[plugin] refused newapi: built-in built for api 2." + std::to_string(SCO_API_MINOR) + "").c_str()));

    // A clean load fills the manifest from the query; UnloadAll unloads built-ins after every
    // other plugin, even one loaded before them.
    std::vector<P::Plugin> list;
    P::Options on;
    on.enabled = true;
    for (auto& n : P::Discover(plugins, on)) if (n.folder == "m0") list.push_back(std::move(n));
    list.push_back(P::FromBuiltin({ "late", TQuery, TLoad, TUnload }));
    CHECK(list.size() == 2);
    if (list.size() != 2) return;
    P::ContainCallouts(&list);
    CHECK(P::LoadNative(list[0], api, sco::host::NewPlugin("m0"), on));
    g_mode = kOk;
    g_name = "late";
    CHECK(P::LoadBuiltin(list[1], api, sco::host::NewPlugin("late"), on));
    const P::Plugin& late = list[1];
    CHECK(late.state == State::Loaded && late.loadOrder > list[0].loadOrder && !late.module);
    CHECK(late.manifest.version == "2.3.4" && late.manifest.author == "tests" && late.manifest.apiMajor == 1);
    CHECK(P::Describe(late) == "late 2.3.4 builtin loaded");
    CHECK(Logged(("[plugin] loaded late 2.3.4 (api 1." + std::to_string(SCO_API_MINOR) + ") built in").c_str()));
    CHECK(!P::LoadBuiltin(list[1], api, sco::host::NewPlugin("late2"), on));   // not Ready any more
    g_log.clear();
    g_tUnloads = 0;
    P::UnloadAll(list);
    CHECK((Unloaded() == std::vector<std::string>{ "m0", "late" }));
    CHECK(late.state == State::Unloaded && g_tUnloads == 1 && sco::SubscriptionCount() == subs);
    P::ContainCallouts(nullptr);
}

// ---- the host kit ---------------------------------------------------------------------------

// The product's signature tables: Start registers them through Platform::registerSignatures, and
// only with an image. Nothing in sco-core registers Star Citizen's tables on its own.
static int g_registerCalls = 0;
#ifndef SCO_KERNEL_ONLY
#include "game_contracts/app_register_signatures.inc"
#else
static sco::SigRegistrationResult RegisterForTest(sco::SignatureRegistry&) { ++g_registerCalls; return {sco::SigRegistryStatus::Ok, 0, 0}; }   // kernel only: no game tables
#endif
static sco::SigRegistrationResult RefuseSignatureRegistration(sco::SignatureRegistry&) {
    return {sco::SigRegistryStatus::DuplicateId, 0, 0};
}

static void TestApp(const fs::path& sdk, const fs::path& out) {
    const fs::path root = out / "app" / "plugins";
    std::error_code ec;
    fs::remove_all(out / "app", ec);
    fs::create_directories(root);
    for (const char* id : { "m0", "m11" }) {
        if (!fs::is_directory(out / "plugins" / id)) {
            std::printf("FAIL: %s missing (tools/test.sh or the CMake build makes it)\n", (out / "plugins" / id).string().c_str());
            ++g_fail;
            return;
        }
        fs::copy(out / "plugins" / id, root / id, fs::copy_options::recursive);
    }
    fs::copy(sdk / "examples" / "greeter", root / "greeter", fs::copy_options::recursive);
    // A synthetic data pack tests indexing without a game example or game record names.
    fs::create_directories(root / "sample_pack" / "lists");
    std::ofstream(root / "sample_pack" / "plugin.ini")
        << "id = sample_pack\nname = Sample pack\nversion = 1.0\napi = 1.0\nkind = data\n";
    std::ofstream(root / "sample_pack" / "lists" / "items.txt") << "synthetic-item\n";

    sco::app::Platform pf;
    pf.registerSignatures = RegisterForTest;
    pf.hostVersion = "test-app 1.0";
    pf.pluginRoot = root;
    CHECK(pf.pluginsEnabled);   // a configured root uses the default-on consumer path
    pf.builtins = kBuiltins;
    pf.nBuiltins = 2;
    pf.scripts = &kLua;
    pf.setCapabilities = SetCaps;

    sco::app::Tick(1);                                                       // before Start: nothing
    CHECK(sco::app::Plugins().empty());
    g_log.clear();
    g_events.clear();
    CHECK(sco::app::Start(pf));
    // A host started without a registry pins that absence for its lifetime.
    auto unexpectedRegistry = std::make_shared<sco::SignatureRegistry>();
    CHECK(unexpectedRegistry->Freeze() == sco::SigRegistryStatus::Ok);
    CHECK(unexpectedRegistry->Resolve(sco::Image{}) == sco::SigRegistryStatus::Ok);
    CHECK(!sco::InstallHostSignatureRegistry(unexpectedRegistry, nullptr));
    CHECK(!sco::AcquireHostSignatureRegistry());

    // Built-ins first, then folders in name order; load order follows.
    const auto& list = sco::app::Plugins();
    std::vector<std::string> ids;
    for (const auto& p : list) ids.push_back(p.folder);
    CHECK((ids == std::vector<std::string>{ "core1", "faulty", "greeter", "m0", "m11", "sample_pack" }));
    for (const auto& p : list) {
        CHECK(p.state == State::Loaded);
        if (p.state != State::Loaded) std::printf("  %s\n", P::Describe(p).c_str());
    }
    CHECK(list.size() == 6);
    if (list.size() != 6) return;
    CHECK(list[0].loadOrder < list[1].loadOrder && list[1].loadOrder < list[2].loadOrder &&
          list[2].loadOrder < list[3].loadOrder && list[3].loadOrder < list[4].loadOrder && list[5].loadOrder == 0);
    CHECK(Logged(("[plugin] loaded core1 1.0.0 (api 1." + std::to_string(SCO_API_MINOR) + ") built in").c_str()));
    CHECK(Logged("[plugin] 6 found, 6 loaded (plugins = on)"));
    CHECK(Logged("[plugin] core1 1.0.0 builtin loaded"));
    CHECK(Logged("[m0] hello from m0"));
    CHECK(!Logged("[core] signatures"));                                     // no image: skipped
    CHECK(g_registerCalls == 0);                                             // nor registered

    // Capabilities before the plugins, game.ready after them.
    CHECK((g_events == std::vector<std::string>{ "caps", "core1:load", "core1:game.ready" }));
    CHECK(Logged("[greeter] teleport is available"));
    CHECK(LogIndex("[plugin] 6 found") < LogIndex("[greeter] teleport is available"));

    // The data pack is indexed.
    const auto content = sco::app::Content().FromPlugin("sample_pack");
    CHECK(content.size() == 1 && content[0]->name == "lists/items.txt");

    // A second Start is refused and changes nothing.
    CHECK(!sco::app::Start(pf));
    CHECK(Logged("[app] start refused: already started") && sco::app::Plugins().size() == 6);

    // Commands: the built-in's and the Lua plugin's.
    Reply r = Invoke("core1.count");
    CHECK(r.called && r.r == sco::Result::Ok && r.text == "ticks=0");
    r = Invoke("greeter.greet", { Str("Pilot"), Bool(true) });
    CHECK(r.r == sco::Result::Ok && r.text == "HI, PILOT!");

    // Tick reaches every subscriber in a healthy session. Fault containment and
    // terminal-retirement behavior run in a separate process below.
    sco::app::Tick(100);
    CHECK(g_coreTicks == 1 && FakeTicks("m0") == 1 && FakeTicks("m11") == 1);
    CHECK(StateOf("faulty") == State::Loaded);
    sco::app::Tick(200);
    CHECK(g_coreTicks == 2 && FakeTicks("m0") == 2 && FakeTicks("m11") == 2);
    CHECK(StateOf("core1") == State::Loaded && StateOf("m0") == State::Loaded);
    CHECK(Invoke("core1.count").text == "ticks=2");

    // Stop: game.exit first, then unload newest first with built-ins last.
    const sco_plugin* firstSelf = Find("core1")->self;
    g_log.clear();
    g_events.clear();
    sco::app::Stop();
    CHECK((g_events == std::vector<std::string>{ "core1:game.exit", "core1:unload" }));
    CHECK(Logged("[greeter] greeted 1 times"));
    CHECK(LogIndex("[greeter] greeted 1 times") < LogIndex("[plugin] unloaded"));
    CHECK((Unloaded() == std::vector<std::string>{ "m11", "m0", "greeter", "faulty", "core1" }));
    CHECK(StateOf("core1") == State::Unloaded && StateOf("m0") == State::Unloaded && StateOf("greeter") == State::Unloaded);
    CHECK(StateOf("faulty") == State::Unloaded && StateOf("sample_pack") == State::Loaded);
    CHECK(Invoke("core1.count").r == sco::Result::Unavailable);
    sco::app::Tick(300);                                                     // after Stop: nothing
    CHECK(g_coreTicks == 2);
    sco::app::Stop();                                                        // twice: no-op
    CHECK(g_events.size() == 2);

    // Start again, with a game image this time (a synthetic one: no row resolves). Fresh
    // handles; the signature report is logged and every plugin still loads.
    std::vector<uint8_t> mem(8192, 0xCC);
    sco::Image img{};
    img.base = mem.data();
    img.text = { mem.data(), 4096 };
    img.rdata = { mem.data() + 4096, 4096 };
    img.size = static_cast<uint32_t>(mem.size());
    pf.image = &img;
    pf.imageOwner = pf.resolverOwner = std::make_shared<int>(1);
    g_log.clear();
    CHECK(sco::app::Start(pf));
    CHECK(Logged("[core] signatures: 0/"));
    CHECK(g_registerCalls == 1 && !Logged("signature tables did not all register"));
    CHECK(LogIndex("[core] signatures") < LogIndex("[plugin] 6 found, 6 loaded (plugins = on)"));
    CHECK(sco::app::Plugins().size() == 6 && Find("core1")->self && Find("core1")->self != firstSelf);
    for (const auto& p : sco::app::Plugins()) CHECK(p.state == State::Loaded);
    CHECK(Invoke("core1.count").text == "ticks=0");
    sco::app::Tick(1000);
    CHECK(g_coreTicks == 1 && StateOf("faulty") == State::Loaded);
    CHECK(sco::caps::Has("teleport") && sco::AcquireHostSignatureSnapshot());
    sco::app::Stop();
    CHECK(StateOf("core1") == State::Unloaded);
    CHECK(!sco::caps::Has("teleport") && !sco::AcquireHostSignatureSnapshot());

    // Registration failure must not publish a partial registry or start plugins/caps;
    // a new start can retry using a fresh registry.
    pf.registerSignatures = RefuseSignatureRegistration;
    g_events.clear();
    CHECK(!sco::app::Start(pf));
    CHECK(!sco::AcquireHostSignatureSnapshot() && !sco::caps::Has("teleport"));
    CHECK(g_events.empty());
    pf.registerSignatures = RegisterForTest;
    CHECK(sco::app::Start(pf));
    CHECK(sco::AcquireHostSignatureSnapshot() && sco::caps::Has("teleport"));
    sco::app::Stop();
    CHECK(!sco::AcquireHostSignatureSnapshot() && !sco::caps::Has("teleport"));

    // Plugins off: built-ins still load, no folder is read.
    pf.pluginsEnabled = false;
    pf.image = nullptr;
    g_log.clear();
    CHECK(sco::app::Start(pf));
    CHECK(sco::app::Plugins().size() == 2 && StateOf("core1") == State::Loaded);
    CHECK(Logged("[plugin] 2 found, 2 loaded (plugins = off)"));
    sco::app::Stop();

    // An empty root must not discover the current directory, even with plugins enabled.
    const fs::path previousDirectory = fs::current_path();
    fs::current_path(root);
    pf.pluginsEnabled = true;
    pf.pluginRoot.clear();
    CHECK(sco::app::Start(pf));
    CHECK(sco::app::Plugins().size() == 2 && StateOf("core1") == State::Loaded);
    sco::app::Stop();
    fs::current_path(previousDirectory);
    pf.pluginRoot = root;

    // No script runtime: a lua plugin is refused, the rest load.
    pf.pluginsEnabled = true;
    pf.scripts = nullptr;
    CHECK(sco::app::Start(pf));
    CHECK(StateOf("greeter") == State::Refused && Find("greeter")->reason == "no script runtime");
    CHECK(StateOf("m0") == State::Loaded);
    sco::app::Stop();

    // A data folder gives plugins sco.storage, published before they load and withdrawn after
    // they unloaded; without one there is none.
    const sco_api* api = sco::host::BuildApi({ "test-app 1.0" });
    const void* table = nullptr;
    CHECK(api->query_service("sco.storage", 0x00010000, &table) == SCO_NOT_FOUND);
    pf.dataRoot = out / "app" / "data";
    CHECK(sco::app::Start(pf));
    CHECK(api->query_service("sco.storage", 0x00010000, &table) == SCO_OK && table);
    sco::app::Stop();
    CHECK(api->query_service("sco.storage", 0x00010000, &table) == SCO_NOT_FOUND);

    // sco.ui is always published, with the product's reserved chords, and withdrawn after unload.
    static const char* const kReserved[] = { "F6", "not a chord" };
    pf.reservedChords = kReserved;
    pf.nReservedChords = 2;
    CHECK(sco::app::Start(pf));
    CHECK(api->query_service("sco.ui", 0x00010000, &table) == SCO_OK && table == sco::ui::Table());
    CHECK((sco::ui::ReservedChords() == std::vector<std::string>{ "f6" }));
    CHECK(Logged("[app] hotkey 'not a chord' not reserved: BAD_ARG"));
    sco::app::Stop();
    CHECK(api->query_service("sco.ui", 0x00010000, &table) == SCO_NOT_FOUND && !sco::ui::Started());

    // sco.net is always published with its capability, so a plugin that requires it loads; nothing
    // listens until the product hosts or joins. Gone after Stop.
    pf.scripts = &kLua;
    fs::create_directories(root / "needsnet");
    {
        std::FILE* f = std::fopen((root / "needsnet" / "plugin.ini").string().c_str(), "wb");
        std::fputs("id = needsnet\nname = Needs sco.net\nversion = 1.0.0\napi = 1.0\nkind = lua\nentry = main.lua\n"
                   "requires = sco.net\n", f);
        std::fclose(f);
        f = std::fopen((root / "needsnet" / "main.lua").string().c_str(), "wb");
        std::fputs("-- requires sco.net\n", f);
        std::fclose(f);
    }
    CHECK(sco::app::Start(pf));
    CHECK(sco::caps::Has("sco.net") && sco::net::Started() && sco::net::BoundPort() == 0);
    CHECK(api->query_service(SCO_NET_NAME, SCO_NET_VERSION_1_0, &table) == SCO_OK && table == sco::net::Table());
    CHECK(StateOf("needsnet") == State::Loaded);
    sco::app::Stop();
    CHECK(!sco::caps::Has("sco.net") && !sco::net::Started());
    CHECK(api->query_service(SCO_NET_NAME, SCO_NET_VERSION_1_0, &table) == SCO_NOT_FOUND);

    // requires names services and plugins, not only capabilities: a plugin loads after the plugins
    // it requires, whatever the folder order (aaa_user sorts before m11, whose service it needs).
    // Host services (sco.storage, sco.ui) count as provided; a cycle, a missing provider and a
    // game service the host doesn't publish are refused with a reason, and nothing else is affected.
    auto makeLua = [&](const char* id, const char* needs, const char* script = "-- nothing to do\n") {
        fs::create_directories(root / id);
        std::FILE* f = std::fopen((root / id / "plugin.ini").string().c_str(), "wb");
        std::fprintf(f, "id = %s\nname = %s\nversion = 1.0.0\napi = 1.0\nkind = lua\nentry = main.lua\n", id, id);
        if (*needs) std::fprintf(f, "requires = %s\n", needs);
        std::fclose(f);
        f = std::fopen((root / id / "main.lua").string().c_str(), "wb");
        std::fputs(script, f);
        std::fclose(f);
    };
    makeLua("aaa_user", "m11.svc");
    makeLua("hosted", "sco.storage, sco.ui");
    makeLua("cyc_a", "cyc_b.svc");
    makeLua("cyc_b", "cyc_a.svc");
    makeLua("orphan", "nowhere.svc");
    makeLua("needsgame", "game.vehicles");
    makeLua("bad_lua", "", "this is not lua (\n");          // refused when its script runs
    makeLua("a_needs_bad", "bad_lua");                      // sorts first; loads after bad_lua, which fails
    g_log.clear();
    CHECK(sco::app::Start(pf));
    CHECK(StateOf("m11") == State::Loaded && StateOf("aaa_user") == State::Loaded);
    CHECK(Find("m11")->loadOrder < Find("aaa_user")->loadOrder);
    CHECK(StateOf("hosted") == State::Loaded);
    CHECK(StateOf("cyc_a") == State::Refused && Find("cyc_a")->reason == "requires cycle: cyc_a -> cyc_b -> cyc_a");
    CHECK(StateOf("cyc_b") == State::Refused && Find("cyc_b")->reason == "requires cycle: cyc_a -> cyc_b -> cyc_a");
    CHECK(StateOf("orphan") == State::Refused && Find("orphan")->reason == "requires service nowhere.svc: no plugin provides it");
    CHECK(Logged("[plugin] orphan 1.0.0 lua refused: requires service nowhere.svc: no plugin provides it"));
    CHECK(StateOf("bad_lua") == State::Refused);
    CHECK(StateOf("a_needs_bad") == State::Refused && !Find("a_needs_bad")->self);
    CHECK(Find("a_needs_bad")->reason.rfind("requires bad_lua: plugin 'bad_lua' is refused (", 0) == 0);
    CHECK(Logged("[plugin] refused a_needs_bad: requires bad_lua"));
    CHECK(StateOf("needsgame") == State::Refused);          // gameServices is off here: nothing publishes game.vehicles
    CHECK(Find("needsgame")->reason == "requires service game.vehicles: no plugin provides it");
    CHECK(StateOf("greeter") == State::Loaded && StateOf("needsnet") == State::Loaded);
    g_log.clear();
    sco::app::Stop();
    {
        const auto gone = Unloaded();   // newest first: the plugin that needed m11 unloads before it
        const auto at = [&](const char* id) { for (size_t i = 0; i < gone.size(); ++i) if (gone[i] == id) return static_cast<long>(i); return -1L; };
        CHECK(at("aaa_user") >= 0 && at("m11") >= 0 && at("aaa_user") < at("m11"));
    }

    // A provider that is there but doesn't load takes its dependents with it: m11 is switched off
    // by a disabled marker, so aaa_user is refused and never started.
    {
        std::FILE* f = std::fopen((root / "m11" / "disabled").string().c_str(), "wb");
        std::fclose(f);
    }
    CHECK(sco::app::Start(pf));
    CHECK(StateOf("m11") == State::Disabled && StateOf("aaa_user") == State::Refused);
    CHECK(Find("aaa_user")->reason == "requires m11: plugin 'm11' is disabled (disabled file)" && !Find("aaa_user")->self);
    CHECK(StateOf("hosted") == State::Loaded);
    sco::app::Stop();
    fs::remove(root / "m11" / "disabled", ec);
    fs::remove_all(root / "aaa_user", ec);
    fs::remove_all(root / "hosted", ec);
    fs::remove_all(root / "cyc_a", ec);
    fs::remove_all(root / "cyc_b", ec);
    fs::remove_all(root / "orphan", ec);
    fs::remove_all(root / "bad_lua", ec);
    fs::remove_all(root / "a_needs_bad", ec);

#ifdef SCO_GAME_SERVICES
    // Production app refuses the game-services request before native mutation.
    // The synthetic fixture separately tests app-level failure ordering only.
    pf.gameServices = true;
    pf.gameReadersQuiesced = [](void*) -> bool { std::abort(); };
    CHECK(!sco::app::Start(pf));
    CHECK(sco::game::services::Start() == sco::Result::Unavailable);
    CHECK(!sco::game::services::Started());
    pf.gameServices = false;
#endif
    fs::remove_all(out / "app", ec);
}

// A crashed plugin makes retirement terminal. Run in its own process so the
// deliberately retained session cannot contaminate healthy-session coverage.
static void TestFaultRetainsSession() {
    sco::app::Platform pf;
    pf.pluginsEnabled = false;
    pf.builtins = kBuiltins;
    pf.nBuiltins = 2;
    g_faultOnTick = true;
    g_events.clear();
    CHECK(sco::app::Start(pf));
    const auto* core = Find("core1");
    CHECK(core && core->self);
    const auto* coreSelf = core ? core->self : nullptr;
    sco::app::Tick(100);
    CHECK(StateOf("faulty") == State::Crashed);
    CHECK(Logged("[plugin] faulty crashed in tick (0xC0000005) and was disabled"));
    CHECK(StateOf("core1") == State::Loaded && g_coreTicks == 1);
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(StateOf("core1") == State::Loaded && Find("core1")->self == coreSelf);
    CHECK(sco::ui::Started()); // dependent host backing stays live
    CHECK(std::find(g_events.begin(), g_events.end(), "core1:unload") == g_events.end());
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(!sco::app::Start(pf));
}

// ---- hot reload -----------------------------------------------------------------------------

static std::vector<std::string> g_opened;   // file names the loader opened, in order
static int g_closed = 0;
static void* RlOpen(const fs::path& f, std::string& e) {
    g_opened.push_back(f.filename().string());
    return P::PlatformModuleOps().open(f, e);
}
static void* RlSymbol(void* m, const char* n) { return P::PlatformModuleOps().symbol(m, n); }
static bool g_probeWithdrawal = false;
static std::vector<bool> g_withdrawalHidden;
static void RlClose(void* m) {
    ++g_closed;
    if (g_probeWithdrawal) {
        const void* table = nullptr;
        const sco_api* api = sco::host::BuildApi({ "test-app 1.0" });
        const sco_result r = api->query_service("m11.svc", 1u << 16, &table);
        g_withdrawalHidden.push_back(r != SCO_OK && !table);
    }
    P::PlatformModuleOps().close(m);
}

static void WriteText(const fs::path& f, const std::string& text) {
    std::ofstream(f, std::ios::binary) << text;
}
static size_t CountCommands(const char* name) {
    std::vector<const sco::Command*> v(sco::kMaxCommands);
    const size_t n = sco::ListCommands(v.data(), v.size());
    size_t c = 0;
    for (size_t i = 0; i < n && i < v.size(); ++i) c += std::strcmp(v[i]->name, name) == 0;
    return c;
}
static size_t CountLog(const char* line) {
    size_t c = 0;
    for (const auto& l : g_log) c += l == line;
    return c;
}
static size_t ReloadFiles(const fs::path& dir) {   // .reload-* files in a plugin folder
    size_t c = 0;
    for (const auto& e : fs::directory_iterator(dir)) c += e.path().filename().string().rfind(".reload-", 0) == 0;
    return c;
}

// A folder whose id belongs to a built-in, through the real host kit (#11): refused before any
// load attempt, with the conflict named, whatever its kind; a non-conflicting id still loads and
// the built-in itself keeps working. "pack1" and "core1off" are two more built-ins (data-only
// ones, no code) so a data pack and a disabled folder can clash with them.
static const sco_plugin_info* NamedInfo(const char* id) {
    static sco_plugin_info info;
    info = { sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, id, "1.0.0", "tests" };
    return &info;
}
static const sco_plugin_info* PackQuery() { return NamedInfo("pack1"); }
static const sco_plugin_info* OffQuery() { return NamedInfo("core1off"); }
static sco_result PackLoad(const sco_api*, sco_plugin*) { return SCO_OK; }
static void PackUnload() {}

// The discovered folder with this id (a built-in has the same folder name, and comes first).
static const P::Plugin* Folder(const char* id) {
    for (const auto& p : sco::app::Plugins())
        if (p.folder == id && p.manifest.kind != P::Kind::Builtin) return &p;
    return nullptr;
}
static State FolderState(const char* id) { const P::Plugin* p = Folder(id); return p ? p->state : State::Off; }

static void TestBuiltinIdConflict(const fs::path& out) {
    const fs::path root = out / "conflict" / "plugins";
    std::error_code ec;
    fs::remove_all(out / "conflict", ec);
    fs::create_directories(root);
    for (const char* id : { "m0", "m11" }) {   // the non-conflicting controls
        if (!fs::is_directory(out / "plugins" / id)) {
            std::printf("FAIL: %s missing (tools/test.sh or the CMake build makes it)\n", (out / "plugins" / id).string().c_str());
            ++g_fail;
            return;
        }
        fs::copy(out / "plugins" / id, root / id, fs::copy_options::recursive);
    }
    // A native folder (its entry is missing too), a Lua folder, a data pack and a disabled folder,
    // each with a built-in's id.
    fs::create_directories(root / "core1");
    WriteText(root / "core1" / "plugin.ini", "id = core1\nname = Clash\nversion = 1.0.0\napi = 1.0\nkind = native\nentry = gone.dll\n");
    fs::create_directories(root / "faulty");
    WriteText(root / "faulty" / "plugin.ini", "id = faulty\nname = Clash\nversion = 1.0.0\napi = 1.0\nkind = lua\nentry = main.lua\n");
    WriteText(root / "faulty" / "main.lua", "sco.log('info', 'should never run')\n");
    fs::create_directories(root / "pack1" / "lists");
    WriteText(root / "pack1" / "plugin.ini", "id = pack1\nname = Clash\nversion = 1.0.0\napi = 1.0\nkind = data\n");
    WriteText(root / "pack1" / "lists" / "x.txt", "x\n");
    fs::create_directories(root / "core1off");
    WriteText(root / "core1off" / "plugin.ini", "id = core1off\nname = Clash\nversion = 1.0.0\napi = 1.0\nkind = data\n");
    WriteText(root / "core1off" / "disabled", "");

    const P::Builtin builtins[] = {
        { "core1", CoreQuery, CoreLoad, CoreUnload },
        { "faulty", FaultyQuery, FaultyLoad, FaultyUnload },
        { "pack1", PackQuery, PackLoad, PackUnload },
        { "core1off", OffQuery, PackLoad, PackUnload },
    };
    sco::app::Platform pf;
    pf.hostVersion = "test-app 1.0";
    pf.pluginRoot = root;
    pf.pluginsEnabled = true;
    pf.builtins = builtins;
    pf.nBuiltins = 4;
    pf.scripts = &kLua;

    g_log.clear();
    g_events.clear();
    CHECK(sco::app::Start(pf));

    const auto& list = sco::app::Plugins();
    CHECK(list.size() == 10);   // 4 built-ins + m0, m11 and the four clashing folders
    for (const auto& p : list) {
        if (p.manifest.kind == P::Kind::Builtin) continue;
        if (p.folder == "m0" || p.folder == "m11") { CHECK(p.state == State::Loaded); continue; }
        CHECK(p.state == (p.folder == "core1off" ? State::Disabled : State::Refused));
    }
    CHECK(FolderState("core1") == State::Refused &&
          Folder("core1")->reason == "id 'core1' belongs to a built-in plugin");
    CHECK(FolderState("faulty") == State::Refused &&
          Folder("faulty")->reason == "id 'faulty' belongs to a built-in plugin");
    CHECK(FolderState("pack1") == State::Refused &&
          Folder("pack1")->reason == "id 'pack1' belongs to a built-in plugin");
    CHECK(FolderState("core1off") == State::Disabled &&
          Folder("core1off")->reason == "disabled file; id 'core1off' belongs to a built-in plugin");
    CHECK(StateOf("m0") == State::Loaded);                                   // no conflict: loads
    CHECK(CountCommands("core1.count") == 1);                                // the built-in registered
    CHECK(Invoke("core1.count").text == "ticks=0");                          // and works
    CHECK(!Logged("should never run"));                                      // the Lua folder never ran
    CHECK(Logged("[plugin] core1 1.0.0 native refused: id 'core1' belongs to a built-in plugin"));
    CHECK(Logged("[plugin] faulty 1.0.0 lua refused: id 'faulty' belongs to a built-in plugin"));
    CHECK(Logged("[plugin] pack1 1.0.0 data refused: id 'pack1' belongs to a built-in plugin"));
    CHECK(Logged("[plugin] 10 found, 6 loaded (plugins = on)"));   // 4 built-ins + m0, m11
    sco::app::Stop();
    CHECK((g_events == std::vector<std::string>{ "core1:load", "core1:game.ready", "core1:game.exit", "core1:unload" }));
}

static const char* const kRlScript =
    "sco.log('info', 'rl run')\n"
    "sco.register_command{ name = 'rl.ping', title = 'Ping', fn = function() return 'pong' end }\n"
    "sco.register_command{ name = 'rl.self', title = 'Self', fn = function()\n"
    "  local ok, name, reply = sco.invoke('sco.reload', 'rl')\n"   // a reload from inside the plugin's own command
    "  return tostring(ok) .. ' ' .. tostring(name) .. ' ' .. tostring(reply)\n"
    "end }\n";

// Hot reload (sco::app::Reload, the host command sco.reload) through the real loader, host table
// and sco-lua: Lua re-run, natives from a copy, a handed-out provider refused, built-ins refused,
// busy refused. No timing anywhere: every check is on the result of a call.
static void TestReload(const fs::path& out, bool faultCase = false) {
    const fs::path base = out / (faultCase ? "reload_fault" : "reload");
    const fs::path root = base / "plugins";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(root);
    for (const char* id : { "m0", "m11", "svc_provider", "svc_caller" }) {
        if (!fs::is_directory(out / "plugins" / id)) {
            std::printf("FAIL: %s missing (tools/test.sh or the CMake build makes it)\n", (out / "plugins" / id).string().c_str());
            ++g_fail;
            return;
        }
        fs::copy(out / "plugins" / id, root / id, fs::copy_options::recursive);
    }
    fs::create_directories(root / "rl");
    WriteText(root / "rl" / "plugin.ini", "id = rl\nname = rl\nversion = 1.0.0\napi = 1.0\nkind = lua\nentry = main.lua\n");
    WriteText(root / "rl" / "main.lua", kRlScript);
    WriteText(root / "m0" / ".reload-99-stale", "left by a run that died");   // cleaned at Start

    sco::app::Platform pf;
    pf.hostVersion = "test-app 1.0";
    pf.pluginRoot = root;
    pf.pluginsEnabled = true;
    pf.builtins = kBuiltins;
    pf.nBuiltins = 2;
    pf.scripts = &kLua;
    pf.moduleOps = P::ModuleOps{ RlOpen, RlSymbol, RlClose };
    const sco_api* api = sco::host::BuildApi({ "test-app 1.0" });

    std::string why;
    CHECK(sco::app::Reload("rl", &why) == sco::Result::Unavailable && why == "the host kit is not started");
    g_log.clear();
    g_opened.clear();
    CHECK(sco::app::Start(pf));
    CHECK(!fs::exists(root / "m0" / ".reload-99-stale"));                    // stale copy removed at Start
    CHECK(StateOf("rl") == State::Loaded && StateOf("m0") == State::Loaded && StateOf("svc_provider") == State::Loaded);
    CHECK(std::find(g_opened.begin(), g_opened.end(), std::string(Find("m0")->manifest.entry)) != g_opened.end());
    CHECK(Find("m0")->reloadCopy.empty() && CountCommands("sco.reload") == 1);   // no copy yet; the host command exists

    // Lua: the script runs again under a new handle; its commands are registered again, not doubled.
    const size_t liveBefore = sco::ListCommands(nullptr, 0);
    const sco_plugin* s0 = Find("rl")->self;
    g_log.clear();
    Reply r = Invoke("sco.reload", { Str("rl") });
    CHECK(r.called && r.r == sco::Result::Ok && r.text == "reloaded rl");
    const sco_plugin* s1 = Find("rl")->self;
    CHECK(StateOf("rl") == State::Loaded && s1 && s1 != s0);
    CHECK(CountCommands("rl.ping") == 1 && CountCommands("rl.self") == 1 && sco::ListCommands(nullptr, 0) == liveBefore);
    CHECK(Invoke("rl.ping").text == "pong" && CountLog("[rl] rl run") == 1 && Logged("[app] reloaded rl"));
    CHECK(sco::Release(s0) == sco::Result::BadArg);                          // the old handle was released
    // A second reload, straight after the first (same tick): a third handle.
    CHECK(sco::app::Reload("rl") == sco::Result::Ok);
    const sco_plugin* s2 = Find("rl")->self;
    CHECK(s2 && s2 != s1 && s2 != s0 && CountCommands("rl.ping") == 1 && CountLog("[rl] rl run") == 2);
    CHECK(Find("rl")->loadOrder > Find("m0")->loadOrder);

    // Busy: the plugin's own command asks for its reload, with its code on the stack. Refused;
    // nothing changed.
    r = Invoke("rl.self");
    CHECK(r.r == sco::Result::Ok && r.text == "false failed busy: a callout of rl is running");
    CHECK(Find("rl")->self == s2 && StateOf("rl") == State::Loaded && CountLog("[rl] rl run") == 2);

    // Refusals: built-ins, unknown ids, a plugin that is not Loaded.
    const sco_plugin* core = Find("core1")->self;
    CHECK(sco::app::Reload("core1", &why) == sco::Result::BadArg && why == "core1 is built in and cannot be reloaded");
    CHECK(Find("core1")->self == core && StateOf("core1") == State::Loaded && Invoke("core1.count").r == sco::Result::Ok);
    CHECK(sco::app::Reload("nope", &why) == sco::Result::NotFound && why == "no plugin 'nope'");
    r = Invoke("sco.reload", { Str("nope") });
    CHECK(r.r == sco::Result::NotFound && r.text == "no plugin 'nope'");
    CHECK(Invoke("sco.reload").r == sco::Result::BadArg);                    // the argument is required
    CHECK(sco::app::Reload("faulty", &why) == sco::Result::BadArg);

    // Native: loaded from a copy next to the DLL; the module is fresh (its counters start again).
    sco::app::Tick(1);
    sco::app::Tick(2);
    CHECK(FakeTicks("m0") == 2);
    g_opened.clear();
    const int closedBefore = g_closed;
    const sco_plugin* m0 = Find("m0")->self;
    CHECK(sco::app::Reload("m0") == sco::Result::Ok);
    CHECK(g_opened.size() == 1 && g_opened[0].rfind(".reload-", 0) == 0 && g_closed == closedBefore + 1);
    const fs::path copy1 = Find("m0")->reloadCopy;
    CHECK(Find("m0")->self != m0 && StateOf("m0") == State::Loaded && !copy1.empty() && fs::exists(copy1));
    CHECK(fs::exists(root / "m0" / Find("m0")->manifest.entry));             // the original is left alone
    CHECK(FakeTicks("m0") == 0);
    sco::app::Tick(3);
    CHECK(FakeTicks("m0") == 1);
    CHECK(Find("m0")->pinned.empty());                                       // closed, not kept
    // The next reload closes that copy and deletes it; a third copy is mapped.
    CHECK(sco::app::Reload("m0") == sco::Result::Ok);
    const fs::path copy2 = Find("m0")->reloadCopy;
    CHECK(copy2 != copy1 && fs::exists(copy2) && !fs::exists(copy1) && ReloadFiles(root / "m0") == 1);

    // A native that registers commands: re-registered, not doubled.
    const size_t live = sco::ListCommands(nullptr, 0);
    const sco_plugin* c0 = Find("svc_caller")->self;
    CHECK(CountCommands("svc_caller.self") == 1 && sco::app::Reload("svc_caller") == sco::Result::Ok);
    CHECK(Find("svc_caller")->self != c0 && CountCommands("svc_caller.self") == 1 && sco::ListCommands(nullptr, 0) == live);

    // A provider whose service table was handed out is session-lifetime: the reload is refused
    // before anything is torn down — unload() never runs, Release never runs, no copy is made,
    // its services keep resolving (the same table) and the consumer keeps working. Only a
    // restart of the game reloads it.
    struct Svc { uint32_t size; int (*ok)(); int (*boom)(); };
    const void* t0 = nullptr;
    CHECK(api->query_service("svc_provider.svc", 1u << 16, &t0) == SCO_OK && t0);
    const int closed = g_closed;
    const int unloads = FakeUnloads("svc_provider");
    g_opened.clear();
    CHECK(sco::app::Reload("svc_provider", &why) == sco::Result::Failed &&
          why == "provides a service in use; restart the game to reload it");
    CHECK(Find("svc_provider")->state == State::Loaded && Find("svc_provider")->pinned.empty());
    CHECK(unloads == 0 && FakeUnloads("svc_provider") == unloads);            // unload() did not run
    CHECK(g_closed == closed && g_opened.empty());                            // no module opened or closed
    CHECK(ReloadFiles(root / "svc_provider") == 0);                           // no copy was made
    const void* t1 = nullptr;
    CHECK(api->query_service("svc_provider.svc", 1u << 16, &t1) == SCO_OK && t1 == t0);   // still resolves
    if (t0) CHECK(static_cast<const Svc*>(t0)->ok() == 7);                    // the held table still runs
    r = Invoke("sco.reload", { Str("svc_provider") });                        // the command surfaces the why
    CHECK(r.r == sco::Result::Failed && r.text == "provides a service in use; restart the game to reload it");
    CHECK(Invoke("svc_caller.ok").r == sco::Result::Ok);                      // the consumer still works
    // The C API and UI do not supply alternative administrative teardown paths.
    sco_arg reloadArg{};
    reloadArg.type = SCO_ARG_STRING;
    reloadArg.v.s = "svc_provider";
    sco_plugin* caller = Find("svc_caller")->self;
    CHECK(api->invoke(caller, "sco.reload", &reloadArg, 1, nullptr, nullptr) == SCO_FAILED);
    CHECK(sco::ui::Table()->bind_hotkey(caller, "ctrl+alt+r", "sco.reload", &reloadArg, 1) == SCO_OK);
    std::string uiReply;
    CHECK(sco::ui::Dispatch("ctrl+alt+r", &uiReply) == sco::Result::Failed &&
          uiReply == "provides a service in use; restart the game to reload it");
    CHECK(sco::ui::Table()->unbind_hotkey(caller, "ctrl+alt+r") == SCO_OK);
    CHECK(FakeUnloads("svc_provider") == unloads && g_closed == closed &&
          StateOf("svc_provider") == State::Loaded && ReloadFiles(root / "svc_provider") == 0);

    // A copy that can't be made changes nothing: the plugin keeps running.
    const fs::path entry = root / "m11" / Find("m11")->manifest.entry;
    const sco_plugin* m11 = Find("m11")->self;
    fs::rename(entry, entry.string() + ".bak");
    CHECK(sco::app::Reload("m11", &why) == sco::Result::Failed && why.rfind("cannot copy ", 0) == 0);
    CHECK(Find("m11")->self == m11 && StateOf("m11") == State::Loaded);
    fs::rename(entry.string() + ".bak", entry);

    // A script that no longer loads: the plugin ends Refused, its commands are gone, a retry says so.
    WriteText(root / "rl" / "main.lua", "this is not lua (\n");
    CHECK(sco::app::Reload("rl", &why) == sco::Result::Failed && why.rfind("main.lua:", 0) == 0);
    CHECK(StateOf("rl") == State::Refused && CountCommands("rl.ping") == 0 && Logged("[app] reload rl failed: main.lua:"));
    CHECK(sco::app::Reload("rl", &why) == sco::Result::Failed && why == "rl is not loaded (refused)");

    // Stop still tears the refused provider down: host shutdown (UnloadAll) does not ask
    // CanUnload. Its module stays mapped (the table was handed out), as for any unload.
    sco::app::Stop();
    CHECK(StateOf("svc_provider") == State::Unloaded && FakeUnloads("svc_provider") == 1);
    CHECK(Logged("[plugin] unloaded svc_provider") && Logged("kept mapped: its service table was handed out"));
    CHECK(ReloadFiles(root / "m0") == 0 && Find("m0")->reloadCopy.empty());
    CHECK(sco::app::Reload("m0") == sco::Result::Unavailable);
    CHECK(Invoke("sco.reload", { Str("m0") }).r == sco::Result::Unavailable);

    // Platform::loadFromCopies: every native is mapped from a copy from the start.
    WriteText(root / "rl" / "main.lua", kRlScript);
    pf.loadFromCopies = true;
    g_opened.clear();
    CHECK(sco::app::Start(pf));
    CHECK(StateOf("m0") == State::Loaded && StateOf("m11") == State::Loaded && StateOf("rl") == State::Loaded);
    size_t fromCopy = 0;
    for (const auto& name : g_opened) fromCopy += name.rfind(".reload-", 0) == 0;
    CHECK(fromCopy == 4 && g_opened.size() == 4);                            // m0, m11, svc_caller, svc_provider
    CHECK(!Find("m0")->reloadCopy.empty() && sco::app::Reload("m0") == sco::Result::Ok);

    if (!faultCase) {
        CHECK(sco::app::Stop() == sco::Result::Ok);
        CHECK(ReloadFiles(root / "m0") == 0 && ReloadFiles(root / "m11") == 0);
        fs::remove_all(base, ec);
        return;
    }

    // An attributed provider fault is terminal for this instance, not a recoverable reload.
    const P::Plugin* provider = Find("svc_provider");
    const void* heldFaultTable = nullptr;
    CHECK(api->query_service("svc_provider.svc", 1u << 16, &heldFaultTable) == SCO_OK && heldFaultTable);
    void* const faultModule = provider->module;
    const int faultTicks = FakeTicks("svc_provider");
    const int faultCloses = g_closed;
    P::MarkCrashed(*const_cast<P::Plugin*>(provider), "attributed service fault", 0xC0000005u);
    CHECK(StateOf("svc_provider") == State::Faulted && provider->module == faultModule);
    CHECK(api->query_service("svc_provider.svc", 1u << 16, &t1) == SCO_NOT_FOUND && !t1);
    sco::app::Tick(9);
    CHECK(FakeTicks("svc_provider") == faultTicks && FakeUnloads("svc_provider") == 0 && g_closed == faultCloses);
    P::LogReport(sco::app::Plugins(), true);
    CHECK(Logged("svc_provider 1.0.0 native faulted:") && Logged("restart the game"));
    r = Invoke("sco.reload", { Str("svc_provider") });
    CHECK(r.r == sco::Result::Failed && r.text.find("faulted") != std::string::npos &&
          r.text.find("restart the game") != std::string::npos);
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(StateOf("svc_provider") == State::Faulted && provider->module == faultModule && FakeUnloads("svc_provider") == 0);
    CHECK(StateOf("m0") == State::Loaded && StateOf("m11") == State::Loaded);
    CHECK(ReloadFiles(root / "m0") > 0 && ReloadFiles(root / "m11") > 0);
    CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(pf));
    // Keep mapped module files and the other backing until process exit.
}

// A native provider, a diamond of Lua dependents and an unrelated script. Dependencies name
// both an id and a service; leaf sorts first so folder order cannot stand in for dependency order.
static void TestReloadCascade(const fs::path& out, bool faultCase = false) {
    const fs::path base = out / (faultCase ? "reload_cascade_fault" : "reload_cascade");
    const fs::path root = base / "plugins";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(root);
    fs::copy(out / "plugins" / "m11", root / "m11", fs::copy_options::recursive);
    fs::copy(out / "plugins" / "svc_provider", root / "svc_provider", fs::copy_options::recursive);
    {
        std::ofstream ini(root / "svc_provider" / "plugin.ini", std::ios::app);
        ini << "\nrequires = b_user\n";
    }
    auto makeLua = [&](const char* id, const char* needs) {
        fs::create_directories(root / id);
        WriteText(root / id / "plugin.ini", std::string("id = ") + id + "\nname = " + id +
                  "\nversion = 1.0.0\napi = 1.0\nkind = lua\nentry = main.lua\n" +
                  (*needs ? std::string("requires = ") + needs + "\n" : ""));
        WriteText(root / id / "main.lua", "-- no service query: the provider remains reloadable\n");
    };
    makeLua("a_leaf", "b_user, z_user.svc, svc_provider.svc");
    makeLua("b_user", "m11.svc");
    makeLua("z_user", "m11");
    makeLua("unrelated", "");
    makeLua("cyc_rel_a", "cyc_rel_b");
    makeLua("cyc_rel_b", "cyc_rel_a");
    makeLua("off_user", "m11");
    WriteText(root / "off_user" / "disabled", "");
    const std::string busyScript =
        "sco.register_command{ name = 'b_user.reload_provider', title = 'Reload', fn = function()\n"
        "  local ok, name, reply = sco.invoke('sco.reload', 'm11')\n"
        "  return tostring(ok) .. ' ' .. tostring(name) .. ' ' .. tostring(reply)\nend }\n";
    WriteText(root / "b_user" / "main.lua", busyScript);
    sco::app::Platform pf;
    pf.pluginRoot = root;
    pf.pluginsEnabled = true;
    pf.scripts = &kLua;
    pf.moduleOps = P::ModuleOps{ RlOpen, RlSymbol, RlClose };
    CHECK(sco::app::Start(pf));
    static int providerTable = 42;
    auto publishProvider = [&] {
        CHECK(sco::ProvideService(Find("m11")->self, "m11", "m11.svc", 1u << 16, &providerTable) == sco::Result::Ok);
    };
    publishProvider();
    const sco_plugin* provider = Find("m11")->self;
    const sco_plugin* user = Find("b_user")->self;
    const sco_plugin* leaf = Find("a_leaf")->self;
    const sco_plugin* other = Find("z_user")->self;
    const sco_plugin* unrelated = Find("unrelated")->self;
    g_log.clear();
    std::string why;
    g_withdrawalHidden.clear();
    g_probeWithdrawal = true;
    CHECK(sco::app::Reload("m11", &why) == sco::Result::Ok && why.empty());
    g_probeWithdrawal = false;
    CHECK((g_withdrawalHidden == std::vector<bool>{ true, true }));
    CHECK((Unloaded() == std::vector<std::string>{ "a_leaf", "z_user", "svc_provider", "b_user", "m11" }));
    CHECK(Find("m11")->self != provider && Find("b_user")->self != user &&
          Find("a_leaf")->self != leaf && Find("z_user")->self != other);
    CHECK(Find("unrelated")->self == unrelated && StateOf("unrelated") == State::Loaded);
    CHECK(Find("m11")->loadOrder < Find("b_user")->loadOrder &&
          Find("b_user")->loadOrder < Find("svc_provider")->loadOrder &&
          Find("svc_provider")->loadOrder < Find("z_user")->loadOrder &&
          Find("z_user")->loadOrder < Find("a_leaf")->loadOrder);

    // Reloading a member of a mutual cycle fails with "requires cycle in reload group"
    auto* cycA = const_cast<P::Plugin*>(Find("cyc_rel_a"));
    auto* cycB = const_cast<P::Plugin*>(Find("cyc_rel_b"));
    cycA->state = State::Loaded;
    cycB->state = State::Loaded;
    cycA->after.push_back("cyc_rel_b");
    cycB->after.push_back("cyc_rel_a");
    CHECK(sco::app::Reload("cyc_rel_a", &why) == sco::Result::Failed &&
          why == "requires cycle in reload group");
    cycA->after.clear(); cycB->after.clear();
    cycA->state = State::Off; cycB->state = State::Off;
    CHECK(StateOf("off_user") == State::Disabled && !Find("off_user")->self);

    // A dependent's callout is on the stack: refuse the entire group before even making copies.
    provider = Find("m11")->self;
    user = Find("b_user")->self;
    const fs::path providerCopy = Find("m11")->reloadCopy;
    g_log.clear();
    Reply busy = Invoke("b_user.reload_provider");
    CHECK(busy.r == sco::Result::Ok && busy.text == "false failed busy: a callout of b_user is running");
    CHECK(Unloaded().empty() && Find("m11")->self == provider && Find("b_user")->self == user);
    CHECK(Find("m11")->reloadCopy == providerCopy && ReloadFiles(root / "m11") == 1);

    // The second native copy fails after the first is prepared. Nothing unloads; temporary
    // copies and reservations are rolled back, so a subsequent command reload can succeed.
    const fs::path entry = root / "svc_provider" / Find("svc_provider")->manifest.entry;
    fs::rename(entry, entry.string() + ".bak");
    CHECK(sco::app::Reload("m11", &why) == sco::Result::Failed && why.rfind("cannot copy ", 0) == 0);
    CHECK(Unloaded().empty() && Find("m11")->self == provider && Find("b_user")->self == user);
    CHECK(ReloadFiles(root / "m11") == 1 && ReloadFiles(root / "svc_provider") == 1);
    fs::rename(entry.string() + ".bak", entry);
    Reply command = Invoke("sco.reload", { Str("m11") });
    CHECK(command.r == sco::Result::Ok && command.text == "reloaded m11");
    CHECK(Find("m11")->self != provider && Find("b_user")->self != user);
    CHECK((Unloaded() == std::vector<std::string>{ "a_leaf", "z_user", "svc_provider", "b_user", "m11" }));

    // A load failure refuses its transitive dependents without running them, but the other
    // branch and the unrelated plugin remain loaded. Old dependent copies are not left behind.
    WriteText(root / "b_user" / "main.lua", "this is not lua (\n");
    g_log.clear();
    CHECK(sco::app::Reload("m11", &why) == sco::Result::Failed && why.rfind("b_user: main.lua:", 0) == 0);
    CHECK(StateOf("m11") == State::Loaded && StateOf("z_user") == State::Loaded);
    CHECK(StateOf("b_user") == State::Refused && StateOf("a_leaf") == State::Refused &&
          StateOf("svc_provider") == State::Refused);
    CHECK(Find("a_leaf")->reason.rfind("requires b_user:", 0) == 0 &&
          Find("svc_provider")->reason.rfind("requires b_user:", 0) == 0);
    CHECK(ReloadFiles(root / "svc_provider") == 0);
    CHECK(Find("unrelated")->self == unrelated && StateOf("unrelated") == State::Loaded);
    sco::app::Stop();

    if (faultCase) {
        // Unload faults are terminal for the owning session. Keep this case in
        // its own process and retain every surviving provider and its backing.
        WriteText(root / "b_user" / "main.lua", busyScript);
        CHECK(sco::app::Start(pf));
        publishProvider();
        provider = Find("m11")->self;
        user = Find("b_user")->self;
        const_cast<P::Plugin*>(Find("svc_provider"))->exports.unload = Fault;
        g_log.clear();
        CHECK(sco::app::Reload("m11", &why) == sco::Result::Failed && why.rfind("unload failed: svc_provider:", 0) == 0);
        CHECK(Find("m11")->self == provider && Find("b_user")->self == user &&
              StateOf("m11") == State::Loaded && StateOf("b_user") == State::Loaded);
        CHECK(StateOf("svc_provider") == State::Crashed && StateOf("a_leaf") == State::Refused &&
              StateOf("z_user") == State::Loaded);
        CHECK(Find("a_leaf")->reason.rfind("requires svc_provider:", 0) == 0);
        CHECK(ReloadFiles(root / "m11") == 0 && ReloadFiles(root / "svc_provider") == 0);
        const void* available = nullptr;
        CHECK(sco::host::BuildApi({ "test-app 1.0" })->query_service("m11.svc", 2u << 16, &available) == SCO_UNAVAILABLE && !available);
        CHECK(sco::app::Stop() == sco::Result::Failed);
        CHECK(StateOf("m11") == State::Loaded && Find("m11")->self == provider);
        CHECK(StateOf("b_user") == State::Loaded && Find("b_user")->self == user);
        CHECK(sco::ui::Started());
        CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(pf));
        return;
    }

    WriteText(root / "b_user" / "main.lua", busyScript);

    // A table handed out by a dependent, not the requested provider, still blocks the whole
    // group. Refusal preserves every owner, service identity, module and original copy count.
    CHECK(sco::app::Start(pf));
    publishProvider();
    provider = Find("m11")->self;
    user = Find("b_user")->self;
    const sco_plugin* service = Find("svc_provider")->self;
    const sco_api* api = sco::host::BuildApi({ "test-app 1.0" });
    const void* table = nullptr;
    CHECK(api->query_service("svc_provider.svc", 1u << 16, &table) == SCO_OK && table);
    const int closed = g_closed;
    g_log.clear();
    g_opened.clear();
    CHECK(sco::app::Reload("m11", &why) == sco::Result::Failed &&
          why == "provides a service in use; restart the game to reload it");
    CHECK(Unloaded().empty() && g_opened.empty() && g_closed == closed);
    CHECK(Find("m11")->self == provider && Find("b_user")->self == user && Find("svc_provider")->self == service);
    CHECK(ReloadFiles(root / "m11") == 0 && ReloadFiles(root / "svc_provider") == 0);
    CHECK(FakeUnloads("m11") == 0 && FakeUnloads("svc_provider") == 0);
    const void* same = nullptr;
    CHECK(api->query_service("svc_provider.svc", 1u << 16, &same) == SCO_OK && same == table);
    // Failure must not leave the earlier provider in withdrawal: its service still resolves.
    CHECK(api->query_service("m11.svc", 1u << 16, &same) == SCO_OK && same);
    sco::app::Stop();
    fs::remove_all(base, ec);
}

static void TestReloadNonCodeDependents(const fs::path& out) {
    const fs::path base = out / "reload_noncode";
    const fs::path root = base / "plugins";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(root / "pack");
    fs::copy(out / "plugins" / "m0", root / "m0", fs::copy_options::recursive);
    WriteText(root / "pack" / "plugin.ini",
              "id = pack\nname = Pack\nversion = 1.0.0\napi = 1.0\nkind = data\nrequires = m0\n");
    sco::app::Platform pf;
    pf.pluginRoot = root;
    pf.pluginsEnabled = true;
    pf.moduleOps = P::ModuleOps{ RlOpen, RlSymbol, RlClose };
    CHECK(sco::app::Start(pf));
    // G14 snapshots packs before any code loads: a code dependency refuses the
    // pack at startup, and that refused pack must not block its provider's reload.
    CHECK(StateOf("pack") == State::Refused &&
          Find("pack")->reason == "startup data pack requires code provider: m0");
    g_log.clear();
    std::string why;
    CHECK(sco::app::Reload("m0", &why) == sco::Result::Ok && why.empty());
    CHECK(Unloaded() == std::vector<std::string>{ "m0" } &&
          StateOf("m0") == State::Loaded && StateOf("pack") == State::Refused);
    sco::app::Stop();

    // A host-supplied resolved edge on an already loaded pack still cannot join
    // a code reload cascade. Exercise the guard without violating startup policy.
    WriteText(root / "pack" / "plugin.ini",
              "id = pack\nname = Pack\nversion = 1.0.0\napi = 1.0\nkind = data\n");
    CHECK(sco::app::Start(pf));
    auto* pack = const_cast<P::Plugin*>(Find("pack"));
    CHECK(pack->state == State::Loaded);
    pack->after.push_back("m0");
    const sco_plugin* before = Find("m0")->self;
    g_log.clear();
    CHECK(sco::app::Reload("m0", &why) == sco::Result::Failed && why == "pack cannot join a reload cascade");
    CHECK(Unloaded().empty() && Find("m0")->self == before && ReloadFiles(root / "m0") == 0);
    sco::app::Stop();
    fs::remove_all(root / "pack", ec);

    pf.builtins = kBuiltins;
    pf.nBuiltins = 1;
    CHECK(sco::app::Start(pf));
    // Built-ins have no plugin.ini. A host can supply resolved dependencies in its own metadata;
    // exercise that boundary without changing the public plugin ABI or adding a builtin manifest.
    auto* builtin = const_cast<P::Plugin*>(Find("core1"));
    builtin->after.push_back("m0");
    before = Find("m0")->self;
    const sco_plugin* core = builtin->self;
    g_log.clear();
    CHECK(sco::app::Reload("m0", &why) == sco::Result::Failed && why == "core1 cannot join a reload cascade");
    CHECK(Unloaded().empty() && Find("m0")->self == before && builtin->self == core &&
          ReloadFiles(root / "m0") == 0);
    sco::app::Stop();
    fs::remove_all(base, ec);
}

// ---- [settings]: declared in plugin.ini, read by a script, set by the product, kept across a restart ----

static std::vector<std::string> g_setEvents;
static void OnSettingsChanged(const char*, const void* data, void*) {
    const auto* c = static_cast<const sco_settings_changed*>(data);
    g_setEvents.push_back(std::string(c->plugin) + "." + c->name);
}

static void WriteSettingsPlugin(const fs::path& dir, const char* speedDecl) {
    fs::create_directories(dir);
    std::FILE* f = std::fopen((dir / "plugin.ini").string().c_str(), "wb");
    std::fputs("id = setdemo\nname = Settings demo\nversion = 1.0.0\napi = 1.0\nkind = lua\nentry = main.lua\n[settings]\n", f);
    std::fputs(speedDecl, f);
    std::fputs("\nmode = enum(easy,normal,hard) default normal\ngod_mode = bool\n", f);
    std::fclose(f);
    f = std::fopen((dir / "main.lua").string().c_str(), "wb");
    std::fputs("sco.register_command{ name = 'setdemo.show', title = 'Show', fn = function()\n"
               "  return tostring(sco.settings.get('speed')) .. ' ' .. sco.settings.get('mode') .. ' ' .. tostring(sco.settings.get('god_mode'))\n"
               "end }\n", f);
    std::fclose(f);
}

// A reloaded plugin gets its [settings] declared again, through the same path as at Start (plugin.ini
// is not read again), and the kept values are still there: they belong to the plugin id.
static void TestReloadSettings(const fs::path& out) {
    const fs::path base = out / "reload_settings";
    std::error_code ec;
    fs::remove_all(base, ec);
    WriteSettingsPlugin(base / "plugins" / "setdemo", "speed = int default 5 min 1 max 10");
    sco::app::Platform pf;
    pf.hostVersion = "test-app 1.0";
    pf.pluginRoot = base / "plugins";
    pf.dataRoot = base / "data";
    pf.pluginsEnabled = true;
    pf.scripts = &kLua;
    CHECK(sco::app::Start(pf));
    CHECK(Invoke("setdemo.show").text == "5 normal false");
    CHECK(sco::settings::SetInt("setdemo", "speed", 8) == sco::Result::Ok);
    CHECK(sco::settings::SetString("setdemo", "mode", "hard") == sco::Result::Ok);
    const sco_plugin* before = Find("setdemo")->self;
    CHECK(sco::app::Reload("setdemo") == sco::Result::Ok);
    CHECK(Find("setdemo")->self != before && StateOf("setdemo") == State::Loaded);
    CHECK(sco::settings::Entries("setdemo").size() == 3 && sco::settings::Pages().size() == 1);   // declared once, not twice
    CHECK(Invoke("setdemo.show").text == "8 hard false");                    // read by the new script from the kept values
    CHECK(sco::settings::SetInt("setdemo", "speed", 9) == sco::Result::Ok && Invoke("setdemo.show").text == "9 hard false");
    sco::app::Stop();
    fs::remove_all(base, ec);
}

static void TestAppSettings(const fs::path& out) {
    const fs::path base = out / "app_settings";
    std::error_code ec;
    fs::remove_all(base, ec);
    WriteSettingsPlugin(base / "plugins" / "setdemo", "speed = int default 5 min 1 max 10");

    sco::app::Platform pf;
    pf.hostVersion = "test-app 1.0";
    pf.pluginRoot = base / "plugins";
    pf.dataRoot = base / "data";
    pf.pluginsEnabled = true;
    pf.scripts = &kLua;
    static int token;   // the owner of the test's own subscription

    // First run: the script reads the declared defaults; the product's sets change them, once each.
    g_log.clear();
    g_setEvents.clear();
    CHECK(sco::app::Start(pf));
    CHECK(StateOf("setdemo") == State::Loaded);
    CHECK(sco::Subscribe(&token, SCO_SETTINGS_CHANGED_EVENT, OnSettingsChanged, nullptr) == sco::Result::Ok);
    CHECK(Invoke("setdemo.show").text == "5 normal false");
    const auto pages = sco::settings::Pages();
    CHECK(pages.size() == 1 && pages[0].plugin == "setdemo" && pages[0].entries.size() == 3 && pages[0].tab.empty());
    CHECK(sco::settings::SetInt("setdemo", "speed", 8) == sco::Result::Ok);
    CHECK(sco::settings::SetString("setdemo", "mode", "hard") == sco::Result::Ok);
    CHECK(sco::settings::SetBool("setdemo", "god_mode", true) == sco::Result::Ok);
    CHECK(sco::settings::SetInt("setdemo", "speed", 8) == sco::Result::Ok);          // unchanged: silent
    CHECK(sco::settings::SetInt("setdemo", "speed", 99) == sco::Result::BadArg);     // refused: silent
    CHECK((g_setEvents == std::vector<std::string>{ "setdemo.speed", "setdemo.mode", "setdemo.god_mode" }));
    CHECK(Invoke("setdemo.show").text == "8 hard true");
    sco::app::Stop();
    CHECK(sco::settings::Entries("setdemo").empty() && !sco::settings::Started());

    // Second run over the same data folder: the values are still there.
    g_log.clear();
    CHECK(sco::app::Start(pf));
    CHECK(Invoke("setdemo.show").text == "8 hard true");
    CHECK(!Logged("kept value dropped"));
    sco::app::Stop();

    // The plugin updates and speed becomes a string: its kept int is dropped, with a log line,
    // the other two settings are kept.
    WriteSettingsPlugin(base / "plugins" / "setdemo", "speed = string default fast");
    g_log.clear();
    CHECK(sco::app::Start(pf));
    CHECK(Invoke("setdemo.show").text == "fast hard true");
    CHECK(Logged("[settings] setdemo.speed: kept value dropped (it was saved as int, the setting is string now); the default applies"));
    sco::app::Stop();
    g_log.clear();
    CHECK(sco::app::Start(pf));                    // the dropped value was deleted: nothing to say twice
    CHECK(Invoke("setdemo.show").text == "fast hard true" && !Logged("kept value dropped"));
    sco::app::Stop();
    CHECK(sco::Release(&token) == sco::Result::Ok);
    fs::remove_all(base, ec);
}

// ---- shutdown reentrancy --------------------------------------------------------------------

static int g_stopExits = 0, g_stopUnloads = 0;
static void StopExit(const char*, const void*, void*) {
    if (++g_stopExits == 1) sco::app::Stop();   // bounded even before the regression is fixed
    CHECK(StateOf("stopprobe") == State::Loaded);   // the exit callback must unwind first
}
static const sco_plugin_info* StopQuery() {
    static const sco_plugin_info info{ sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
                                       "stopprobe", "1.0.0", "tests" };
    return &info;
}
static sco_result StopLoad(const sco_api* api, sco_plugin* self) {
    return api->subscribe(self, "game.exit", StopExit, nullptr);
}
static void StopUnload() {
    if (++g_stopUnloads == 1) sco::app::Stop();
}
static void TestRecursiveShutdown() {
    static const P::Builtin b{ "stopprobe", StopQuery, StopLoad, StopUnload };
    sco::app::Platform pf;
    pf.builtins = &b; pf.nBuiltins = 1;
    g_stopExits = g_stopUnloads = 0;
    CHECK(sco::app::Start(pf));
    sco::app::Stop();
    CHECK(g_stopExits == 1 && g_stopUnloads == 1);
    CHECK(StateOf("stopprobe") == State::Unloaded);
    sco::app::Stop();
    CHECK(g_stopExits == 1 && g_stopUnloads == 1);
}

static void StopDuringEvent(const char*, const void*, void*) {
    sco::app::Stop();
    CHECK(StateOf("stopprobe") == State::Loaded);
    CHECK(g_stopUnloads == 0);
}
static void TestShutdownCalloutSafety() {
    static const P::Builtin b{ "stopprobe", StopQuery, StopLoad, StopUnload };
    sco::app::Platform pf;
    pf.builtins = &b; pf.nBuiltins = 1;
    g_stopExits = g_stopUnloads = 0;
    CHECK(sco::app::Start(pf));
    std::thread worker([] { sco::app::Stop(); });
    worker.join();
    CHECK(StateOf("stopprobe") == State::Loaded && g_stopExits == 0);
    CHECK(sco::Subscribe(Find("stopprobe")->self, "stop.test", StopDuringEvent, nullptr) == sco::Result::Ok);
    CHECK(sco::Dispatch("stop.test", nullptr) == sco::Result::Ok);
    sco::app::Stop();
    CHECK(g_stopExits == 1 && g_stopUnloads == 1);
}

static const sco_api* g_drainApi = nullptr;
static sco_plugin* g_drainSelf = nullptr;
static int g_drainRuns = 0, g_drainDrops = 0, g_drainDones = 0, g_drainExits = 0;
static void DrainRun(void*) { ++g_drainRuns; }
static void DrainDrop(void*) { ++g_drainDrops; }
static void DrainDone(sco::Result, const char*, void*) { ++g_drainDones; }
static void DrainEvent(const char*, const void*, void*) { ++g_drainRuns; }
static sco::Result DrainCommand(const sco::Arg*, uint32_t, void*, char*, uint32_t) {
    ++g_drainRuns; return sco::Result::Ok;
}
static const sco_plugin_info* DrainQuery() {
    static const sco_plugin_info info{ sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
                                       "drainprobe", "1.0.0", "tests" };
    return &info;
}
static void DrainExit(const char*, const void*, void*) {
    ++g_drainExits;
    // game.exit still sees the live session; teardown begins only after this returns.
    const void* table = nullptr;
    CHECK(g_drainApi->query_service("sco.ui", 0x00010000, &table) == SCO_OK && table);
    CHECK(g_drainApi->run_on_game_thread(g_drainSelf, DrainRun, nullptr) == SCO_OK);
}
static sco_result DrainLoad(const sco_api* api, sco_plugin* self) {
    g_drainApi = api; g_drainSelf = self;
    static int table;
    CHECK(api->provide_service(self, "drainprobe.svc", 0x00010000, &table) == SCO_OK);
    CHECK(api->subscribe(self, "game.exit", DrainExit, nullptr) == SCO_OK);
    const sco::Command cmd{ "drainprobe.run", "", "", nullptr, nullptr, 0, DrainCommand, nullptr };
    CHECK(sco::RegisterCommand(self, "drainprobe", cmd) == sco::Result::Ok);
    return SCO_OK;
}
static void DrainUnload() {
    CHECK(sco::QueuedTasks() == 0);
    CHECK(g_drainDrops == 3);   // plugin invoke, host-owned task and ownerless task cleaned first
    const void* table = reinterpret_cast<const void*>(1);
    CHECK(g_drainApi->query_service("drainprobe.svc", 0x00010000, &table) == SCO_UNAVAILABLE && !table);
    table = reinterpret_cast<const void*>(1);
    CHECK(g_drainApi->query_service("sco.ui", 0x00010000, &table) == SCO_UNAVAILABLE && !table);
    static int replacement;
    CHECK(g_drainApi->provide_service(g_drainSelf, "drainprobe.new", 0x00010000, &replacement) == SCO_UNAVAILABLE);
    CHECK(g_drainApi->run_on_game_thread(g_drainSelf, DrainRun, nullptr) == SCO_UNAVAILABLE);
    CHECK(sco::Post(DrainRun, nullptr) == sco::Result::Unavailable);
    CHECK(sco::Dispatch("drain.event", nullptr) == sco::Result::Unavailable);
    CHECK(sco::Invoke("drainprobe.run", nullptr, 0, DrainDone, nullptr) == sco::Result::Unavailable);
    CHECK(sco::DrainTasks() == 0);
    sco::Result posted = sco::Result::Ok;
    sco_result queried = SCO_OK;
    std::thread worker([&] {
        posted = sco::Post(DrainRun, nullptr, sco::host::HostOwner());
        const void* peer = reinterpret_cast<const void*>(1);
        queried = g_drainApi->query_service("sco.ui", 0x00010000, &peer);
        CHECK(!peer);
    });
    worker.join();
    CHECK(posted == sco::Result::Unavailable && queried == SCO_UNAVAILABLE);
}
static void TestShutdownDrain() {
    static const P::Builtin b{ "drainprobe", DrainQuery, DrainLoad, DrainUnload };
    sco::app::Platform pf;
    pf.builtins = &b; pf.nBuiltins = 1;
    g_drainRuns = g_drainDrops = g_drainDones = g_drainExits = 0;
    CHECK(sco::app::Start(pf));
    CHECK(sco::Subscribe(sco::host::HostOwner(), "drain.event", DrainEvent, nullptr) == sco::Result::Ok);
    CHECK(sco::detail::PostOwned(DrainRun, DrainDrop, nullptr, sco::host::HostOwner()) == sco::Result::Ok);
    CHECK(sco::detail::PostOwned(DrainRun, DrainDrop, nullptr, nullptr) == sco::Result::Ok);
    CHECK(sco::Post(DrainRun, nullptr, g_drainSelf) == sco::Result::Ok);
    sco::Result queued = sco::Result::Failed;
    std::thread worker([&] {
        queued = sco::detail::InvokeOwned("drainprobe.run", nullptr, 0, DrainDone, nullptr, g_drainSelf, DrainDrop);
    });
    worker.join();
    CHECK(queued == sco::Result::Ok);
    sco::app::Stop();
    CHECK(g_drainExits == 1 && g_drainRuns == 0 && g_drainDones == 0 && g_drainDrops == 3);
    CHECK(sco::QueuedTasks() == 0 && sco::DrainTasks() == 0);
    CHECK(sco::Post(DrainRun, nullptr) == sco::Result::Unavailable);
    CHECK(sco::Unsubscribe(sco::host::HostOwner(), "drain.event", DrainEvent) == sco::Result::Ok);
    // A new host session reopens admission, without replaying cancelled work.
    pf.builtins = nullptr; pf.nBuiltins = 0;
    CHECK(sco::app::Start(pf));
    CHECK(sco::Post(DrainRun, nullptr) == sco::Result::Ok && sco::DrainTasks() == 1);
    CHECK(g_drainRuns == 1);
    sco::app::Stop();
}

static int g_overflowDrops = 0;
static void OverflowDrop(void* ctx) {
    CHECK(*static_cast<int*>(ctx) == g_overflowDrops);   // exact FIFO, no duplicate cleanup
    ++g_overflowDrops;
}
static void DrainDoneC(sco_result, const char*, void*) { ++g_drainDones; }
static void TestShutdownQueueOverflow() {
    sco::app::Platform pf;
    CHECK(sco::app::Start(pf));
    g_overflowDrops = g_drainRuns = g_drainDones = 0;
    std::vector<int> contexts(sco::kMaxQueuedTasks + 5);
    for (size_t i = 0; i < contexts.size(); ++i) {
        contexts[i] = static_cast<int>(i);
        CHECK(sco::detail::PostOwned(DrainRun, OverflowDrop, &contexts[i], nullptr) == sco::Result::Ok);
    }
    const sco_api* api = sco::host::BuildApi({ "shutdown-test" });
    sco_plugin* caller = sco::host::NewPlugin("overflowcaller");
    sco_result queued = SCO_FAILED;
    std::thread worker([&] {
        std::string text = "copied-before-caller-unwinds";
        sco_arg arg{}; arg.type = SCO_ARG_STRING; arg.v.s = text.c_str();
        queued = api->invoke(caller, "sco.reload", &arg, 1, DrainDoneC, nullptr);
    });
    worker.join();
    CHECK(queued == SCO_OK && sco::QueuedTasks() == contexts.size() + 1);
    sco::app::Stop();
    CHECK(g_overflowDrops == static_cast<int>(contexts.size()));
    CHECK(g_drainRuns == 0 && g_drainDones == 0 && sco::QueuedTasks() == 0);
    CHECK(sco::Release(caller) == sco::Result::Ok);
}

static int g_pendingFaultUnloads = 0;
static void PendingFaultUnload() { ++g_pendingFaultUnloads; }
static void TestShutdownPendingFault() {
    static const P::Builtin b{ "stopprobe", StopQuery, StopLoad, PendingFaultUnload };
    sco::app::Platform pf;
    pf.builtins = &b; pf.nBuiltins = 1;
    g_stopExits = g_pendingFaultUnloads = 0;
    CHECK(sco::app::Start(pf));
    // Off-thread attribution has closed the owner but its game-thread verdict is pending.
    CHECK(sco::detail::QuarantineOwner(Find("stopprobe")->self) == sco::Result::Ok);
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(g_pendingFaultUnloads == 0);
    CHECK(StateOf("stopprobe") == State::Faulted);
    CHECK(sco::ui::Started());
    CHECK(sco::app::Stop() == sco::Result::Failed && !sco::app::Start(pf));
}

// These teardown failures intentionally latch the host and therefore run in their own processes.
static sco::Result FixtureHostStart(void*) { return sco::Result::Ok; }
static sco::Result FixtureHostTick(void*) { return sco::Result::Ok; }
static sco::Result FixtureHostStopFail(void* ctx) {
    ++*static_cast<int*>(ctx);
    return sco::Result::Failed;
}
static void TestHostStopRetainsRegistry() {
    sco::Image image{};
    int stopCalls = 0;
    sco::app::Platform pf;
    pf.image = &image;
    pf.imageOwner = pf.resolverOwner = std::make_shared<int>(1);
    pf.setCapabilities = SetCaps;
    pf.hostService = {&stopCalls, FixtureHostStart, FixtureHostTick, FixtureHostStopFail};
    CHECK(sco::app::Start(pf));
    const auto owner = sco::AcquireHostSignatureRegistry();
    CHECK(owner && owner->Snapshot() && sco::caps::Has("teleport"));
    CHECK(sco::app::Stop() == sco::Result::Failed);
    CHECK(stopCalls == 1 && sco::AcquireHostSignatureRegistry() == owner);
    CHECK(owner->Snapshot() && !sco::caps::Has("teleport"));
    CHECK(!sco::app::Start(pf));
}
static void TestHostOwnerMismatch() {
    sco::Image image{};
    sco::app::Platform pf;
    pf.image = &image;
    pf.imageOwner = pf.resolverOwner = std::make_shared<int>(1);
    pf.setCapabilities = SetCaps;
    CHECK(sco::app::Start(pf));
    const auto owner = sco::AcquireHostSignatureRegistry();
    CHECK(owner && owner->Snapshot());
    auto successor = std::make_shared<sco::SignatureRegistry>();
    CHECK(successor->Freeze() == sco::SigRegistryStatus::Ok);
    CHECK(successor->Resolve(image) == sco::SigRegistryStatus::Ok);
    // A synthetic hostile handoff must fail while the app owns its lifecycle pin.
    auto hostileOwner = std::const_pointer_cast<sco::SignatureRegistry>(owner);
    CHECK(!sco::ClearHostSignatureRegistry(hostileOwner, nullptr));
    CHECK(!sco::InstallHostSignatureRegistry(successor, nullptr));
    CHECK(sco::AcquireHostSignatureRegistry() == owner);
    CHECK(owner->Snapshot() && hostileOwner->Resolve(image) == sco::SigRegistryStatus::HostSealed);
    CHECK(sco::app::Stop() == sco::Result::Ok);
    CHECK(!sco::AcquireHostSignatureRegistry() && !sco::caps::Has("teleport"));
    CHECK(sco::app::Start(pf));
    CHECK(sco::app::Stop() == sco::Result::Ok);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: test_app <sdk dir> <out dir>\n"); return 2; }
    sco::SetLogSink(Sink);
    sco::SetGameThread();   // Start sets it too; the loader tests below need it either way
#ifndef SCO_KERNEL_ONLY
#include "game_contracts/app_register_adapter.inc"
#endif
#ifndef _WIN32
    P::SetCallGuard(SignalGuard);
#endif
    if (argc > 3 && std::strcmp(argv[3], "registry-stop-failure") == 0) {
        TestHostStopRetainsRegistry();
        std::printf("registry stop failure: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc > 3 && std::strcmp(argv[3], "registry-owner-mismatch") == 0) {
        TestHostOwnerMismatch();
        std::printf("registry owner mismatch: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc > 3 && std::strcmp(argv[3], "fault-retained") == 0) {
        TestFaultRetainsSession();
        std::printf("fault-retained: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc > 3 && std::strcmp(argv[3], "reload-fault-retained") == 0) {
        TestReload(argv[2], true);
        std::printf("reload-fault-retained: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc > 3 && std::strcmp(argv[3], "cascade-fault-retained") == 0) {
        TestReloadCascade(argv[2], true);
        std::printf("cascade-fault-retained: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc > 3 && std::strcmp(argv[3], "shutdown-only") == 0) {
        TestRecursiveShutdown();
        TestShutdownCalloutSafety();
        TestShutdownDrain();
        TestShutdownQueueOverflow();
        TestShutdownPendingFault();
        std::printf("sco-core shutdown tests: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    TestBuiltinLoader(sco::host::BuildApi({ "test-app 1.0" }), fs::path(argv[2]) / "plugins");
    TestApp(argv[1], argv[2]);
    TestBuiltinIdConflict(argv[2]);
    TestReload(argv[2]);
    TestReloadCascade(argv[2]);
    TestReloadNonCodeDependents(argv[2]);
    TestAppSettings(argv[2]);
    TestReloadSettings(argv[2]);
    P::SetCallGuard(nullptr);
    std::printf("sco-core app tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
