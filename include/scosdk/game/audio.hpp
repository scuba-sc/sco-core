// SPDX-License-Identifier: GPL-3.0-only
#ifndef SCOSDK_GAME_AUDIO_HPP
#define SCOSDK_GAME_AUDIO_HPP
#include "../plugin.hpp"
#include <sc_audio.h>
namespace sco::sdk::game {
class Audio {
public:
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        table_ = nullptr; self_ = self;
        if (!api || !self) return SCO_BAD_ARG;
        if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) || !api->query_service)
            return SCO_UNAVAILABLE;
        const void* out = nullptr;
        const auto r = api->query_service(SC_AUDIO_NAME, SC_AUDIO_VERSION_1_0, &out);
        if (r != SCO_OK) return r;
        table_ = static_cast<const sc_audio_v1*>(out);
        return table_ ? SCO_OK : SCO_UNAVAILABLE;
    }
    sco_result Open(const Plugin& p) noexcept { return Open(p.Api(), p.Self()); }
    sco_result Resolve(const char* name, uint64_t& trigger) const noexcept {
        trigger = 0;
        if (!table_ || table_->size < offsetof(sc_audio_v1, resolve) + sizeof(table_->resolve) || !table_->resolve)
            return SCO_UNAVAILABLE;
        return table_->resolve(self_, name, &trigger);
    }
    sco_result Play(uint64_t trigger, uint64_t& playback) const noexcept {
        playback = 0;
        if (!table_ || table_->size < offsetof(sc_audio_v1, play) + sizeof(table_->play) || !table_->play)
            return SCO_UNAVAILABLE;
        return table_->play(self_, trigger, &playback);
    }
    sco_result Stop(uint64_t playback) const noexcept {
        if (!table_ || table_->size < offsetof(sc_audio_v1, stop) + sizeof(table_->stop) || !table_->stop)
            return SCO_UNAVAILABLE;
        return table_->stop(self_, playback);
    }
private:
    const sc_audio_v1* table_ = nullptr;
    sco_plugin* self_ = nullptr;
};
}
#endif
