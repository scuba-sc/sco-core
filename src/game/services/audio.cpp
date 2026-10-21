// SPDX-License-Identifier: GPL-3.0-only
#include "audio.h"
#include "spawn.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
#include <cstring>
#include <limits>
namespace sco::game::services {
namespace {
AudioBackend backend;
bool started = false, busy = false, failed = false, hooked = false;
// Set by a native play_once fault; never cleared, so one-shot audio stays off until
// process restart even across StopAudio/StartAudio.
bool once_failed = false;
uint64_t next = 1;
struct Trigger { const void* owner = nullptr; uint64_t token = 0, key = 0; char name[128]{}; };
struct Playback { const void* owner = nullptr; uint64_t token = 0, key = 0; };
Trigger triggers[SC_AUDIO_MAX_TRIGGERS];
Playback playbacks[SC_AUDIO_MAX_PLAYBACKS];
bool Live(const sco_plugin* self) { return self && host::PluginId(self) && !detail::Released(self); }
bool Complete() { return backend.resolve && backend.play && backend.stop; }
sco_result SafeAdmission(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!Live(self)) return SCO_BAD_ARG;
    if (busy) return SCO_FAILED;
    if (!started || detail::RuntimeStopping() || !hooked)
        return SCO_UNAVAILABLE;
    return SCO_OK;
}
sco_result Admission(sco_plugin* self) {
    const auto safe = SafeAdmission(self);
    if (safe != SCO_OK) return safe;
    if (failed || !Complete() || !caps::Has("game.audio.triggers")) return SCO_UNAVAILABLE;
    return SCO_OK;
}
sco_result OnceAdmission(sco_plugin* self) {
    const auto safe = SafeAdmission(self);
    if (safe != SCO_OK) return safe;
    if (failed || once_failed || !backend.resolve || !backend.play_once ||
        !caps::Has("game.audio.play_once"))
        return SCO_UNAVAILABLE;
    return SCO_OK;
}
sco_result ResolutionModeAdmission() {
    if (failed) return SCO_UNAVAILABLE;
    const bool legacy = Complete() && caps::Has("game.audio.triggers");
    const bool once = !once_failed && backend.resolve && backend.play_once &&
                      caps::Has("game.audio.play_once");
    return (legacy || once) ? SCO_OK : SCO_UNAVAILABLE;
}
sco_result ResolveAdmission(sco_plugin* self) {
    const auto safe = SafeAdmission(self);
    if (safe != SCO_OK) return safe;
    return ResolutionModeAdmission();
}
void Latch() {
    failed = true;
    caps::Set("game.audio.triggers", false, "audio cleanup/provider fault; restart required");
}
// May run after an owner has been marked released, including from a backend
// callout. A busy release is drained immediately after that callout returns.
void Cleanup(const void* owner, bool all = false) {
    if (busy) return;
    busy = true;
    for (auto& p : playbacks) {
        if (!p.token || (!all && p.owner != owner)) continue;
        const auto r = backend.stop ? backend.stop(backend.ctx, p.key) : SCO_UNAVAILABLE;
        if (r == SCO_OK) p = {};
        else Latch();
    }
    for (auto& t : triggers) if (t.token && (all || t.owner == owner)) t = {};
    busy = false;
}
void DrainReleased() {
    for (const auto& p : playbacks)
        if (p.token && detail::Released(p.owner)) Cleanup(p.owner);
    for (auto& t : triggers) if (t.token && detail::Released(t.owner)) t = {};
}
void OwnerReleased(const void* owner) { Cleanup(owner); }
sco_result Resolve(sco_plugin* self, const char* name, uint64_t* out) {
    if (!out) return SCO_BAD_ARG;
    *out = 0;
    auto r = ResolveAdmission(self); if (r != SCO_OK) return r;
    if (!name) return SCO_BAD_ARG;
    const size_t n = strnlen(name, SC_AUDIO_MAX_NAME + 1);
    if (!n || n > SC_AUDIO_MAX_NAME) return SCO_BAD_ARG;
    for (size_t i = 0; i < n; ++i)
        if (static_cast<unsigned char>(name[i]) < 0x21 || static_cast<unsigned char>(name[i]) > 0x7e)
            return SCO_BAD_ARG;
    Trigger* free = nullptr;
    for (auto& t : triggers) {
        if (t.owner == self && t.token && std::strcmp(t.name, name) == 0) { *out = t.token; return SCO_OK; }
        if (!t.token && !free) free = &t;
    }
    if (!free || next == std::numeric_limits<uint64_t>::max()) return SCO_TOO_MANY;
    char copy[128]{}; std::memcpy(copy, name, n);
    uint64_t key = 0;
    busy = true; r = backend.resolve(backend.ctx, copy, &key); busy = false;
    DrainReleased();
    if (!Live(self) || detail::RuntimeStopping() || ResolutionModeAdmission() != SCO_OK)
        return SCO_UNAVAILABLE;
    if (r != SCO_OK) return r;
    if (!key) return SCO_FAILED;
    free->owner = self; free->token = next++; free->key = key;
    std::memcpy(free->name, copy, sizeof(copy)); *out = free->token;
    return SCO_OK;
}
sco_result Play(sco_plugin* self, uint64_t token, uint64_t* out) {
    if (!out) return SCO_BAD_ARG;
    *out = 0;
    auto r = Admission(self); if (r != SCO_OK) return r;
    Trigger* trigger = nullptr;
    for (auto& t : triggers) if (t.token && t.token == token) trigger = &t;
    if (!trigger) return SCO_NOT_FOUND;
    if (trigger->owner != self) return SCO_FAILED;
    Playback* free = nullptr;
    for (auto& p : playbacks) if (!p.token) { free = &p; break; }
    if (!free || next == std::numeric_limits<uint64_t>::max()) return SCO_TOO_MANY;
    // Reserve before effect creation, including reentrant owner release.
    free->owner = self; free->token = next++;
    busy = true; r = backend.play(backend.ctx, trigger->key, &free->key); busy = false;
    if (r != SCO_OK || !free->key) {
        if (free->key) Latch(); // violated failure-atomic backend; retain cleanup tracking
        else *free = {};
        DrainReleased(); return r == SCO_OK ? SCO_FAILED : r;
    }
    DrainReleased();
    if (!Live(self) || failed || detail::RuntimeStopping() || !caps::Has("game.audio.triggers")) {
        Cleanup(self); return SCO_UNAVAILABLE;
    }
    *out = free->token; return SCO_OK;
}
sco_result PlayOnce(sco_plugin* self, uint64_t token) {
    auto r = OnceAdmission(self); if (r != SCO_OK) return r;
    Trigger* trigger = nullptr;
    for (auto& t : triggers) if (t.token && t.token == token) trigger = &t;
    if (!trigger) return SCO_NOT_FOUND;
    if (trigger->owner != self) return SCO_FAILED;
    const uint64_t key = trigger->key;
    busy = true; r = backend.play_once(backend.ctx, key); busy = false;
    if (r == SCO_CRASHED) {
        once_failed = true;
        caps::Set("game.audio.play_once", false, "native audio fault; restart required");
        return SCO_CRASHED;
    }
    DrainReleased();
    if (!Live(self) || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    return r;
}
sco_result Stop(sco_plugin* self, uint64_t token) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!Live(self)) return SCO_BAD_ARG;
    if (busy) return SCO_FAILED;
    Playback* found = nullptr;
    for (auto& p : playbacks) if (p.token && p.token == token) found = &p;
    if (!found) return SCO_NOT_FOUND;
    if (found->owner != self) return SCO_FAILED;
    busy = true;
    const auto r = backend.stop(backend.ctx, found->key);
    if (r == SCO_OK) *found = {}; else Latch();
    busy = false; DrainReleased(); return r;
}
const sc_audio_v1 table{sizeof(table), 0, Resolve, Play, Stop, PlayOnce};
}
Result StartAudio(const AudioBackend& value) {
    if (!OnGameThread()) return Result::WrongThread;
    if (started || busy || failed) return Result::Failed;
    if ((value.ctx || value.resolve || value.play || value.stop || value.play_once) &&
        (!value.resolve || (!(value.play && value.stop) && !value.play_once))) return Result::BadArg;
    auto r = AddGameReleaseHook(OwnerReleased);
    if (r != Result::Ok) return r;
    hooked = true;
    r = host::ProvideGameService(SC_AUDIO_NAME, SC_AUDIO_VERSION_1_1, &table);
    if (r != Result::Ok) { RemoveGameReleaseHook(OwnerReleased); hooked = false; return r; }
    backend = value; started = true;
    caps::Set("game.audio.play_once", false, once_failed
        ? "native audio fault; restart required"
        : "default OFF; native registration witness ungrounded; not run in game");
    caps::Set("game.audio.triggers", Complete(), "audio requires a host-owned isolated effect provider");
    return Result::Ok;
}
Result StopAudio() {
    if (!OnGameThread()) return Result::WrongThread;
    if (busy) return Result::Failed;
    if (!started) return failed ? Result::Failed : Result::Ok;
    host::WithdrawGameService(SC_AUDIO_NAME);
    caps::Set("game.audio.triggers", false, "audio stopped");
    caps::Set("game.audio.play_once", false, "audio stopped");
    Cleanup(nullptr, true);
    if (failed) return Result::Failed; // backing context and release hook remain for recovery
    RemoveGameReleaseHook(OwnerReleased); hooked = false;
    backend = {}; started = false;
    return Result::Ok;
}
} // namespace sco::game::services
