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
// Process-retained closure (game host Composition::ProcessRetained): closes admission only. Every
// entry then refuses, and a running session is asked to leave without waiting for its network
// thread. The broker, its table and its sockets stay allocated until process exit; nothing is
// joined, withdrawn or freed. Game thread.
void Close();
// Wire through ServiceOptions::ownerReleased, reusing existing net release hook.
void ReleaseOwner(const void* owner);
}
