#pragma once
// The game.* bus events inside the game pack: started and stopped by services::Start / Stop
// (spatial.cpp), after game.actors, whose state probe the death hook feeds. Game thread.
#include "sco/runtime.h"

namespace sco::game::services {

// Sets the game.events.* capabilities from the rows, installs the hooks whose rows are OK and
// subscribes the tick that publishes. Never fails on a missing row (that event's capability is
// off). If cleanup after a failed start cannot remove a hook, it is retained and StopEvents
// returns Failed until process exit.
Result StartEvents();
// Removes the hooks, unsubscribes the tick and turns the capabilities off.
// Failed means a hook may remain installed and its backing state is retained.
Result StopEvents();

}  // namespace sco::game::services
