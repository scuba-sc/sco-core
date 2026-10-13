/* GPL-3.0-only: x64 mission ABI pins; synthetic identifiers only. */
#include "sc_missions.h"
#include "sc_economy.h"
#include <stddef.h>
#ifdef __cplusplus
#define PIN static_assert
#else
#define PIN _Static_assert
#endif
PIN(sizeof(sco_api) == 128, "frozen base");
PIN(sizeof(sc_economy_v1) == 32, "economy table");
PIN(sizeof(sc_economy_balance) == 16, "balance record");
PIN(sizeof(sc_economy_request) == 32, "payment request");
PIN(sizeof(sc_economy_receipt) == 56, "receipt");
PIN(sizeof(sc_economy_event) == 96, "transaction event");
PIN(offsetof(sc_economy_v1, receipt) == 24, "receipt pointer");
PIN(sizeof(sc_mission_state) == 4, "enum width");
PIN(sizeof(sc_mission_script) == 152, "script layout");
PIN(offsetof(sc_mission_script, bytes) == 136, "script bytes pointer");
PIN(sizeof(sc_mission_offer) == 48, "offer size");
PIN(offsetof(sc_mission_offer, local_reward) == 32, "reward offset");
PIN(sizeof(sc_mission_status) == 48, "status size");
PIN(offsetof(sc_mission_status, session) == 40, "session offset");
PIN(sizeof(sc_missions_v1) == 64, "mission table size");
PIN(offsetof(sc_missions_v1, offer) == 8, "offer pointer");
PIN(offsetof(sc_missions_v1, status) == 16, "status pointer");
PIN(SC_MISSION_ACTIVE == 1 && SC_MISSION_COMPLETED == 2 && SC_MISSION_ABANDONED == 3, "state values");
