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
// Live installed groups, not gameplay certification. Readiness disappears on Stop.
enum Ready : uint32_t {
    Terminal=1u<<0, Caller=1u<<1, List=1u<<2, Deliver=1u<<3,
    Claim=1u<<4, Retrieve=1u<<5, Lift=1u<<6, Store=1u<<7,
    Hangar=1u<<8, Tokens=1u<<9, Diagnostics=1u<<10
};
struct Status { bool configured = false, admitted = false, installed = false, stopped = false;
                std::size_t catalogCount = 0; uint32_t ready = 0; };
// Start validates the configuration, enabled flag, and offline/EAC admission, then
// returns Unavailable without installing hooks or publishing admission. A proven
// safe phase and reader/worker lifetime protocol is not yet supplied by the host.
// Stop is terminal; Start and Stop must not overlap.
Result Start(const Config& config);
// Windows-only fleet query: is any ship of this class stored, retrieved, delivering or
// queued this session? Returns true for a null name, inactive admission, or calls
// off the game thread, or before the fleet is built, so an unavailable query
// cannot authorize removal. Once built, an unknown class returns false. The caller must link the Windows provider.
bool FleetClassBusy(const char* name);
// Currently always returns Unavailable, before validation, engine calls or publication;
// the previous configuration/catalog is unchanged. No engine lifecycle contract yet
// establishes safe mid-session rebuilding or rebuild/Stop overlap. Internal transactional
// fleet building and stable slots do not establish engine quiescence.
Result Reconfigure(const Config& config);
Status Snapshot();
// Game/window-message thread: pump on EVERY message, not just next-tick dispatch.
void Pump(uint32_t now);
// Terminal shutdown closes custom work. Hooks/backing state remain process-lifetime;
// do not unload or restart the game-pack image. Caller joins engine workers before unload.
void Stop();
} // namespace sco::game::asop_host
