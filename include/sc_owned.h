/* sc_owned.h: bounded retained-build and actors-owned removal contracts.
 * Game pack only; frozen sco_api and existing service layouts are unchanged.
 * Game thread only, including receipt/forget. Released or unknown self is BAD_ARG.
 * No raw deletion, ownership adoption, player-vehicle registration or foreign-seat authority.
 * retain_build marks an existing game.entities-owned spawn as a retained placement:
 * it remains movable/removable by its owner and is removed on unload. Legacy entities.keep
 * is refused for a retained placement. This is NOT persistence across reload or a saved ID.
 * Unretained legacy keep still relinquishes all removal authority irreversibly.
 * NPC removal uses game.actors ownership only, never entities or spawn_as ownership.
 * Seat removal checks a fresh occupant against expected_npc and that same actors owner;
 * it never removes/ejects a player, foreign NPC or undecodable occupant.
 * SCO_OK removal means the existing despawn primitive accepted initiation, NOT disappearance.
 * Receipts stay REQUESTED: alive=0 (unstreamed/unavailable/wrong thread) is not completion.
 * No completion detector is proven on the engine. Receipts/retentions are bounded, owner-scoped,
 * cleared on ordinary unload/stop; retained-provider faults require restart (no safe cleanup
 * promise). forget only frees a receipt; it neither cancels removal nor restores ownership.
 * All output IDs are zeroed on failure. receipt requires out->size >= sizeof(*out); undersized
 * records are untouched, sufficient records have their payload cleared even on failure.
 * Clients require size >= offsetof(member)+sizeof(member) AND non-NULL callbacks.
 * Absent/truncated/null prerequisite method: UNAVAILABLE. Ownership mismatch/stale ticket:
 * NOT_FOUND. Changed seat occupant: FAILED. Invalid input: BAD_ARG. Full tracking: TOO_MANY.
 * Underlying despawn/read refusals propagate; no receipt is allocated for a refused removal.
 * No allocations, callbacks or engine pointers cross the ABI; no recursive reentry. */
#ifndef SC_OWNED_H
#define SC_OWNED_H
#include "sco_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SC_OWNED_NAME "game.owned"
#define SC_OWNED_VERSION_1_0 0x00010000u
#define SC_OWNED_MAX_RETAINED 1024u
#define SC_OWNED_MAX_RECEIPTS 1024u
#define SC_REMOVAL_REQUESTED 1u
#define SC_REMOVAL_BUILD 1u
#define SC_REMOVAL_NPC 2u
#define SC_REMOVAL_SEAT_NPC 3u
/* All fields except size are outputs; tickets never repeat within this service process. */
typedef struct sc_removal_receipt {
    uint32_t size;
    uint32_t state;
    uint64_t ticket;
    uint64_t entity_id;
    uint32_t kind;
    uint32_t _pad;
} sc_removal_receipt;
typedef struct sc_owned_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Idempotent mark; only existing game.entities ownership, without relinquishing it. */
    sco_result (*retain_build)(sco_plugin* self, uint64_t entity_id);
    /* Only a retained placement; successful initiation consumes placement authority. */
    sco_result (*remove_build)(sco_plugin* self, uint64_t entity_id, uint64_t* out_ticket);
    /* Only a game.actors spawn_npc still owned by self. */
    sco_result (*remove_npc)(sco_plugin* self, uint64_t npc_id, uint64_t* out_ticket);
    /* Fresh seat index lookup must still name expected_npc, which must be actors-owned. */
    sco_result (*remove_seat_npc)(sco_plugin* self, uint64_t ship, uint32_t seat_index,
                                  uint64_t expected_npc, uint64_t* out_ticket);
    /* Query an owner-scoped initiation receipt; never infers completion from alive. */
    sco_result (*receipt)(sco_plugin* self, uint64_t ticket, sc_removal_receipt* out);
    /* Explicitly forget a receipt, not cancel/release/reclaim an entity. */
    sco_result (*forget)(sco_plugin* self, uint64_t ticket);
} sc_owned_v1;
#ifdef __cplusplus
}
#endif
#endif
