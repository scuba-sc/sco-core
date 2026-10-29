// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Native one-shot audio: the Wwise entry points `game.audio.play_once` would call, resolved by
// name from the game's own export table. No address is hardcoded, and nothing is installed unless
// a witness shows the audio pump's shared game object is registered when a submission lands. No
// such witness is grounded, so production resolves the exports and then refuses; the capability
// stays OFF. Not run in game.
#include "audio.h"
#include <cstdint>

namespace sco::game::services {

// The exported names the resolver asks for, exactly as StarCitizen.exe's export directory carries
// them (read from the 4.10.197.2878 LIVE client, sha256 ddba788f...4310c2a — the same target the
// seam report pinned). Pinned here so a typo cannot silently disable the backend, and asserted by
// tests/test_audio_engine.cpp.
inline constexpr const char* kAudioPostEventExport =
    "?PostEvent@SoundEngine@AK@@YAII_KIP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXIPEAUAkExternalSourceInfo@@I@Z";
inline constexpr const char* kAudioGetIdFromStringExport = "?GetIDFromString@SoundEngine@AK@@YAIPEBD@Z";
inline constexpr const char* kAudioIsInitializedExport = "?IsInitialized@SoundEngine@AK@@YA_NXZ";

// One exported Wwise function per field. A build without one leaves it null and no backend is
// installed; the service still publishes the 1.1 table with both capabilities OFF.
struct AudioExports {
    void* module = nullptr;
    // unsigned int AK::SoundEngine::PostEvent(unsigned int eventId, unsigned long long gameObject,
    //     unsigned int flags, void (*callback)(AK::AkCallbackType, AK::AkCallbackInfo*),
    //     void* cookie, unsigned int cookieMask, AK::AkExternalSourceInfo* sources,
    //     unsigned int numSources). Nonzero means accepted submission, not audible output.
    uint32_t (*post_event)(uint32_t, uint64_t, uint32_t, void (*)(int32_t, void*), void*, uint32_t,
                           void*, uint32_t) = nullptr;
    // unsigned int AK::SoundEngine::GetIDFromString(const char* name); zero means unknown name.
    uint32_t (*id_from_string)(const char*) = nullptr;
    // bool AK::SoundEngine::IsInitialized(); PostEvent itself also returns zero when it is not.
    uint32_t (*is_initialized)() = nullptr;
    bool Complete() const { return module && post_event && id_from_string && is_initialized; }
};

// The game object the audio pump posts on. The missing evidence is that this value is still
// registered at submission time; an export existing does not prove it, so the field stays null in
// production and InstallNativeAudio refuses.
struct AudioWitness {
    uint64_t (*game_object)(void* ctx) = nullptr;
    void* ctx = nullptr;
    bool Grounded() const { return game_object != nullptr; }
};

// GetProcAddress over the game module, by exported name.
using ExportLookup = void* (*)(void* module, const char* name);

// Fills every field the module exports. A missing export is left null, never guessed.
AudioExports ResolveAudioExports(void* module, ExportLookup lookup);

// Installs the one-shot backend for the process. Refuses, with a static reason, unless the exports
// are complete and the witness is grounded; a refused install leaves no backend and no callout.
Result InstallNativeAudio(const AudioExports& exports, const AudioWitness& witness = {});

// Empty until an install succeeds.
AudioBackend NativeAudioBackend();
// Why the last install refused; null after a successful one.
const char* NativeAudioRefusal();

// Production: SCO_AUDIO_NATIVE=1, then resolve by name from the main module and install. Returns
// the backend to publish, or an empty one so the service still publishes 1.1 with both
// capabilities OFF.
AudioBackend ConfigureProductionAudio();

}  // namespace sco::game::services
