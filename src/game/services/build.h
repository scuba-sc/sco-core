#pragma once
#include "sco/runtime.h"
#include <sc_game_build.h>
namespace sco::game::services {
// game.build 1.0: the running exe's identity and the game pack's verified-build pin
// (docs/game/build.md). Pure metadata from the module header; publishes once the game
// pack publishes, no capability, no rows, no engine reads beyond the image header.
Result StartGameBuild();
void StopGameBuild();
}  // namespace sco::game::services
