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
/* Frozen 1.0 prefix: read keeps its offset, so the old caller record (size 16) stays valid. */
PIN(offsetof(sc_gamerules_v1, read) == 8, "read offset");
PIN(offsetof(sc_gamerules_v1, read) + sizeof(((sc_gamerules_v1*)0)->read) == 16, "1.0 extent");
PIN(SC_GAMERULES_SERVICE_VERSION == 0x00010000u, "1.0 version constant");
PIN(SC_GAMERULES_VERSION_1_0 == 0x00010000u, "1.0 version");
PIN(SC_GAMERULES_VERSION_1_1 == 0x00010001u, "1.1 version");
/* 1.1 tail. */
PIN(offsetof(sc_gamerules_v1, list_params) == 16, "list_params offset");
PIN(sizeof(sc_gamerules_v1) == 24, "table size");
PIN(sizeof(sc_gamerules_param_info) == 88, "param info size");
PIN(offsetof(sc_gamerules_param_info, type) == 4, "param type offset");
PIN(offsetof(sc_gamerules_param_info, name) == 8, "param name offset");
PIN(offsetof(sc_gamerules_param_info, group) == 56, "param group offset");
PIN(sizeof(((sc_gamerules_param_info*)0)->name) == 48, "param name size");
PIN(sizeof(((sc_gamerules_param_info*)0)->group) == 32, "param group size");
PIN(SC_GAMERULES_PARAM_INT == 1u && SC_GAMERULES_PARAM_FLOAT == 2u, "param types");
PIN(SC_GAMERULES_PARAM_MAX_PAGE == 64u && SC_GAMERULES_PARAM_MAX_STRIDE == 4096u, "param limits");
PIN(sizeof(sc_gamerules_state) == 56, "snapshot size");
PIN(offsetof(sc_gamerules_state, generation) == 8, "generation offset");
PIN(offsetof(sc_gamerules_state, enforce_group_qt) == 16, "enforce offset");
PIN(offsetof(sc_gamerules_state, quantum_travel_allowed) == 20, "qt offset");
PIN(offsetof(sc_gamerules_state, quantum_boost_allowed) == 24, "boost offset");
PIN(offsetof(sc_gamerules_state, travel_target_threshold_angle) == 28, "angle offset");
static sco_result read_ok(sco_plugin* p, sc_gamerules_state* o) { (void)p; (void)o; return SCO_OK; }
static sco_result list_ok(sco_plugin* p, uint32_t off, sc_gamerules_param_info* o, uint32_t stride,
                          uint32_t cap, uint32_t* n, uint32_t* total) {
    (void)p; (void)off; (void)o; (void)stride; (void)cap; *n = 0; *total = 7;
    return SCO_OK;
}
int main(void) {
    sc_gamerules_v1 full = {sizeof(full), 0, read_ok, list_ok};
    sc_gamerules_state st;
    memset(&st, 0, sizeof(st));
    st.size = sizeof(st);
    assert(sc_gamerules_read(&full, NULL, &st) == SCO_OK);
    sc_gamerules_v1 truncated = {offsetof(sc_gamerules_v1, read), 0, read_ok, list_ok};
    assert(sc_gamerules_read(&truncated, NULL, &st) == SCO_UNAVAILABLE);
    assert(sc_gamerules_read(NULL, NULL, &st) == SCO_UNAVAILABLE);
    assert(sc_gamerules_read(&full, NULL, NULL) == SCO_UNAVAILABLE);
    sc_gamerules_v1 nullfn = {sizeof(nullfn), 0, NULL, NULL};
    assert(sc_gamerules_read(&nullfn, NULL, &st) == SCO_UNAVAILABLE);
    /* Old 1.0 caller record: a 16-byte table still reads, and has no list_params. */
    sc_gamerules_v1 legacy = {offsetof(sc_gamerules_v1, read) + sizeof(legacy.read), 0, read_ok,
                              list_ok};
    assert(legacy.size == 16);
    assert(sc_gamerules_read(&legacy, NULL, &st) == SCO_OK);
    uint32_t n = 99, total = 99;
    assert(sc_gamerules_list_params(&legacy, NULL, 0, NULL, 0, 0, &n, &total) == SCO_UNAVAILABLE);
    assert(n == 99 && total == 99);
    /* 1.1 helper: full extent and a non-null member. */
    assert(sc_gamerules_list_params(&full, NULL, 0, NULL, 0, 0, &n, &total) == SCO_OK);
    assert(n == 0 && total == 7);
    sc_gamerules_v1 short11 = {offsetof(sc_gamerules_v1, list_params), 0, read_ok, list_ok};
    assert(sc_gamerules_list_params(&short11, NULL, 0, NULL, 0, 0, &n, &total) == SCO_UNAVAILABLE);
    assert(sc_gamerules_list_params(NULL, NULL, 0, NULL, 0, 0, &n, &total) == SCO_UNAVAILABLE);
    sc_gamerules_v1 nolist = {sizeof(nolist), 0, read_ok, NULL};
    assert(sc_gamerules_list_params(&nolist, NULL, 0, NULL, 0, 0, &n, &total) == SCO_UNAVAILABLE);
    assert(sc_gamerules_read(&nolist, NULL, &st) == SCO_OK);
    return 0;
}
