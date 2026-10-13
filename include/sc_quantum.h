/* Offline bounded quantum controls. No game pointers, arbitrary actions or effects cross this ABI.
 * Table lifetime: game session; all methods game thread only. See docs/quantum.md. */
#ifndef SC_QUANTUM_H
#define SC_QUANTUM_H
#include "sco_api.h"
#include <stddef.h>
#define SC_QUANTUM_SERVICE_NAME "game.quantum"
#define SC_QUANTUM_SERVICE_VERSION 0x00010000u
#define SC_QUANTUM_MAX_HOLD_MS 1000u
#define SC_QUANTUM_PROGRESS_MAX 1000000u
#define SC_QUANTUM_IDLE 0u
#define SC_QUANTUM_CHARGING 1u
#define SC_QUANTUM_BOOSTING 2u
#define SC_QUANTUM_BLOCK_NONE 0u
#define SC_QUANTUM_BLOCK_UNAVAILABLE 1u
#define SC_QUANTUM_BLOCK_OFFLINE 2u
#define SC_QUANTUM_BLOCK_DRIVE 3u
#define SC_QUANTUM_BLOCK_ENGINE 4u
#define SC_QUANTUM_BLOCK_RESTART 5u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sc_quantum_state {
    uint32_t size;
    uint32_t enabled;
    uint32_t held;
    uint32_t phase;
    uint32_t block;
    uint32_t progress; /* integer millionths, 0..SC_QUANTUM_PROGRESS_MAX */
    uint64_t session; /* opaque generation, not a player/ship identifier */
} sc_quantum_state;
typedef struct sc_quantum_v1 {
    uint32_t size;
    uint32_t _pad;
    sco_result (*enable)(sco_plugin* self, uint32_t on);
    sco_result (*hold)(sco_plugin* self, uint32_t milliseconds);
    sco_result (*release)(sco_plugin* self);
    sco_result (*state)(sco_plugin* self, sc_quantum_state* out);
} sc_quantum_v1;
/* Optional safe adapters: require complete pointer extent before reading a callback. */
#define SC_QUANTUM_HAS(t, member) ((t) && (t)->size >= offsetof(sc_quantum_v1, member) + sizeof((t)->member) && (t)->member)
static inline sco_result sc_quantum_enable(const sc_quantum_v1* t, sco_plugin* self, uint32_t on) {
    return SC_QUANTUM_HAS(t, enable) ? t->enable(self, on) : SCO_UNAVAILABLE;
}
static inline sco_result sc_quantum_hold(const sc_quantum_v1* t, sco_plugin* self, uint32_t ms) {
    return SC_QUANTUM_HAS(t, hold) ? t->hold(self, ms) : SCO_UNAVAILABLE;
}
static inline sco_result sc_quantum_release(const sc_quantum_v1* t, sco_plugin* self) {
    return SC_QUANTUM_HAS(t, release) ? t->release(self) : SCO_UNAVAILABLE;
}
static inline sco_result sc_quantum_read_state(const sc_quantum_v1* t, sco_plugin* self, sc_quantum_state* out) {
    return SC_QUANTUM_HAS(t, state) ? t->state(self, out) : SCO_UNAVAILABLE;
}
#undef SC_QUANTUM_HAS
#ifdef __cplusplus
}
#endif
#endif
