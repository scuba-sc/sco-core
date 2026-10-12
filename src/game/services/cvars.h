// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/runtime.h"
#include <sc_cvars.h>
namespace sco::game::services {
// Host-only engine port. No engine ABI guessed here. Index is the exact allowlist
// row, not a string/console command. Adapter must contain native faults and return
// copied canonical bools. Production has no verified port and remains unavailable.
struct CvarsPort {
    void* ctx = nullptr;
    bool verified = false;
    bool offline_admitted = false;
    sco_result (*read_bool)(void*, uint32_t, uint32_t*) = nullptr;
};
Result SetCvarsPort(const CvarsPort& port); // game thread, stopped only
Result StartCvars();
void StopCvars();
}
