/* Independently compiled C11/C++20 gamerules layout and tail refusal fixture. */
#include <sc_gamerules.h>
#include <stddef.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
#define PIN static_assert
#else
#define PIN _Static_assert
#endif
PIN(sizeof(sc_gamerules_v1) == 16, "table size");
PIN(offsetof(sc_gamerules_v1, read) == 8, "read offset");
PIN(sizeof(sc_gamerules_state) == 56, "snapshot size");
PIN(offsetof(sc_gamerules_state, generation) == 8, "generation offset");
PIN(offsetof(sc_gamerules_state, enforce_group_qt) == 16, "enforce offset");
PIN(offsetof(sc_gamerules_state, quantum_travel_allowed) == 20, "qt offset");
PIN(offsetof(sc_gamerules_state, quantum_boost_allowed) == 24, "boost offset");
PIN(offsetof(sc_gamerules_state, travel_target_threshold_angle) == 28, "angle offset");
static sco_result read_ok(sco_plugin* p, sc_gamerules_state* o) { (void)p; (void)o; return SCO_OK; }
int main(void) {
    sc_gamerules_v1 full = {sizeof(full), 0, read_ok};
    sc_gamerules_state st;
    memset(&st, 0, sizeof(st));
    st.size = sizeof(st);
    assert(sc_gamerules_read(&full, NULL, &st) == SCO_OK);
    sc_gamerules_v1 truncated = {offsetof(sc_gamerules_v1, read), 0, read_ok};
    assert(sc_gamerules_read(&truncated, NULL, &st) == SCO_UNAVAILABLE);
    assert(sc_gamerules_read(NULL, NULL, &st) == SCO_UNAVAILABLE);
    assert(sc_gamerules_read(&full, NULL, NULL) == SCO_UNAVAILABLE);
    sc_gamerules_v1 nullfn = {sizeof(nullfn), 0, NULL};
    assert(sc_gamerules_read(&nullfn, NULL, &st) == SCO_UNAVAILABLE);
    return 0;
}
