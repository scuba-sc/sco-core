/* GPL-3.0-only. Optional diagnostic ingress, NOT a runtime observation endpoint.
 * sco.diagnostics.publisher 1.0: host-owned, absent by default. A trusted host
 * enables it only after positive offline admission and selects exactly one
 * plugin ID. Submissions update diagnostic memory only, never gameplay/state,
 * commands/settings/services. No reads, callbacks, paths, arbitrary JSON, text,
 * identifiers, timestamps, engine values or transport are accepted.
 *
 * submit: game-thread only; caller must be the selected live sco_plugin handle.
 * Observation is copied synchronously, no pointer retained. Host supplies time.
 * One accepted observation/second; excess returns TOO_MANY without overwrite.
 * BAD_ARG: invalid/released/unselected handle, null/short record or unknown health.
 * UNAVAILABLE: stopped or terminal shutdown. WRONG_THREAD off game thread.
 * No exceptions cross ABI; allocation failures return TOO_MANY.
 * The host stores at most one advisory record. Unload/quarantine makes it absent;
 * Stop clears it. Borrowed table is process-lifetime, cached calls while stopped
 * refuse. Service reopen starts empty. UNKNOWN is not a successful health claim.
 *
 * This versioned source contract does NOT change the six closed runtime JSON v1
 * schemas. Extra output requires separately reviewed versioned endpoint schemas.
 * Authoritative host fault/readiness flags are never derived from advisory health.
 * No C#/Lua binding. Full native privileges apply; not a sandbox/covert-channel
 * guarantee. Runtime clients still need independent endpoint admission/consent.
 */
#ifndef SCO_DIAGNOSTICS_PUBLISHER_H
#define SCO_DIAGNOSTICS_PUBLISHER_H
#include "sco_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SCO_DIAGNOSTICS_PUBLISHER_NAME "sco.diagnostics.publisher"
#define SCO_DIAGNOSTICS_PUBLISHER_VERSION_1_0 0x00010000u
typedef enum sco_diagnostics_health {
    SCO_DIAGNOSTICS_HEALTH_UNKNOWN = 0,
    SCO_DIAGNOSTICS_HEALTH_READY = 1,
    SCO_DIAGNOSTICS_HEALTH_DEGRADED = 2,
    SCO_DIAGNOSTICS_HEALTH_FORCE32 = 0x7fffffff
} sco_diagnostics_health;
typedef struct sco_diagnostics_observation_v1 {
    uint32_t size;
    sco_diagnostics_health health;
} sco_diagnostics_observation_v1;
typedef struct sco_diagnostics_publisher_v1 {
    uint32_t size;
    uint32_t _pad;
    sco_result (*submit)(sco_plugin* self, const sco_diagnostics_observation_v1* observation);
} sco_diagnostics_publisher_v1;
#ifdef __cplusplus
}
#endif
#endif
