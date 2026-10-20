// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "cvars.h"
#include <cstdint>

namespace sco::game::services {
// Windows engine boundary. These are resolved identities, not public handles.
// Receivers are re-queried for each operation. A changed receiver is refused,
// never used to restore a prior value belonging to another native object.
struct CvarsEngineRows {
    uintptr_t console = 0; // address of the console pointer
    uintptr_t lookup = 0;
    uintptr_t reference_vtable = 0;
    uintptr_t get_int = 0;
    uintptr_t set_int = 0;
    uintptr_t flags = 0;
    uintptr_t name = 0;
};
class NativeCvars {
public:
    NativeCvars(CvarsEngineRows rows, bool (*admitted)(), bool (*cleanupAdmitted)() = nullptr)
        : rows_(rows), admitted_(admitted), cleanupAdmitted_(cleanupAdmitted) {}
    CvarsPort Port(bool writes);
    Result Install(CvarsEngineRows rows, bool writes);
private:
    static sco_result Read(void*, uint32_t, uint32_t*);
    static sco_result Write(void*, uint32_t, uint32_t);
    static sco_result Restore(void*, uint32_t, uint32_t);
    sco_result Access(uint32_t, uint32_t*, bool, bool = false);
    CvarsEngineRows rows_;
    bool (*admitted_)();
    bool (*cleanupAdmitted_)();
    uintptr_t identities_[4]{};
};
// Explicit native opt-in; unresolved identities or admission leave an empty port.
Result ConfigureProductionCvars();
}
