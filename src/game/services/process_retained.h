// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
#include "sco/runtime.h"

namespace sco::game::services::detail {
// Private. Called only by the app runtime started through app::detail::StartProcessRetained
// (game::host Composition::ProcessRetained). The entry enforces its own preconditions: the caller
// states the app runtime is process-retained, and the shell's reader-quiescence attestation is
// called here, immediately before the first native mutation. Anything else (null or false barrier,
// a throw, not process-retained) is Unavailable with nothing started. Runs the real game-service
// start; the public services::Start() stays an unconditional refusal. A runtime stopped by closure
// never calls Stop/Finalize for these services: providers stay published and refuse through the
// runtime-stopping flag. Every native hook the services install goes in during this call, under the
// attestation (creative's magazine hook included); none is installed lazily afterwards.
struct ProcessRetainedStart {
    bool (*barrier)(void*) = nullptr;
    void* context = nullptr;
    bool appProcessRetained = false;
};
Result StartProcessRetained(const ProcessRetainedStart& start);
}
