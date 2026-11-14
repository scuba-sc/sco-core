// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// The process-retained pak enable (src/game/pak_retained.h) over a fake loader. Built against the
// production sco_pak, not the unsafe test fixture: the public Enable must stay gated, the retained
// one must hook, and Disable must then refuse. x86-64 only (the loader detour); skipped elsewhere.
#include "sco/caps.h"
#include "sco/game/pak.h"
#include "../src/game/pak_retained.h"
#include "sco/hook.h"
#include "sco/log.h"
#include "sco/vfs.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if (defined(__x86_64__) || defined(_M_X64)) && !defined(__APPLE__)
#define SCO_TEST_PAK 1
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#endif

[[maybe_unused]] static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#if SCO_TEST_PAK
namespace pak = sco::game::pak;
namespace vfs = sco::vfs;

static std::vector<std::string> g_lines;
static void Capture(const char* line) { g_lines.emplace_back(line); }
static bool Logged(const char* needle) {
    for (const auto& l : g_lines) if (l.find(needle) != std::string::npos) return true;
    return false;
}

static void* Page() {
#if defined(_WIN32)
    return VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
    void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
#endif
}
template <class F> static void* Addr(F f) {
    static_assert(sizeof(F) == sizeof(void*), "function pointer size");
    void* p;
    std::memcpy(&p, &f, sizeof p);
    return p;
}
static uintptr_t LoaderBody(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t) { return 1; }
// A detourable entry: three `mov rax, rax`, then an absolute jump to LoaderBody.
static uint8_t* MakeLoaderStub() {
    uint8_t* p = static_cast<uint8_t*>(Page());
    if (!p) return nullptr;
    static const uint8_t kMovs[] = { 0x48, 0x89, 0xC0, 0x48, 0x89, 0xC0, 0x48, 0x89, 0xC0 };
    std::memcpy(p, kMovs, sizeof kMovs);
    uint8_t* j = p + sizeof kMovs;
    j[0] = 0xFF; j[1] = 0x25;
    std::memset(j + 2, 0, 4);
    void* body = Addr(&LoaderBody);
    std::memcpy(j + 6, &body, 8);
    return p;
}

static int g_barrierCalls = 0;
static bool g_barrierResult = true, g_barrierThrow = false;
static bool Barrier(void* context) {
    CHECK(context == &g_barrierCalls);
    ++g_barrierCalls;
    if (g_barrierThrow) throw 1;
    return g_barrierResult;
}
static std::shared_ptr<const vfs::Table> g_table;
static std::shared_ptr<const vfs::Table> Mounts() { return g_table; }

int main() {
    sco::SetLogSink(Capture);
    vfs::Mount m;
    m.path = "data/game2.dcb"; m.source = "test";
    m.producer = vfs::SpliceList{ {}, nullptr };
    g_table = vfs::Table::Build({ std::move(m) });
    CHECK(g_table);
    uint8_t* stub = MakeLoaderStub();
    if (!stub) { std::printf("test_pak_retained: no executable page, skipped\n"); return 77; }
    static uintptr_t cryPak = 0;
    pak::Targets targets;
    targets.loader = stub; targets.cryPak = &cryPak;
    pak::Options o;
    o.mounts = &Mounts;

    // The public entries stay gated in this (production) build.
    CHECK(pak::Enable(o, targets) == sco::Result::Unavailable);
    CHECK(pak::Enable(o) == sco::Result::Unavailable);
    CHECK(!pak::Enabled() && sco::hook::DetourCount() == 0);
    // The retained entry refuses what it does not support, without side effects.
    pak::Options all = o; all.scope = pak::Scope::AllFiles; all.allFilesProbed = true;
    CHECK(pak::retained::Enable(all, targets, Barrier, &g_barrierCalls) == sco::Result::BadArg);
    pak::Options noMounts = o; noMounts.mounts = nullptr;
    CHECK(pak::retained::Enable(noMounts, targets, Barrier, &g_barrierCalls) == sco::Result::BadArg);
    CHECK(!pak::Enabled() && sco::hook::DetourCount() == 0);
    // The entry enforces the shell's attestation itself: null, false and a throw install nothing.
    CHECK(pak::retained::Enable(o, targets, nullptr, nullptr) == sco::Result::Unavailable);
    g_barrierResult = false;
    CHECK(pak::retained::Enable(o, targets, Barrier, &g_barrierCalls) == sco::Result::Unavailable);
    g_barrierResult = true; g_barrierThrow = true;
    CHECK(pak::retained::Enable(o, targets, Barrier, &g_barrierCalls) == sco::Result::Unavailable);
    g_barrierThrow = false;
    CHECK(g_barrierCalls == 3 && !pak::Enabled() && sco::hook::DetourCount() == 0 && !sco::caps::Has("vfs.pak"));
    pak::Disable();
    CHECK(!Logged("disable refused"));
    pak::Disable(); // nothing enabled, nothing latched: a harmless no-op
    CHECK(!Logged("disable refused"));

    CHECK(pak::retained::Enable(o, targets, Barrier, &g_barrierCalls) == sco::Result::Ok);
    CHECK(g_barrierCalls == 4);
    CHECK(pak::Enabled() && sco::hook::DetourCount() == 1 && sco::caps::Has("vfs.pak"));
    CHECK(pak::retained::Enable(o, targets, Barrier, &g_barrierCalls) == sco::Result::BadArg); // already enabled
    // Disable is refused: the patch, the mounts and the capability stay.
    pak::Disable();
    CHECK(Logged("disable refused"));
    CHECK(pak::Enabled() && sco::hook::DetourCount() == 1 && sco::caps::Has("vfs.pak"));
    pak::Disable();
    CHECK(pak::Enabled() && sco::hook::DetourCount() == 1);
    CHECK(g_table && g_table->Mounted("data/game2.dcb"));
    CHECK(pak::Enable(o, targets) == sco::Result::Unavailable); // the public gate is unchanged afterwards
    std::printf("test_pak_retained: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
#else
int main() { std::printf("test_pak_retained: skipped (needs x86-64)\n"); return 77; }
#endif
