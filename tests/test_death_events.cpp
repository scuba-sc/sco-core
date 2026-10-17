// The death hook's one-owner rule, over the real event bus: one hook, two events.
//
// The game's death path is detoured once, by the game pack. A consumer that needs every actor death
// (sc-offline's contracts retire dead hostiles from it) subscribes to game.actors.died; the
// local-player view stays game.player.died. Before this, sc-offline detoured the same function a
// second time for the same reason, sco::hook refused the second detour (one per target), and
// game.player.died never fired at all. These tests pin the rule that replaced it, so a second detour
// is never needed again:
//
//   - every death publishes game.actors.died, whatever actor it was;
//   - only your own death also publishes game.player.died, with your player's id;
//   - an unreadable id (0) still publishes game.actors.died, and never fabricates a player death;
//   - each event is gated by its own capability only: one being off never suppresses the other.
//
// The dispatch itself is the production code (src/game/services/death_events.h), called over the real
// sco::Dispatch with real subscribers, so this exercises the same payloads and the same bus a plugin
// sees in game. It is portable: the decision is a pure function, deliberately kept out of
// events.cpp, which is Windows-only (it reads game memory under SEH).
//   tools/test.sh
#include "sco/host.h"
#include "sco/runtime.h"
#include <sc_game_events.h>
#include <cstdio>
#include <cstring>
#include <vector>

// The production decision, included by path relative to src/ like the services include each other.
#include "game/services/death_events.h"

namespace D = sco::game::services;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// What a subscriber saw, so the assertions are about the payload the bus actually delivered.
struct Seen {
    std::vector<uint64_t> actorIds;    // game.actors.died: entity_id of each death
    std::vector<uint64_t> playerIds;   // game.player.died: entity_id of each death
    std::vector<uint32_t> actorSizes;  // the size each payload declared, as delivered
};

static Seen g_seen;

static void OnActorDied(const char*, const void* data, void*) {
    const auto* d = static_cast<const sc_game_actor_died*>(data);
    g_seen.actorIds.push_back(d->entity_id);
    g_seen.actorSizes.push_back(d->size);
}

static void OnPlayerDied(const char*, const void* data, void*) {
    const auto* d = static_cast<const sc_game_player_died*>(data);
    g_seen.playerIds.push_back(d->entity_id);
}

static void Reset() { g_seen = Seen{}; }

// One death record, dispatched with the given capabilities, exactly as the tick would.
static void Fire(const D::Record& r, bool mine, uint64_t myId, bool capActorDied, bool capDied) {
    D::DeathDispatch(r, mine, myId, capActorDied, capDied);
}

// A death of an actor that is not your player, and one that is.
static D::Record DeathOf(uint64_t id, uintptr_t actor) { return D::Record{ D::RecordKindDeath, actor, 0, id }; }

int main() {
    // The bus is game-thread only; this test's main thread is the game thread.
    sco::SetGameThread();
    CHECK(sco::Subscribe(sco::host::HostOwner(), SC_GAME_EVENT_ACTOR_DIED, OnActorDied, nullptr) == sco::Result::Ok);
    CHECK(sco::Subscribe(sco::host::HostOwner(), SC_GAME_EVENT_PLAYER_DIED, OnPlayerDied, nullptr) == sco::Result::Ok);

    // Every death is published, whichever actor it was: this is what contracts consumes, and the
    // reason it no longer needs a detour of its own.
    Reset();
    Fire(DeathOf(0x1000, 0xAAAA0000), false, 0x2000, true, true);
    CHECK(g_seen.actorIds.size() == 1 && g_seen.actorIds[0] == 0x1000);
    CHECK(g_seen.playerIds.empty());                       // not your player: no game.player.died
    CHECK(g_seen.actorSizes.size() == 1 && g_seen.actorSizes[0] == sizeof(sc_game_actor_died));

    // Your own death publishes both, and game.player.died carries your player's id, which is a
    // different read from the hook's: the record's id is the actor's, your id is what was last read
    // for you. They can differ, and that difference is the point of two payloads.
    Reset();
    Fire(DeathOf(0x1000, 0xBBBB0000), true, 0x2000, true, true);
    CHECK(g_seen.actorIds.size() == 1 && g_seen.actorIds[0] == 0x1000);
    CHECK(g_seen.playerIds.size() == 1 && g_seen.playerIds[0] == 0x2000);

    // An id that couldn't be read is 0, and the occurrence is still published: a consumer counts
    // deaths without trusting the id, and nothing invents a player death from it.
    Reset();
    Fire(DeathOf(0, 0xCCCC0000), false, 0, true, true);
    CHECK(g_seen.actorIds.size() == 1 && g_seen.actorIds[0] == 0);
    CHECK(g_seen.playerIds.empty());

    // Each event is gated by its own capability only. A build where game.actors.died isn't ready
    // still gets game.player.died, and the other way round: neither view can silence the other.
    Reset();
    Fire(DeathOf(0x1000, 0xDDDD0000), true, 0x2000, false, true);
    CHECK(g_seen.actorIds.empty() && g_seen.playerIds.size() == 1 && g_seen.playerIds[0] == 0x2000);
    Reset();
    Fire(DeathOf(0x1000, 0xEEEE0000), true, 0x2000, true, false);
    CHECK(g_seen.actorIds.size() == 1 && g_seen.actorIds[0] == 0x1000 && g_seen.playerIds.empty());

    // With both capabilities off, nothing is published.
    Reset();
    Fire(DeathOf(0x1000, 0xF0000000), true, 0x2000, false, false);
    CHECK(g_seen.actorIds.empty() && g_seen.playerIds.empty());

    // Several deaths in one tick keep their own ids, in order.
    Reset();
    Fire(DeathOf(0xA, 0x1), false, 0, true, true);
    Fire(DeathOf(0xB, 0x2), true, 0xC, true, true);
    Fire(DeathOf(0xD, 0x3), false, 0, true, true);
    CHECK(g_seen.actorIds.size() == 3 && g_seen.actorIds[0] == 0xA && g_seen.actorIds[1] == 0xB && g_seen.actorIds[2] == 0xD);
    CHECK(g_seen.playerIds.size() == 1 && g_seen.playerIds[0] == 0xC);

    // An older subscriber that only knows game.player.died's payload is unaffected: the event names
    // and the structs it reads are unchanged, so the new event is additive. The new payload is laid
    // out exactly like the one it sits beside, so a consumer that already checks `size` handles both.
    CHECK(std::strcmp(SC_GAME_EVENT_ACTOR_DIED, "game.actors.died") == 0);
    CHECK(std::strcmp(SC_GAME_EVENT_PLAYER_DIED, "game.player.died") == 0);
    static_assert(sizeof(sc_game_actor_died) == sizeof(sc_game_player_died), "death payload prefix");
    static_assert(sizeof(sc_game_player_died) == 24, "death payload size");

    CHECK(sco::Unsubscribe(sco::host::HostOwner(), SC_GAME_EVENT_ACTOR_DIED, OnActorDied) == sco::Result::Ok);
    CHECK(sco::Unsubscribe(sco::host::HostOwner(), SC_GAME_EVENT_PLAYER_DIED, OnPlayerDied) == sco::Result::Ok);

    std::printf("death events tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
