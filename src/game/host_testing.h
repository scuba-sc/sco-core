#pragma once
// Private synthetic seam: exercises the exact dispatch used by the installed detour.
// Not a public/plugin header. Does not install a hook or inspect a process.
#include "sco/vfs.h"
#ifdef SCO_GAME_HOST_REPORT_TESTING
#include <atomic>
#endif
namespace sco::game::host::testing {
#ifdef SCO_GAME_HOST_REPORT_TESTING
// Fixed test faults only; no foreign callback or ambient startup authority.
enum class ReportFault { None, BeforeRootAllocation, BeforeOptionsCopy, AfterOptionsCopy, OptionsAllocation };
void SetReportFault(ReportFault) noexcept;
bool ConsumeOptionsAllocationFault() noexcept;
bool RetainedRegistrySurvivesStaticRelease();
// One-shot typed rendezvous, not an executable callback or provider slot.
struct StatusPause { std::atomic<bool> reached{false}, release{false}; };
void PauseNextStatus(StatusPause*) noexcept;
#endif
using QuitFn = void (*)(void* system, void* info, int exitCode);
void ForwardQuit(QuitFn original, void* system, void* info, int exitCode);
void FailAfterAppStart(bool enabled);
void LogAfterRegistryInstall(bool enabled);
bool HasPrivateRegistry();
vfs::Result TransformDataCore(vfs::BaseIo& base, uint64_t size, std::vector<vfs::Splice>& out);
}
