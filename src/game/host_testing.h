#pragma once
// Private synthetic seam: exercises the exact dispatch used by the installed detour.
// Not a public/plugin header. Does not install a hook or inspect a process.
#include "sco/vfs.h"
namespace sco::game::host::testing {
using QuitFn = void (*)(void* system, void* info, int exitCode);
void ForwardQuit(QuitFn original, void* system, void* info, int exitCode);
void FailAfterAppStart(bool enabled);
void LogAfterRegistryInstall(bool enabled);
vfs::Result TransformDataCore(vfs::BaseIo& base, uint64_t size, std::vector<vfs::Splice>& out);
}
