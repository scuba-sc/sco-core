#pragma once
// The death hook's decision, as a portable unit: what one death record means, and which bus events
// that turns into. Kept out of events.cpp (which is Windows-only: it reads game memory under SEH)
// so the rule itself can be exercised on any host, through the real event bus, without a game.
//
// The rule, in one place, because it is the whole point of the fix:
//   - One hook owns the game's death path, so every actor's death arrives here exactly once.
//   - game.actors.died is published for every death, whatever actor it was, and it is published even
//     when the dying entity's id couldn't be read (0): an occurrence is never dropped for want of a
//     name. A subscriber that needs to know which entity died checks the id.
//   - game.player.died is published only when the actor that died is your player, and it carries the
//     id read for your player, not the hook's. game.player.died is never suppressed because
//     game.actors.died's capability is off, and game.actors.died is never suppressed because
//     game.player.died's is: the two are independent answers over one record.
//   - Neither event fires while its own capability is off.
#include "sco/runtime.h"
#include <sc_game_events.h>
#include <cstdint>
#include <cstdio>

namespace sco::game::services {

// One hook occurrence. `a` is the actor the game passed; `b` is the second pointer (seat only);
// `id` is the dying entity's id, read by the hook before the game's own code ran, 0 when unreadable.
struct Record { int kind; uintptr_t a, b; uint64_t id; };

// The kinds, shared so a test can name one. events.cpp keeps its own enum with these values.
constexpr int RecordKindSpawn = 1, RecordKindDeath = 2, RecordKindSeatEnter = 3, RecordKindSeatExit = 4;

// What a death record means. Purely a function of its arguments: no game memory, no Windows.
//
// `mine` is the caller's answer to "is this actor your player"; `entityId` is the id that belongs
// with that answer (your player's, as last read). Returns how many events were published.
inline int DeathDispatch(const Record& r, bool mine, uint64_t entityId, bool capActorDied, bool capDied) {
    int published = 0;
    if (capActorDied) {
        const sc_game_actor_died p = { sizeof(p), 0, r.id, 0 };
        size_t called = 0;
        if (Dispatch(SC_GAME_EVENT_ACTOR_DIED, &p, &called) == Result::Ok) ++published;
    }
    // Only your own death is your player's death. The id is your player's, which is a different read
    // from the hook's: it is the one that was current when the death happened, and it is what
    // game.player.died has always carried.
    if (mine && capDied) {
        const sc_game_player_died p = { sizeof(p), 0, entityId, 0 };
        size_t called = 0;
        if (Dispatch(SC_GAME_EVENT_PLAYER_DIED, &p, &called) == Result::Ok) ++published;
    }
    return published;
}

}  // namespace sco::game::services
