/* SPDX-License-Identifier: GPL-3.0-only */
#include <sc_audio.h>
#include <stddef.h>
#ifdef __cplusplus
#define PIN(x) static_assert(x, #x)
#else
#define PIN(x) _Static_assert(x, #x)
#endif
PIN(sizeof(sc_audio_v1) == 40);
PIN(offsetof(sc_audio_v1, resolve) == 8);
PIN(offsetof(sc_audio_v1, play) == 16);
PIN(offsetof(sc_audio_v1, stop) == 24);
PIN(offsetof(sc_audio_v1, play_once) == 32);
PIN(SC_AUDIO_VERSION_1_0 == 0x00010000u);
PIN(SC_AUDIO_VERSION_1_1 == 0x00010001u);
PIN(SC_AUDIO_MAX_TRIGGERS == 64u);
PIN(SC_AUDIO_MAX_PLAYBACKS == 64u);
void sco_abi_audio_pins(void);
void sco_abi_audio_pins(void) {
    const sc_audio_v1* t = 0;
    sco_result (*resolve)(sco_plugin*, const char*, uint64_t*) = t->resolve;
    sco_result (*play)(sco_plugin*, uint64_t, uint64_t*) = t->play;
    sco_result (*stop)(sco_plugin*, uint64_t) = t->stop;
    sco_result (*play_once)(sco_plugin*, uint64_t) = t->play_once;
    (void)resolve; (void)play; (void)stop; (void)play_once;
}
