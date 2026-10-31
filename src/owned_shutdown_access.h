// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/owned_shutdown.h"

namespace sco {
// Internal trusted host composition capability. Ordinary owner clients get
// reader/worker APIs, not startup publication control.
class OwnedShutdownHostAccess {
public:
    static bool begin_startup(OwnedShutdown& owner) { return owner.begin_startup(); }
    static OwnedShutdown::Transition transition(OwnedShutdown& owner) { return owner.try_transition(); }
    static bool on_transition_thread(const OwnedShutdown& owner) { return owner.on_transition_thread(); }
};
}
