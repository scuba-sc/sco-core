/* Independently compiled C11/C++20 quantum layout and tail refusal fixture. */
#include <sc_quantum.h>
#include <stddef.h>
/* Keep tail-refusal calls active in optimized/Release fixture builds. */
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
PIN(sizeof(sc_quantum_v1) == 40, "table size");
PIN(offsetof(sc_quantum_v1, enable) == 8, "enable offset");
PIN(offsetof(sc_quantum_v1, hold) == 16, "hold offset");
PIN(offsetof(sc_quantum_v1, release) == 24, "release offset");
PIN(offsetof(sc_quantum_v1, state) == 32, "state offset");
PIN(sizeof(sc_quantum_state) == 32, "snapshot size");
PIN(offsetof(sc_quantum_state, session) == 24, "session offset");
static sco_result enable_ok(sco_plugin* p, uint32_t n) { (void)p; (void)n; return SCO_OK; }
static sco_result release_ok(sco_plugin* p) { (void)p; return SCO_OK; }
static sco_result state_ok(sco_plugin* p, sc_quantum_state* o) { (void)p; (void)o; return SCO_OK; }
int main(void) {
    sc_quantum_v1 full = {sizeof(full), 0, enable_ok, enable_ok, release_ok, state_ok};
    assert(sc_quantum_enable(&full, NULL, 1) == SCO_OK);
    assert(sc_quantum_hold(&full, NULL, 1) == SCO_OK);
    assert(sc_quantum_release(&full, NULL) == SCO_OK);
    assert(sc_quantum_read_state(&full, NULL, NULL) == SCO_OK);
    /* Allocation really ends at the advertised extent; ASan sees a partial pointer. */
    for (uint32_t n = 4; n < sizeof(full); ++n) {
        void* memory = malloc(n); assert(memory);
        memcpy(memory, &full, n); memcpy(memory, &n, sizeof(n));
        const sc_quantum_v1* t = (const sc_quantum_v1*)memory;
        assert(sc_quantum_enable(t, NULL, 1) == (n >= 16 ? SCO_OK : SCO_UNAVAILABLE));
        assert(sc_quantum_hold(t, NULL, 1) == (n >= 24 ? SCO_OK : SCO_UNAVAILABLE));
        assert(sc_quantum_release(t, NULL) == (n >= 32 ? SCO_OK : SCO_UNAVAILABLE));
        assert(sc_quantum_read_state(t, NULL, NULL) == SCO_UNAVAILABLE);
        free(memory);
    }
    full.enable = NULL; full.hold = NULL; full.release = NULL; full.state = NULL;
    assert(sc_quantum_enable(&full, NULL, 1) == SCO_UNAVAILABLE);
    assert(sc_quantum_hold(&full, NULL, 1) == SCO_UNAVAILABLE);
    assert(sc_quantum_release(&full, NULL) == SCO_UNAVAILABLE);
    assert(sc_quantum_read_state(&full, NULL, NULL) == SCO_UNAVAILABLE);
    assert(sc_quantum_enable(NULL, NULL, 1) == SCO_UNAVAILABLE);
    return 0;
}
