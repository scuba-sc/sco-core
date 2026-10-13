#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Host-source composition; not plugin socket authority. Start after net::Start,
// before plugin load. Tick before queued plugin work; Stop before net::Stop.
#include "sco/net/control.h"
namespace sco::game::private_session {
struct Options {
    net::ControlPolicyV1 policy; // enabled + strict loopback by default
    bool (*admitted)() = nullptr; // positive EAC/offline/official-network evidence
    net::Transport* syntheticTransport = nullptr; // host tests only
};
Result Start(const Options& options);
void Tick();
void Stop();
// Wire through ServiceOptions::ownerReleased, reusing existing net release hook.
void ReleaseOwner(const void* owner);
}
