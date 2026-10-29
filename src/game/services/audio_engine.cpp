// SPDX-License-Identifier: GPL-3.0-only
// Native one-shot audio over the game's exported Wwise functions. The exports are grounded (the
// PE export directory carries them); the registration witness the one-shot path needs is not, so
// production resolves the exports and refuses to install. Both capabilities stay OFF. Not run in
// game.
#include "audio_engine.h"
#include "sco/host.h"
#include "sco/log.h"
#include <windows.h>

namespace sco::game::services {
namespace {

struct State {
    AudioExports exports;
    AudioWitness witness;
    bool installed = false;
    const char* refusal = nullptr;
} g;

// Exported names exactly as the module's export directory carries them; the header pins them.
constexpr const char* kPostEvent = kAudioPostEventExport;
constexpr const char* kGetIdFromString = kAudioGetIdFromStringExport;
constexpr const char* kIsInitialized = kAudioIsInitializedExport;

// Native calls, SEH-wrapped. 1 ran, 0 refused or absent, -1 faulted. No C++ object with a
// destructor lives in these frames, so a fault unwinds nothing the caller owns.
int CallPostEvent(uint32_t event_id, uint64_t object) {
    __try {
        const auto fn = g.exports.post_event;
        if (!fn) return 0;
        return fn(event_id, object, 0, nullptr, nullptr, 0, nullptr, 0) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int CallIdFromString(const char* name, uint32_t* id) {
    *id = 0;
    __try {
        const auto fn = g.exports.id_from_string;
        if (!fn) return 0;
        *id = fn(name);
        return *id ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *id = 0;
        return -1;
    }
}

int CallIsInitialized() {
    __try {
        const auto fn = g.exports.is_initialized;
        return fn && fn() ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int CallGameObject(uint64_t* object) {
    *object = 0;
    __try {
        if (!g.witness.game_object) return 0;
        *object = g.witness.game_object(g.witness.ctx);
        return *object ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *object = 0;
        return -1;
    }
}

sco_result Resolve(void*, const char* name, uint64_t* out) {
    uint32_t id = 0;
    const int got = CallIdFromString(name, &id);
    if (got < 0) return SCO_CRASHED;
    if (!got) return SCO_NOT_FOUND;
    *out = id;
    return SCO_OK;
}

sco_result PlayOnce(void*, uint64_t key) {
    // The engine must be up and the pump's object still registered before anything is posted.
    const int live = CallIsInitialized();
    if (live < 0) return SCO_CRASHED;
    if (!live) return SCO_UNAVAILABLE;
    uint64_t object = 0;
    const int registered = CallGameObject(&object);
    if (registered < 0) return SCO_CRASHED;
    if (!registered) return SCO_UNAVAILABLE;
    const int posted = CallPostEvent(static_cast<uint32_t>(key), object);
    if (posted < 0) return SCO_CRASHED;
    return posted ? SCO_OK : SCO_FAILED;
}

bool Enabled(const char* name) {
    char value[8]{};
    return GetEnvironmentVariableA(name, value, sizeof(value)) == 1 && value[0] == '1';
}

void* ExportByName(void* module, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
}

}  // namespace

AudioExports ResolveAudioExports(void* module, ExportLookup lookup) {
    AudioExports exports;
    exports.module = module;
    if (!module || !lookup) return exports;
    exports.post_event = reinterpret_cast<decltype(exports.post_event)>(lookup(module, kPostEvent));
    exports.id_from_string =
        reinterpret_cast<decltype(exports.id_from_string)>(lookup(module, kGetIdFromString));
    exports.is_initialized =
        reinterpret_cast<decltype(exports.is_initialized)>(lookup(module, kIsInitialized));
    return exports;
}

Result InstallNativeAudio(const AudioExports& exports, const AudioWitness& witness) {
    if (!OnGameThread()) return Result::WrongThread;
    g = {};
    if (!exports.Complete()) {
        g.refusal = "the game module does not export every Wwise one-shot entry point";
        return Result::Failed;
    }
    if (!witness.Grounded()) {
        g.refusal = "no grounded witness that the audio pump's game object is still registered";
        return Result::Failed;
    }
    g.exports = exports;
    g.witness = witness;
    g.installed = true;
    return Result::Ok;
}

AudioBackend NativeAudioBackend() {
    AudioBackend backend;
    if (!g.installed) return backend;
    backend.ctx = &g;
    backend.resolve = Resolve;
    backend.play_once = PlayOnce;
    return backend;
}

const char* NativeAudioRefusal() { return g.refusal; }

AudioBackend ConfigureProductionAudio() {
    if (!OnGameThread()) return {};
    if (!Enabled("SCO_AUDIO_NATIVE")) return {};
    const AudioExports exports = ResolveAudioExports(GetModuleHandleA(nullptr), ExportByName);
    if (!exports.Complete()) {
        Log("[game] audio: SCO_AUDIO_NATIVE=1 but the module does not export the Wwise one-shot "
            "entry points; no native backend");
        return {};
    }
    // The exports say the functions exist. They say nothing about whether the pump's game object is
    // registered when a submission lands, so install refuses and the capability stays OFF.
    if (InstallNativeAudio(exports, {}) != Result::Ok) {
        Log("[game] audio: native one-shot backend refused: %s; game.audio.play_once stays OFF",
            NativeAudioRefusal());
        return {};
    }
    return NativeAudioBackend();
}

}  // namespace sco::game::services
