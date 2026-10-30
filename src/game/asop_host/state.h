// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/game/asop_host.h"
#include <array>
#include <atomic>
#include <mutex>
namespace sco::game::asop_host {
struct Catalog {
    std::array<std::array<char,64>, kMaxShips> names{};
    std::size_t count = 0;
    Result Configure(const Config& config);
};
extern Catalog catalog;
extern Config settings;
extern std::atomic<bool> admitted;
#ifdef _WIN32
bool InstallCatalog();
void InstallFleet();
void InstallHangars();
void TickFleet(uint32_t now);
void TickHangars(uint32_t now);
void CloseFleet();
void CloseHangars();
uint32_t FleetReady();
uint32_t HangarReady();
void LogFleet();
void LogHangars();
void SetFleetCaps();
void SetHangarCaps();
// Rebuild the fleet slot table from the current catalog; game thread only, g_shipsLock held by caller.
void RebuildFleetShips();
#endif
} // namespace sco::game::asop_host
