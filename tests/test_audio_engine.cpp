// SPDX-License-Identifier: GPL-3.0-only
// Native one-shot audio over a fake export table: the pinned export names, the refusal when no
// registration witness exists, and the callout mapping once a witness is supplied. No real game,
// no native audio, no audible output. Not run in game.
#include "../src/game/services/audio.h"
#include "../src/game/services/audio_engine.h"
#include <scosdk/game/audio.hpp>
#include <sco/caps.h>
#include <sco/host.h>
#include <sco/runtime.h>
#include <cstdio>
#include <cstring>
namespace E = sco::game::services;
namespace {
int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

struct Fake {
    unsigned posts = 0, ids = 0, inits = 0, lookups = 0;
    uint32_t post_returns = 1;
    bool post_faults = false;
    uint32_t initialized = 1;
    uint64_t object = 0xfffffffeull;
    uint32_t last_event = 0;
    uint64_t last_object = 0;
    bool args_clean = false;
    bool names_exact = true;
};
Fake f;

uint32_t PostEvent(uint32_t event_id, uint64_t object, uint32_t flags,
                   void (*callback)(int32_t, void*), void* cookie, uint32_t cookie_mask, void* sources,
                   uint32_t num_sources) {
    ++f.posts;
    f.last_event = event_id;
    f.last_object = object;
    f.args_clean = flags == 0 && callback == nullptr && cookie == nullptr && cookie_mask == 0 &&
                   sources == nullptr && num_sources == 0;
    if (f.post_faults) *static_cast<volatile int*>(nullptr) = 1;
    return f.post_returns;
}
uint32_t GetIdFromString(const char* name) {
    ++f.ids;
    return std::strcmp(name, "cue") == 0 ? 4242u : 0u;
}
uint32_t IsInitialized() {
    ++f.inits;
    return f.initialized;
}
uint64_t GameObject(void*) { return f.object; }

void* Lookup(void* module, const char* name) {
    ++f.lookups;
    if (!module) return nullptr;
    if (!std::strcmp(name, E::kAudioPostEventExport)) return reinterpret_cast<void*>(&PostEvent);
    if (!std::strcmp(name, E::kAudioGetIdFromStringExport)) return reinterpret_cast<void*>(&GetIdFromString);
    if (!std::strcmp(name, E::kAudioIsInitializedExport)) return reinterpret_cast<void*>(&IsInitialized);
    f.names_exact = false;
    return nullptr;
}
// A module that exports everything except PostEvent: the whole backend must refuse.
void* LookupNoPostEvent(void* module, const char* name) {
    if (!module) return nullptr;
    if (!std::strcmp(name, E::kAudioPostEventExport)) return nullptr;
    return Lookup(module, name);
}
}  // namespace

int main() {
    sco::SetGameThread();
    void* module = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1000));

    // The three names are the ones the client's export directory carries; anything else is a typo.
    const E::AudioExports exports = E::ResolveAudioExports(module, Lookup);
    CHECK(f.names_exact && f.lookups == 3);
    CHECK(exports.Complete() && exports.module == module);

    // A module without PostEvent installs nothing, and says which part is missing.
    const E::AudioExports partial = E::ResolveAudioExports(module, LookupNoPostEvent);
    CHECK(!partial.Complete() && partial.id_from_string && partial.is_initialized);
    CHECK(E::InstallNativeAudio(partial, {GameObject, &f}) == sco::Result::Failed);
    CHECK(std::strstr(E::NativeAudioRefusal(), "does not export") != nullptr);
    CHECK(!E::NativeAudioBackend().resolve && !E::NativeAudioBackend().play_once);

    // Exports alone are not the witness: without it the backend refuses and stays uninstalled.
    CHECK(E::InstallNativeAudio(exports, {}) == sco::Result::Failed);
    CHECK(std::strstr(E::NativeAudioRefusal(), "still registered") != nullptr);
    CHECK(!E::NativeAudioBackend().resolve && !E::NativeAudioBackend().play_once);

    // With a witness the backend installs and drives the pinned exports.
    CHECK(E::InstallNativeAudio(exports, {GameObject, &f}) == sco::Result::Ok);
    CHECK(E::NativeAudioRefusal() == nullptr);
    const E::AudioBackend backend = E::NativeAudioBackend();
    CHECK(backend.ctx && backend.resolve && backend.play_once && !backend.play && !backend.stop);

    // The service publishes 1.1 with both capabilities OFF even when a backend is installed.
    CHECK(E::StartAudio(backend) == sco::Result::Ok);
    const auto* api = sco::host::BuildApi({"audio engine fixture"});
    auto* plugin = sco::host::NewPlugin("audio_engine");
    sco::sdk::game::Audio audio;
    CHECK(audio.Open(api, plugin) == SCO_OK);
    uint64_t token = 0;
    CHECK(audio.Resolve("cue", token) == SCO_UNAVAILABLE && token == 0 && f.ids == 0);
    CHECK(!sco::caps::Has("game.audio.triggers") && !sco::caps::Has("game.audio.play_once"));

    sco::caps::Set("game.audio.play_once", true, "native backend fixture");
    CHECK(audio.Resolve("cue", token) == SCO_OK && token != 0 && f.ids == 1);
    CHECK(audio.Resolve("not-a-cue", token) == SCO_NOT_FOUND && f.ids == 2);

    // Accepted submission: event id from GetIDFromString, the witness object, no other arguments.
    CHECK(audio.Resolve("cue", token) == SCO_OK);
    CHECK(audio.PlayOnce(token) == SCO_OK && f.posts == 1);
    CHECK(f.last_event == 4242u && f.last_object == 0xfffffffeull && f.args_clean);

    // Not initialized, or the object is no longer there: refuse before posting anything.
    f.initialized = 0;
    CHECK(audio.PlayOnce(token) == SCO_UNAVAILABLE && f.posts == 1 && f.inits == 2);
    f.initialized = 1;
    f.object = 0;
    CHECK(audio.PlayOnce(token) == SCO_UNAVAILABLE && f.posts == 1);
    f.object = 0xfffffffeull;

    // A rejected submission is FAILED, not accepted.
    f.post_returns = 0;
    CHECK(audio.PlayOnce(token) == SCO_FAILED && f.posts == 2);
    f.post_returns = 1;

    // A native fault maps to CRASHED and latches the capability off until restart.
    f.post_faults = true;
    CHECK(audio.PlayOnce(token) == SCO_CRASHED && f.posts == 3);
    CHECK(!sco::caps::Has("game.audio.play_once"));
    f.post_faults = false;
    CHECK(audio.PlayOnce(token) == SCO_UNAVAILABLE && f.posts == 3);

    CHECK(E::StopAudio() == sco::Result::Ok);
    CHECK(!sco::caps::Has("game.audio.play_once") && !sco::caps::Has("game.audio.triggers"));
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
