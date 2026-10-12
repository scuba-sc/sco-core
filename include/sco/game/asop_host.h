// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Host-source composition only. Not shipped as a plugin header or query_service.
#include <cstddef>
#include <cstdint>

namespace sco::game::asop_host {
constexpr std::size_t kMaxShips = 2048;
enum class CatalogMode : uint32_t { Game, Ships };
enum class Result : uint32_t { Ok, Disabled, InvalidConfig, AdmissionRefused, Unavailable, AlreadyStarted };
struct Config {
    uint32_t size = sizeof(Config);
    bool enabled = false;
    // Host must positively establish offline preparation and absence of EAC.
    bool offlinePrepared = false;
    bool eacAbsent = false;
    bool diagnostics = false;
    CatalogMode catalog = CatalogMode::Game;
    const char* const* shipClasses = nullptr;
    std::size_t shipCount = 0;
};
struct Status { bool configured = false, admitted = false, installed = false, stopped = false;
                std::size_t catalogCount = 0; };
// Start once after signatures AND guarded game services are ready, before engine callers run.
// Explicit config replaces product environment/filesystem globals. Copies bounded classes.
Result Start(const Config& config);
Status Snapshot();
// Game/window-message thread: pump on EVERY message, not just next-tick dispatch.
void Pump(uint32_t now);
// Terminal shutdown closes custom work. Hooks/backing state remain process-lifetime;
// do not unload or restart the game-pack image. Caller joins engine workers before unload.
void Stop();
} // namespace sco::game::asop_host
