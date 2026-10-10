#pragma once
// game.creative inside the game pack: started and stopped by services::Start / Stop (spatial.cpp),
// after game.entities, whose spawn.cpp release hook it joins. Game thread.
#include "sco/runtime.h"

namespace sco::game::services {

// Sets the game.creative.* capabilities from the rows and publishes game.creative 1.0 with its tick.
// BadArg when the name is taken; nothing is left published then.
Result StartCreative();
// Switches every toggle off, removes the magazine hook, withdraws the table.
void   StopCreative();

// spawn.cpp's release hook: owner unloaded or crashed, so every toggle it switched on goes off
// (god mode, fly, the fly speed, both ammo toggles) and its last error is forgotten. Game thread.
void ReleaseCreativeOwner(const void* owner);

}  // namespace sco::game::services
