/* SPDX-License-Identifier: GPL-3.0-only
 * Optional game.audio 1.1. Native C/C++ only; not run in game.
 * Game-thread synchronous operations; no plugin callbacks or engine pointers.
 * All IDs are host-issued session tokens, never persisted or reused. Resolve
 * names exactly (case-sensitive, printable ASCII, 1..127 bytes). Unknown names
 * return NOT_FOUND without starting an effect. A trigger belongs to its caller.
 * Play returns an owner-bound playback token after backend acceptance, not proof
 * of audible output or completion. Stop affects only that playback. No global
 * stop-by-trigger authority. Unload stops the owner's playbacks and expires its
 * triggers. Cleanup failure retains backing state and requires process restart;
 * the host must retain provider code/context. No native backend installed.
 */
#ifndef SC_AUDIO_H
#define SC_AUDIO_H
#include "sco_api.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SC_AUDIO_NAME "game.audio"
#define SC_AUDIO_VERSION_1_0 0x00010000u
#define SC_AUDIO_VERSION_1_1 0x00010001u
#define SC_AUDIO_MAX_NAME 127u
#define SC_AUDIO_MAX_TRIGGERS 64u
#define SC_AUDIO_MAX_PLAYBACKS 64u
typedef struct sc_audio_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Outputs zero on failure. BAD_ARG invalid/released owner, malformed name
     * or null output; WRONG_THREAD; TOO_MANY bounded tokens; UNAVAILABLE no
     * provider/stopped/restart-required; FAILED provider failure or recursion.
     * Resolve repeats reuse the owner's same live trigger token. */
    sco_result (*resolve)(sco_plugin* self, const char* name, uint64_t* out_trigger);
    /* NOT_FOUND stale/wrong-kind token; FAILED foreign owner. Requires a
     * registered unload cleanup hook and a complete backend before any effect.
     * Every play has a distinct token; no allocation crosses the boundary. */
    sco_result (*play)(sco_plugin* self, uint64_t trigger, uint64_t* out_playback);
    /* OK means stop accepted and backing effect released by the backend.
     * Failure retains tracking, latches new operations unavailable; cleanup can
     * retry. Released owners are handled internally, never via this entrypoint.
     * Tables are borrowed; old tables refuse after stop. Check complete member
     * extent and non-null before each call. */
    sco_result (*stop)(sco_plugin* self, uint64_t playback);
    /* v1.1. Fire-and-forget. OK means the backend accepted submission only; this
     * exposes no playback token, guarantees no audible output/completion, and
     * provides no stop operation. If the owner is released during submission,
     * the method may report UNAVAILABLE after an accepted request; it cannot
     * roll back or cancel, and callers must not retry automatically. Gated by
     * game.audio.play_once, independent of game.audio.triggers. Check complete
     * member extent before calling; sc_audio_play_once does this. */
    sco_result (*play_once)(sco_plugin* self, uint64_t trigger);
} sc_audio_v1;
static inline sco_result sc_audio_play_once(const sc_audio_v1* t, sco_plugin* self,
                                            uint64_t trigger) {
    return (t && t->size >= offsetof(sc_audio_v1, play_once) + sizeof(t->play_once) && t->play_once)
               ? t->play_once(self, trigger) : SCO_UNAVAILABLE;
}
#ifdef __cplusplus
}
#endif
#endif
