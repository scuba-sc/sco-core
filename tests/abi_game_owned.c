/* Game-only G9 table/receipt pins, C11 and C++20, including Windows x64 compile. */
#include <stddef.h>
#include "sc_owned.h"
#ifdef __cplusplus
#define PIN(x) static_assert(x, #x)
#else
#define PIN(x) _Static_assert(x, #x)
#endif
PIN(sizeof(void*) == 8);
PIN(sizeof(sc_owned_v1) == 56);
PIN(offsetof(sc_owned_v1, size) == 0);
PIN(offsetof(sc_owned_v1, retain_build) == 8);
PIN(offsetof(sc_owned_v1, remove_build) == 16);
PIN(offsetof(sc_owned_v1, remove_npc) == 24);
PIN(offsetof(sc_owned_v1, remove_seat_npc) == 32);
PIN(offsetof(sc_owned_v1, receipt) == 40);
PIN(offsetof(sc_owned_v1, forget) == 48);
PIN(sizeof(sc_removal_receipt) == 32);
PIN(offsetof(sc_removal_receipt, size) == 0);
PIN(offsetof(sc_removal_receipt, state) == 4);
PIN(offsetof(sc_removal_receipt, ticket) == 8);
PIN(offsetof(sc_removal_receipt, entity_id) == 16);
PIN(offsetof(sc_removal_receipt, kind) == 24);
PIN(offsetof(sc_removal_receipt, _pad) == 28);
PIN(SC_OWNED_VERSION_1_0 == 0x00010000u);
PIN(SC_OWNED_MAX_RETAINED == 1024u && SC_OWNED_MAX_RECEIPTS == 1024u);
PIN(SC_REMOVAL_REQUESTED == 1u);
PIN(SC_REMOVAL_BUILD == 1u && SC_REMOVAL_NPC == 2u && SC_REMOVAL_SEAT_NPC == 3u);
void sco_abi_owned_pins(const sc_owned_v1* t);
void sco_abi_owned_pins(const sc_owned_v1* t) {
    sco_result (*retain_build)(sco_plugin*, uint64_t) = t->retain_build;
    sco_result (*remove_build)(sco_plugin*, uint64_t, uint64_t*) = t->remove_build;
    sco_result (*remove_npc)(sco_plugin*, uint64_t, uint64_t*) = t->remove_npc;
    sco_result (*remove_seat_npc)(sco_plugin*, uint64_t, uint32_t, uint64_t, uint64_t*) = t->remove_seat_npc;
    sco_result (*receipt)(sco_plugin*, uint64_t, sc_removal_receipt*) = t->receipt;
    sco_result (*forget)(sco_plugin*, uint64_t) = t->forget;
    (void)retain_build; (void)remove_build; (void)remove_npc;
    (void)remove_seat_npc; (void)receipt; (void)forget;
}
