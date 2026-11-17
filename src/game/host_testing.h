#pragma once
// Private synthetic seams. Not a public/plugin header. No test installs a hook
// or inspects a process. ForwardQuit is the legacy synchronous helper; the
// test-only InvokeQuitDetourForTest exercises the actual current detour.
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
enum class QuitOwnerPauseStage { BeforePublication, AfterPublication };
struct QuitOwnerPause {
    QuitOwnerPauseStage stage = QuitOwnerPauseStage::BeforePublication;
    std::atomic<bool> reached{false}, release{false};
};
void PauseNextQuitOwner(QuitOwnerPause*) noexcept;
void FailNextQuitOwnerAllocation() noexcept;
void FailNextQuitOwnerNotification() noexcept;
#endif
using QuitFn = void (*)(void* system, void* info, int exitCode);
#ifdef SCO_GAME_HOST_REPORT_TESTING
// Set only before creating test threads. No real hook is installed.
void SetQuitOriginalForTest(QuitFn) noexcept;
void InvokeQuitDetourForTest(void* system, void* info, int exitCode);
#endif
void ForwardQuit(QuitFn original, void* system, void* info, int exitCode);
void FailAfterAppStart(bool enabled);
void LogAfterRegistryInstall(bool enabled);
bool HasPrivateRegistry();
vfs::Result TransformDataCore(vfs::BaseIo& base, uint64_t size, std::vector<vfs::Splice>& out);
}
