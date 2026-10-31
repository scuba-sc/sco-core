#pragma once
// sco::game::pak: the engine adapter for sco::vfs. Serves a mounted game file to Star Citizen's
// CryPak (the engine's own file I/O) by switching four ICryPak vtable slots (open, read, seek,
// close) while the DataCore loader runs. Design: docs/design/vfs-datacore.md § 3 ("The
// game-specific part"); reference: docs/api.md § sco/game/pak.h.
//
// Native activation is currently refused until a product supplies a proven
// execution-exclusion and lifecycle protocol. The descriptions below document
// the adapter's previous behavior and the isolated synthetic fixture.
//
// Rows (src/game/pak_sigs.cpp, moved byte for byte from sc-offline's quantum.cpp):
//   pak.datacore_loader  CDataCoreLoader::InitializeBinary: the function that loads the string
//                        "DCB file is smaller than expected" (lea r9), found through .pdata
//                        (chained unwind info followed to the primary entry), prologue checked
//   pak.crypak           the ICryPak* global: in the loader's first 0x400 bytes,
//                        mov rcx, [rip+X] ... mov rax, [rcx]; call [rax+0x148] (FOpen)
//   pak.slots            the loader calls [rax+0x160], [rax+0x1D0] and [rax+0x1E0] (read, seek,
//                        close) in its first 0x2400 bytes; pins kReadSlot, kSeekSlot, kCloseSlot
//
// Scope::DataCoreLoad (the only scope until plan PR 10): the slots are swapped only while the
// loader runs, and only the first ".dcb" the loader's own thread opens is tracked. Reads and
// seeks on that handle, on that thread, are served by a sco::vfs::Reader over the mount; every
// other handle and every other thread pass straight through to the engine's functions. Tell,
// size and eof aren't routed: the loader doesn't call them on the file (pak.slots checks the
// calls it does make).
//
// Scope::AllFiles (plan PR 10; capability game.pak.all_files, OFF by default): the same four slots
// stay swapped from Enable to Disable, on every thread, and every mounted path is served (files/
// replacement mounts, sco::vfs::FileMounts, as well as splices). Enable refuses it unless
// Options.allFilesProbed is set, which no sco-core caller does: the frame-time cost and the engine's
// use of the unrouted slots are unmeasured. Tell, size and eof slots have no row pinning their
// offsets, so they are never swapped: the engine's own tell/size/eof on a served handle answer for
// the real file. QueryHandle gives the virtual answers.
#include "sco/runtime.h"
#include "sco/vfs.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace sco::game::pak {

// ICryPak vtable byte offsets. kOpenSlot is pinned by pak.crypak, the others by pak.slots.
constexpr size_t kOpenSlot = 0x148, kReadSlot = 0x160, kSeekSlot = 0x1D0, kCloseSlot = 0x1E0;

// The engine's functions (x86-64: one calling convention). `tag` is the engine's debug tag:
// a pointer to a pointer to a function-name string.
using LoaderFn = uintptr_t (*)(uintptr_t loader, uintptr_t path, uintptr_t a3, uintptr_t a4, uintptr_t a5);
using OpenFn   = uintptr_t (*)(uintptr_t pak, const char* path, const char* mode, uint32_t flags);
using ReadFn   = size_t (*)(uintptr_t pak, void* data, size_t length, size_t elems, uintptr_t file, const void* tag);
using SeekFn   = int (*)(uintptr_t pak, uintptr_t file, int offset, int mode);   // 0 = ok
using CloseFn  = int (*)(uintptr_t pak, uintptr_t file);

// What Enable hooks. Resolve fills it from the pak.* rows (after the host publishes its resolved snapshot); tests pass
// their own.
struct Targets {
    void*      loader = nullptr;   // the DataCore loader (LoaderFn)
    uintptr_t* cryPak = nullptr;   // the global holding the ICryPak* (read at each load)
};
bool Resolve(Targets& out);   // false (out untouched) unless all three pak.* rows are OK

enum class Scope { DataCoreLoad, AllFiles };

// Set ready by Enable(Scope::AllFiles) once its slots are swapped; off otherwise, with the reason.
constexpr char kAllFilesCap[] = "game.pak.all_files";

struct LoadReport;

struct Options {
    uint32_t size  = sizeof(Options);
    Scope    scope = Scope::DataCoreLoad;
    // The current mount snapshot (sco::vfs::MountTable::Current), read once per tracked open.
    // Required: sco-core's host kit owns no mount table yet.
    std::shared_ptr<const vfs::Table> (*mounts)() = nullptr;
    // Optional: called once per DataCore load that ran with a load window, with that load's
    // report (the same one LastLoad returns from then on). Rules:
    //   - Thread: the loader's thread (the engine's), right after the loader returned and the
    //     slots went back. The engine's load waits for the callback, so keep it short.
    //   - Locks: called after the adapter released its lock, so it may call LastLoad, Enabled,
    //     Disable or any sco::vfs API; `report` is valid only during the call.
    //   - Faults: an exception escaping it is caught and logged; on MSVC builds a structured
    //     exception (access violation, ...) is caught too. Either way the load's result goes back
    //     to the engine unchanged. Stack corruption and fast-fail are not catchable.
    // Read when the load starts: a load that started before Disable still calls it.
    void (*onLoad)(const LoadReport& report) = nullptr;
    // Scope::AllFiles only: the product's statement that the in-game probe of persistent hooks
    // passed (frame time, and no engine use of tell/size/eof on served handles). False: Enable
    // refuses AllFiles (Unavailable) and game.pak.all_files stays off.
    bool allFilesProbed = false;
};

// Native activation is currently unavailable: neither overload changes code,
// slots, capabilities or invokes callbacks until continuous exclusion is
// established by a separate product lifecycle contract. The former behavior:
// installs the loader detour (sco::hook::InstallDetour, stolen bytes counted by StolenLength) and
// sets the capability "vfs.pak". Ok, or nothing changes and vfs.pak is off with the reason:
// BadArg (size, unknown scope, null mounts, null targets, already enabled, AllFiles slots still
// draining), Unavailable (a pak.* row not OK; Scope::AllFiles without Options.allFilesProbed or
// with the ICryPak* still null), Failed (the detour or slot swap was refused).
// Scope::AllFiles installs no loader detour (Targets.loader unused): it swaps the four slots of
// *Targets.cryPak's vtable at once and sets game.pak.all_files; LastLoad stays None.
Result Enable(const Options& options);
Result Enable(const Options& options, const Targets& targets);

// Removes the loader detour and restores the slots (sco::hook::RestoreSlot), clears "vfs.pak".
// Idempotent. A load running on another thread keeps its slots until it returns (the slots are
// restored at its end either way), so calling Disable mid-load never cuts a file in half.
// Scope::AllFiles: new opens pass through at once; while a served handle is still open the slots
// stay swapped and the handle keeps its virtual file, and the slots go back when the last one
// closes (Enable refuses a new scope until then).
void Disable();
bool Enabled();

// What the last DataCore load did with its file (the product logs or reports it).
enum class Outcome {
    None,       // no load since Enable
    Applied,    // the tracked .dcb was served from its mount
    Passed,     // a .dcb was tracked but passed through: not mounted, or the mount is inert
    NoDcb,      // the loader opened no .dcb
    NoCryPak,   // the ICryPak* was null at load time: nothing swapped
    SwapFailed, // the slots couldn't be swapped (reason names the error)
};
const char* OutcomeName(Outcome o);   // "applied", "passed", ...

struct LoadReport {
    Outcome     outcome = Outcome::None;
    bool        loaderOk = false;     // the loader returned true (low byte)
    std::string path;                 // the tracked .dcb as the engine named it
    uint64_t    baseSize = 0, size = 0;   // the real file and the virtual one (Applied)
    std::string reason;               // why it passed through, or what failed
    uint64_t    durationMs = 0;       // the engine's loader call, start to return (steady clock)
};
LoadReport LastLoad();

// Scope::AllFiles: a served handle as its virtual file sees it (what the unswapped tell, size and
// eof slots would answer if they were routed). False for any handle that isn't served.
struct HandleState {
    uint64_t pos = 0, size = 0;
    bool     eof = false;   // at or past the end
};
bool QueryHandle(uintptr_t file, HandleState& out);
size_t ServedHandles();   // served handles open now

}  // namespace sco::game::pak
