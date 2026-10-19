// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/runtime.h"
#include <sc_audio.h>
namespace sco::game::services {
// Host-only keys never cross the public ABI. Callbacks are synchronous,
// bounded and nonthrowing. Failed play must leave out_effect zero and no effect;
// successful play returns an independently stoppable effect. Failed stop keeps
// effect/context valid for retry. Provider context outlives retained failures.
struct AudioBackend {
    void* ctx = nullptr;
    sco_result (*resolve)(void*, const char*, uint64_t*) = nullptr;
    sco_result (*play)(void*, uint64_t, uint64_t*) = nullptr;
    sco_result (*stop)(void*, uint64_t) = nullptr;
};
Result StartAudio(const AudioBackend& backend = {});
Result StopAudio();
} // namespace sco::game::services
