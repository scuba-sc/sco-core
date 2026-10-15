// SPDX-License-Identifier: GPL-3.0-only
// Unit tests for sco/game/pak.h Scope::AllFiles: persistent slot swaps on a fake ICryPak (a real
// vtable page whose every other slot must stay untouched) serving sco::vfs mounts, including
// files/ replacement mounts (sco::vfs::FileMounts, ReplaceMount) read from a generated pack in a
// temporary folder. The engine side calls through the vtable, from several threads.
// No loader detour is involved. The swapped-slot parts run where sco::hook can make a data page
// writable (a runtime probe: macOS on arm64 refuses the write+execute protection WriteCode asks
// for); the path, pack and refusal checks run everywhere.
//   tools/test.sh
#include "sco/caps.h"
#include "sco/game/pak.h"
#include "sco/hook.h"
#include "sco/log.h"
#include "sco/vfs.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static std::atomic<int> g_fail{ 0 }, g_pass{ 0 };
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

namespace pak = sco::game::pak;
namespace vfs = sco::vfs;
namespace fs = std::filesystem;
using vfs::Bytes;

static std::mutex g_logMu;
static std::vector<std::string> g_lines;
static void Capture(const char* line) {
    std::lock_guard<std::mutex> hold(g_logMu);
    g_lines.emplace_back(line);
}
static bool Logged(const char* needle) {
    std::lock_guard<std::mutex> hold(g_logMu);
    for (const auto& l : g_lines) if (l.find(needle) != std::string::npos) return true;
    return false;
}

template <class F> static void* Addr(F f) {
    static_assert(sizeof(F) == sizeof(void*), "function pointer size");
    void* p;
    std::memcpy(&p, &f, sizeof p);
    return p;
}
template <class F> static F As(void* p) {
    F f;
    std::memcpy(&f, &p, sizeof f);
    return f;
}

// ---- the fake engine: a handle pool that reuses the lowest free handle ---------------------

static Bytes g_base;   // every engine file but menu.xml
static Bytes g_xml;    // Data/Scripts/menu.xml

struct FakeFile { bool used; const Bytes* data; uint64_t pos; };
constexpr size_t kPool = 64;
static std::mutex g_poolMu;
static FakeFile g_pool[kPool];

static uintptr_t HandleOf(size_t i) { return static_cast<uintptr_t>((i + 1) * 16); }
static FakeFile* FileOf(uintptr_t h) {
    const size_t i = static_cast<size_t>(h / 16) - 1;
    return h && h % 16 == 0 && i < kPool ? &g_pool[i] : nullptr;
}
static size_t OpenCount() {
    std::lock_guard<std::mutex> hold(g_poolMu);
    size_t n = 0;
    for (const FakeFile& f : g_pool) n += f.used ? 1 : 0;
    return n;
}

static uintptr_t FakeOpen(uintptr_t, const char* path, const char*, uint32_t) {
    const std::string p = path ? path : "";
    const Bytes* data = nullptr;
    if (p == "Data/Scripts/menu.xml") data = &g_xml;
    else if (p == "Data/Objects/ship.cgf" || p == "Data/Textures/rock.dds" || p == "Data/Scripts/gone.xml" ||
             p == "Data/Scripts/grow.xml" || p == "Data/Throws.bin" || p == "Data/Scripts/big.xml")
        data = &g_base;
    if (!data) return 0;
    std::lock_guard<std::mutex> hold(g_poolMu);
    for (size_t i = 0; i < kPool; ++i)
        if (!g_pool[i].used) {
            g_pool[i] = { true, data, 0 };
            return HandleOf(i);
        }
    return 0;
}
static size_t FakeRead(uintptr_t, void* data, size_t length, size_t elems, uintptr_t file, const void*) {
    FakeFile* f = FileOf(file);
    if (!f || !length) return 0;
    const uint64_t size = f->data->size();
    const uint64_t want = static_cast<uint64_t>(length) * elems;
    const uint64_t n = f->pos >= size ? 0 : (size - f->pos < want ? size - f->pos : want);
    std::memcpy(data, f->data->data() + f->pos, static_cast<size_t>(n));
    f->pos += n;
    return static_cast<size_t>(n / length);
}
static int FakeSeek(uintptr_t, uintptr_t file, int offset, int mode) {
    FakeFile* f = FileOf(file);
    if (!f) return -1;
    const int64_t base = mode == SEEK_SET ? 0 : mode == SEEK_CUR ? static_cast<int64_t>(f->pos) : static_cast<int64_t>(f->data->size());
    if (mode < SEEK_SET || mode > SEEK_END || base + offset < 0) return -1;
    f->pos = static_cast<uint64_t>(base + offset);
    return 0;
}
static int FakeClose(uintptr_t, uintptr_t file) {
    std::lock_guard<std::mutex> hold(g_poolMu);
    FakeFile* f = FileOf(file);
    if (!f || !f->used) return -1;
    f->used = false;
    return 0;
}

constexpr size_t kVtSlots = 4096 / sizeof(void*);
struct FakePak { void** vt; };
static FakePak   g_pakObj;
static uintptr_t g_cryPak = 0;
static std::vector<void*> g_vtBefore;   // the whole vtable as the engine built it

static void** Vt() { return g_pakObj.vt; }
static void* SlotNow(size_t off) { return Vt()[off / 8]; }
static uintptr_t PakPtr() { return reinterpret_cast<uintptr_t>(&g_pakObj); }
static uintptr_t EOpenMode(const char* path, const char* mode) { return As<pak::OpenFn>(SlotNow(pak::kOpenSlot))(PakPtr(), path, mode, 0); }
static uintptr_t EOpen(const char* path) { return EOpenMode(path, "rb"); }
static size_t ERead(uintptr_t f, void* dst, size_t length, size_t elems) {
    return As<pak::ReadFn>(SlotNow(pak::kReadSlot))(PakPtr(), dst, length, elems, f, nullptr);
}
static int ESeek(uintptr_t f, int off, int mode) { return As<pak::SeekFn>(SlotNow(pak::kSeekSlot))(PakPtr(), f, off, mode); }
static int EClose(uintptr_t f) { return As<pak::CloseFn>(SlotNow(pak::kCloseSlot))(PakPtr(), f); }

// Slot indices that differ from the engine's vtable.
static std::vector<size_t> ChangedSlots() {
    std::vector<size_t> out;
    for (size_t i = 0; i < kVtSlots; ++i) if (Vt()[i] != g_vtBefore[i]) out.push_back(i);
    return out;
}
static bool OnlyFourSwapped() {
    const std::vector<size_t> c = ChangedSlots();
    return c == std::vector<size_t>{ pak::kOpenSlot / 8, pak::kReadSlot / 8, pak::kSeekSlot / 8, pak::kCloseSlot / 8 };
}

static Bytes ReadAll(uintptr_t f, size_t chunk) {
    Bytes out;
    std::vector<uint8_t> buf(chunk);
    for (size_t got; (got = ERead(f, buf.data(), 1, chunk)) > 0;)
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(got));
    return out;
}

static bool CapReason(const char* name, const char* needle) {
    sco::caps::Entry e[32];
    const size_t n = sco::caps::List(e, 32);
    for (size_t i = 0; i < n && i < 32; ++i)
        if (!std::strcmp(e[i].name, name)) return !e[i].ready && std::strstr(e[i].reason, needle) != nullptr;
    return false;
}

// ---- mounts ---------------------------------------------------------------------------------

static std::shared_ptr<const vfs::Table> g_table;
static std::shared_ptr<const vfs::Table> CurrentMounts() { return g_table; }

static std::shared_ptr<const Bytes> B(std::initializer_list<uint8_t> b) { return std::make_shared<const Bytes>(b); }

// Data/Objects/ship.cgf: 4 bytes replaced at 100, 5 inserted at 5000.
static vfs::Mount ShipMount() {
    vfs::Mount m;
    m.path = "Data/Objects/ship.cgf";
    m.source = "ships";
    m.producer = vfs::SpliceList{ { { 100, 4, B({ 'A', 'B', 'C', 'D' }), nullptr }, { 5000, 0, B({ 1, 2, 3, 4, 5 }), nullptr } }, nullptr };
    return m;
}
static Bytes Patched() {
    Bytes v(g_base.begin(), g_base.begin() + 100);
    v.insert(v.end(), { 'A', 'B', 'C', 'D' });
    v.insert(v.end(), g_base.begin() + 104, g_base.begin() + 5000);
    v.insert(v.end(), { 1, 2, 3, 4, 5 });
    v.insert(v.end(), g_base.begin() + 5000, g_base.end());
    return v;
}

static void WriteFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}

static const vfs::MountInfo* InfoOf(const std::vector<vfs::MountInfo>& all, const char* path) {
    for (const vfs::MountInfo& m : all) if (m.path == path) return &m;
    return nullptr;
}

static bool SlotSwapsWork() {
#if defined(_WIN32)
    void** p = static_cast<void**>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    void* page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void** p = page == MAP_FAILED ? nullptr : static_cast<void**>(page);
#endif
    if (!p) return false;
    void* orig = nullptr;
    if (sco::hook::SwapSlot(p, Addr(&FakeClose), &orig) != sco::hook::Error::None) return false;
    return sco::hook::RestoreSlot(p) == sco::hook::Error::None;
}

static pak::Options AllOpts() {
    pak::Options o;
    o.scope = pak::Scope::AllFiles;
    o.mounts = &CurrentMounts;
    o.allFilesProbed = true;
    return o;
}
static pak::Targets g_targets;

// ---- tests ----------------------------------------------------------------------------------

static void TestGamePathRefusals(const fs::path& dir) {
    vfs::Mount m;
    m.path = "untouched";
    for (const char* bad : { "", "/data/a.xml", "\\data\\a.xml", "c:/data/a.xml", "data/../secret.txt", "data/..",
                             "./data/a.xml", "data\\.\\a.xml", "..\\..\\x" })
        CHECK(!vfs::ReplaceMount(bad, dir / "x", "p", 0, m));
    CHECK(m.path == "untouched");
    CHECK(vfs::ReplaceMount("data/../x", dir / "x", "p", 0, m).error.find("'..'") != std::string::npos);
    CHECK(!vfs::ReplaceMount("data/a.xml", fs::path(), "p", 0, m));
    CHECK(!vfs::ReplaceMount(std::string(vfs::kMaxPathLength + 1, 'a'), dir / "x", "p", 0, m));
    CHECK(vfs::ReplaceMount("data/a..b/c.xml", dir / "x", "p", 0, m) && m.path == "data/a..b/c.xml");   // dots inside a name are fine
}

static void TestEnableRefusals() {
    pak::Options o = AllOpts();
    o.allFilesProbed = false;
    CHECK(pak::Enable(o, g_targets) == sco::Result::Unavailable);   // default OFF
    CHECK(!sco::caps::Has(pak::kAllFilesCap) && CapReason(pak::kAllFilesCap, "OFF pending in-game confirmation"));
    CHECK(!sco::caps::Has("vfs.pak"));
    o = AllOpts();
    o.size = 4;
    CHECK(pak::Enable(o, g_targets) == sco::Result::BadArg);
    o = AllOpts();
    o.mounts = nullptr;
    CHECK(pak::Enable(o, g_targets) == sco::Result::BadArg);
    pak::Targets none;
    CHECK(pak::Enable(AllOpts(), none) == sco::Result::BadArg);
    const uintptr_t saved = g_cryPak;
    g_cryPak = 0;
    CHECK(pak::Enable(AllOpts(), g_targets) == sco::Result::Unavailable);
    CHECK(CapReason(pak::kAllFilesCap, "CryPak not created yet"));
    g_cryPak = saved;
    CHECK(pak::Enable(AllOpts()) == sco::Result::Unavailable);   // no pak.* rows resolved here
    CHECK(!pak::Enabled() && sco::hook::SlotCount() == 0 && sco::hook::DetourCount() == 0 && ChangedSlots().empty());
}

static void TestServe(const fs::path& dir) {
    const fs::path packDir = dir / "pack1";
    const std::string menu = "<menu replaced=\"yes\"/>";
    WriteFile(packDir / "files" / "Data" / "Scripts" / "menu.xml", menu);
    WriteFile(packDir / "files" / "Data" / "Scripts" / "gone.xml", "soon deleted");
    WriteFile(packDir / "files" / "Data" / "Scripts" / "big.xml", std::string(300, 'x'));   // over the 256 below
    WriteFile(packDir / "datacore" / "unrelated.toml", "format = 1\n");                       // not files/: ignored
    std::error_code ec;
    // May fail without privileges (Windows); the no-"link" check below holds either way.
    fs::create_symlink(packDir / "files" / "Data" / "Scripts" / "menu.xml", packDir / "files" / "Data" / "link.xml", ec);

    std::vector<vfs::Mount> mounts;
    std::vector<std::string> refused;
    CHECK(vfs::FileMounts(packDir, "pack1", 0, mounts, refused, 256));
    CHECK(mounts.size() == 2);   // menu.xml and gone.xml
    CHECK(refused.size() == 1 && refused[0].find("Data/Scripts/big.xml: 300 bytes, over the limit of 256") == 0);
    for (const vfs::Mount& m : mounts) CHECK(m.source == "pack1" && m.path.find("link") == std::string::npos);
    std::vector<vfs::Mount> none;
    CHECK(vfs::FileMounts(dir / "no-such-pack", "x", 0, none, refused, 256) && none.empty());

    // A replacement over its limit by the time the path is first opened, a throwing transform.
    WriteFile(dir / "grow.xml", "small");
    vfs::Mount grow;
    CHECK(vfs::ReplaceMount("Data/Scripts/grow.xml", dir / "grow.xml", "grower", 0, grow, 64));
    WriteFile(dir / "grow.xml", std::string(100, 'g'));
    vfs::Mount throws;
    throws.path = "Data/Throws.bin";
    throws.source = "thrower";
    throws.producer = vfs::Transform([](vfs::BaseIo&, uint64_t, std::vector<vfs::Splice>&) -> vfs::Result {
        throw std::runtime_error("producer bug");
    });
    mounts.push_back(ShipMount());
    mounts.push_back(std::move(grow));
    mounts.push_back(std::move(throws));
    g_table = vfs::Table::Build(std::move(mounts));
    fs::remove(packDir / "files" / "Data" / "Scripts" / "gone.xml");

    CHECK(pak::Enable(AllOpts(), g_targets) == sco::Result::Ok);
    CHECK(pak::Enabled() && sco::caps::Has("vfs.pak") && sco::caps::Has(pak::kAllFilesCap));
    CHECK(sco::hook::SlotCount() == 4 && sco::hook::DetourCount() == 0 && OnlyFourSwapped());
    CHECK(pak::Enable(AllOpts(), g_targets) == sco::Result::BadArg);   // already enabled
    CHECK(pak::LastLoad().outcome == pak::Outcome::None);

    // Splices: open, read, seek, tell, eof, close.
    const Bytes want = Patched();
    uintptr_t f = EOpen("Data/Objects/ship.cgf");
    pak::HandleState hs;
    CHECK(f && pak::ServedHandles() == 1);
    CHECK(pak::QueryHandle(f, hs) && hs.pos == 0 && hs.size == want.size() && !hs.eof);
    CHECK(ReadAll(f, 333) == want);
    CHECK(pak::QueryHandle(f, hs) && hs.pos == want.size() && hs.eof);
    uint8_t q[6];
    CHECK(ESeek(f, 98, SEEK_SET) == 0 && ERead(f, q, 1, 6) == 6 && std::memcmp(q, want.data() + 98, 6) == 0);
    CHECK(pak::QueryHandle(f, hs) && hs.pos == 104 && !hs.eof);
    CHECK(ESeek(f, 4896, SEEK_CUR) == 0 && ERead(f, q, 1, 6) == 6 && std::memcmp(q, want.data() + 5000, 6) == 0);
    CHECK(ESeek(f, -6, SEEK_END) == 0 && pak::QueryHandle(f, hs) && hs.pos == want.size() - 6);
    CHECK(ESeek(f, -1, SEEK_SET) == -1 && ESeek(f, 0, 7) == -1);
    CHECK(pak::QueryHandle(f, hs) && hs.pos == want.size() - 6);   // refused seeks don't move
    CHECK(ESeek(f, 10, SEEK_END) == 0 && ERead(f, q, 1, 6) == 0 && pak::QueryHandle(f, hs) && hs.eof);
    uint32_t words[3];
    CHECK(ESeek(f, -10, SEEK_END) == 0 && ERead(f, words, 4, 3) == 2);   // whole elements
    CHECK(EClose(f) == 0 && pak::ServedHandles() == 0 && !pak::QueryHandle(f, hs));
    CHECK(Logged("[+] [pak] Data/Objects/ship.cgf served from its mount (10000 -> 10005 bytes)"));

    // files/ replacement: the pack file's bytes, the virtual size, from any spelling of the path.
    f = EOpen("Data/Scripts/menu.xml");
    CHECK(pak::QueryHandle(f, hs) && hs.size == menu.size());
    const Bytes got = ReadAll(f, 7);
    CHECK(std::string(got.begin(), got.end()) == menu);
    CHECK(ESeek(f, -9, SEEK_END) == 0 && ERead(f, q, 1, 6) == 6 && std::memcmp(q, menu.data() + menu.size() - 9, 6) == 0);
    CHECK(ESeek(f, 0, SEEK_END) == 0 && pak::QueryHandle(f, hs) && hs.eof && hs.pos == menu.size());
    CHECK(EClose(f) == 0);

    // Refusals at open: missing, oversize, throwing. Each passes through from 0 with the engine's bytes.
    for (const char* p : { "Data/Scripts/gone.xml", "Data/Scripts/grow.xml", "Data/Throws.bin" }) {
        f = EOpen(p);
        CHECK(f && !pak::QueryHandle(f, hs) && pak::ServedHandles() == 0);
        CHECK(ReadAll(f, 4096) == g_base);
        CHECK(EClose(f) == 0);
    }
    const std::vector<vfs::MountInfo> info = g_table->Mounts();
    const vfs::MountInfo* gone = InfoOf(info, "data/scripts/gone.xml");
    const vfs::MountInfo* grown = InfoOf(info, "data/scripts/grow.xml");
    const vfs::MountInfo* threw = InfoOf(info, "data/throws.bin");
    CHECK(gone && gone->state == vfs::MountState::Inert && gone->reason.find("replacement file missing: gone.xml") != std::string::npos);
    CHECK(grown && grown->state == vfs::MountState::Inert && grown->reason.find("100 bytes, over the limit of 64") != std::string::npos);
    CHECK(threw && threw->state == vfs::MountState::Inert && threw->reason.find("producer bug") != std::string::npos);
    CHECK(Logged("[pak] Data/Scripts/gone.xml passed through (pack1: transform failed: replacement file missing: gone.xml)"));

    // Unmounted and opened-to-write files are the engine's.
    f = EOpen("Data/Textures/rock.dds");
    CHECK(f && !pak::QueryHandle(f, hs) && ReadAll(f, 999) == g_base && EClose(f) == 0);
    f = EOpenMode("Data/Objects/ship.cgf", "wb");
    CHECK(f && !pak::QueryHandle(f, hs) && EClose(f) == 0);
    f = EOpenMode("Data/Objects/ship.cgf", "r+b");
    CHECK(f && !pak::QueryHandle(f, hs) && EClose(f) == 0);

    // A served handle closed behind the adapter's back: the engine hands the value out again, and
    // the new file must be the engine's, not the stale virtual one.
    f = EOpen("Data/Objects/ship.cgf");
    CHECK(pak::ServedHandles() == 1);
    CHECK(FakeClose(PakPtr(), f) == 0);
    const uintptr_t g = EOpen("Data/Textures/rock.dds");
    CHECK(g == f && pak::ServedHandles() == 0 && ReadAll(g, 512) == g_base);
    CHECK(EClose(g) == 0);

    // Several threads at once: mounted and unmounted files, opened, read and closed concurrently.
    std::atomic<bool> threadsOk{ true };
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < 20; ++i) {
                const uintptr_t a = EOpen("Data/Objects/ship.cgf");
                const uintptr_t b = EOpen("Data/Textures/rock.dds");
                const uintptr_t c = EOpen("Data/Scripts/menu.xml");
                const Bytes cb = ReadAll(c, 5);
                if (!a || !b || !c || ReadAll(a, 1000) != want || ReadAll(b, 777) != g_base || std::string(cb.begin(), cb.end()) != menu)
                    threadsOk = false;
                EClose(a);
                EClose(b);
                EClose(c);
            }
        });
    for (std::thread& t : threads) t.join();
    CHECK(threadsOk.load() && pak::ServedHandles() == 0);
}

// Disable while a served handle is open: new opens pass through at once, the open handle keeps its
// virtual file, and the slots go back when it closes.
static void TestDisableDrains() {
    const Bytes want = Patched();
    const uintptr_t f = EOpen("Data/Objects/ship.cgf");
    CHECK(pak::ServedHandles() == 1);
    pak::Disable();
    CHECK(!pak::Enabled() && !sco::caps::Has("vfs.pak") && !sco::caps::Has(pak::kAllFilesCap));
    CHECK(sco::hook::SlotCount() == 4 && OnlyFourSwapped());   // draining
    CHECK(Logged("1 served handle(s) still open"));
    const uintptr_t g = EOpen("Data/Objects/ship.cgf");
    pak::HandleState hs;
    CHECK(g && !pak::QueryHandle(g, hs) && ReadAll(g, 4096) == g_base && EClose(g) == 0);
    CHECK(ESeek(f, 0, SEEK_SET) == 0 && ReadAll(f, 4096) == want);

    // DataCoreLoad can't take the slots while they drain; AllFiles takes the drain back.
    pak::Options dc;
    dc.mounts = &CurrentMounts;
    pak::Targets withLoader = g_targets;
    withLoader.loader = &g_cryPak;   // never detoured: refused first
    CHECK(pak::Enable(dc, withLoader) == sco::Result::BadArg);
    CHECK(sco::hook::DetourCount() == 0);
    CHECK(pak::Enable(AllOpts(), g_targets) == sco::Result::Ok && sco::hook::SlotCount() == 4 && OnlyFourSwapped());
    CHECK(pak::QueryHandle(f, hs) && hs.eof);
    pak::Disable();
    CHECK(sco::hook::SlotCount() == 4);
    CHECK(EClose(f) == 0);
    CHECK(sco::hook::SlotCount() == 0 && ChangedSlots().empty() && pak::ServedHandles() == 0);
    CHECK(Logged("[pak] persistent CryPak slots restored"));

    // Nothing open: Disable restores at once; Disable is idempotent.
    CHECK(pak::Enable(AllOpts(), g_targets) == sco::Result::Ok && OnlyFourSwapped());
    pak::Disable();
    CHECK(sco::hook::SlotCount() == 0 && ChangedSlots().empty());
    pak::Disable();
    CHECK(!pak::Enabled() && ChangedSlots().empty());
}

int main() {
    sco::SetLogSink(Capture);
    g_base.resize(10000);
    for (size_t i = 0; i < g_base.size(); ++i) g_base[i] = static_cast<uint8_t>(i * 7 + (i >> 8));
#if defined(_WIN32)
    void** vt = static_cast<void**>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    void* page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void** vt = page == MAP_FAILED ? nullptr : static_cast<void**>(page);
#endif
    CHECK(vt != nullptr);
    if (!vt) return 1;
    // Every slot holds something distinct; only the four pinned ones point at callable code.
    for (size_t i = 0; i < kVtSlots; ++i) vt[i] = reinterpret_cast<void*>(static_cast<uintptr_t>(0x10000 + i * 16));
    vt[pak::kOpenSlot / 8] = Addr(&FakeOpen);
    vt[pak::kReadSlot / 8] = Addr(&FakeRead);
    vt[pak::kSeekSlot / 8] = Addr(&FakeSeek);
    vt[pak::kCloseSlot / 8] = Addr(&FakeClose);
    g_vtBefore.assign(vt, vt + kVtSlots);
    g_pakObj.vt = vt;
    g_cryPak = PakPtr();
    g_targets.cryPak = &g_cryPak;

    const fs::path dir = fs::temp_directory_path() /
        ("sco-test-pak-files-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);

    TestGamePathRefusals(dir);
    TestEnableRefusals();
    const bool swaps = SlotSwapsWork();
    if (swaps) {
        TestServe(dir);
        TestDisableDrains();
    }
    CHECK(OpenCount() == 0);   // every handle the tests opened was closed

    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf("sco-core pak AllFiles tests: %d passed, %d failed%s\n", g_pass.load(), g_fail.load(),
                swaps ? "" : " (slot-swap parts skipped: sco::hook can't write a data page here)");
    return g_fail.load() ? 1 : 0;
}
