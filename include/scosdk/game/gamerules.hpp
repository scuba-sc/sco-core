// SPDX-License-Identifier: GPL-3.0-only
// scosdk/game/gamerules.hpp: the game pack's service "game.gamerules" (sc_gamerules.h) for C++
// plugins. Header-only, over sco_api.h and sc_gamerules.h.
//
//   sco::sdk::game::GameRules rules;
//   sc_gamerules_state s;
//   if (rules.Open(*this) == SCO_OK && rules.Read(s) == SCO_OK && s.valid) { ... s.quantum_travel_allowed }
//
// Game thread only, like the table. Every call answers sco_result and is noexcept. Read-only: the
// server owns these values, nothing here writes them and nothing writes game memory. Read zeroes
// out and sets its size first, so out stays zero apart from size on any failure. The capability
// game.gamerules.read is OFF by default and the default production backend binds nothing, so on a
// stock product Read answers SCO_UNAVAILABLE without touching the game. The table has no
// last_error member. Reference: docs/game/gamerules.md. Not run in game.
// ListParams separately copies the static 1.1 definitions; it carries no live values and needs no backend.
#ifndef SCOSDK_GAME_GAMERULES_HPP
#define SCOSDK_GAME_GAMERULES_HPP

#include "../plugin.hpp"
#include "sc_gamerules.h"

#include <cstddef>
#include <cstring>
#include <array>
#include <span>

namespace sco::sdk::game {

class GameRules {
public:
    // Finds game.gamerules 1.x. SCO_UNAVAILABLE on a 1.0 sco_api host; SCO_NOT_FOUND when the host
    // has no game pack (or the product didn't start its services). Both leave the GameRules empty.
    sco_result Open(const Plugin& plugin) noexcept { return Open(plugin.Api(), plugin.Self()); }
    // The same for code that holds the C handles (a C-style plugin, a test).
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        t_ = nullptr;
        api_ = nullptr;
        self_ = self;
        if (!api || !self_) return SCO_BAD_ARG;
        if (!Covers(api->size, offsetof(sco_api, query_service)) || !api->query_service) return SCO_UNAVAILABLE;
        const void* table = nullptr;
        const sco_result r = api->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_SERVICE_VERSION, &table);
        if (r != SCO_OK) return r;
        if (!table) return SCO_UNAVAILABLE;
        t_ = static_cast<const sc_gamerules_v1*>(table);
        api_ = api;
        return SCO_OK;
    }
    explicit operator bool() const noexcept { return t_ != nullptr; }
    const sc_gamerules_v1* Table() const noexcept { return t_; }

    // Copies the client's current server GameRules parameter snapshot into out. out is zeroed (size
    // set) first. game.gamerules.read. Check out.valid: 0 means no parameter set was received yet.
    sco_result Read(sc_gamerules_state& out) const noexcept {
        std::memset(&out, 0, sizeof(out));
        out.size = sizeof(out);
        if (!t_ || !Covers(t_->size, offsetof(sc_gamerules_v1, read)) || !t_->read) return SCO_UNAVAILABLE;
        return t_->read(self_, &out);
    }

    // Copied static definitions, not live values. An empty page counts only. Outputs are
    // unchanged on refusal; a successful page initializes each returned record's size.
    sco_result ListParams(uint32_t offset, std::span<sc_gamerules_param_info> page,
                          uint32_t& count, uint32_t& total) const noexcept {
        if (page.size() > SC_GAMERULES_PARAM_MAX_PAGE || &count == &total) return SCO_BAD_ARG;
        if (!t_ || !api_ || !Covers(api_->size, offsetof(sco_api, query_service)) || !api_->query_service)
            return SCO_UNAVAILABLE;
        const void* raw = nullptr;
        const sco_result queried = api_->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_1, &raw);
        if (queried != SCO_OK) return queried;
        const auto* table = static_cast<const sc_gamerules_v1*>(raw);
        if (!table || !Covers(table->size, offsetof(sc_gamerules_v1, list_params)) || !table->list_params)
            return SCO_UNAVAILABLE;
        std::array<sc_gamerules_param_info, SC_GAMERULES_PARAM_MAX_PAGE> copied{};
        for (size_t i = 0; i < page.size(); ++i) copied[i].size = sizeof(copied[i]);
        uint32_t copiedCount = 0, copiedTotal = 0;
        const uint32_t capacity = static_cast<uint32_t>(page.size());
        const sco_result result = table->list_params(self_, offset, capacity ? copied.data() : nullptr,
            sizeof(sc_gamerules_param_info), capacity, &copiedCount, &copiedTotal);
        if (result != SCO_OK) return result;
        if (offset > copiedTotal || copiedCount != (capacity < copiedTotal - offset ? capacity : copiedTotal - offset))
            return SCO_FAILED;
        for (uint32_t i = 0; i < copiedCount; ++i)
            if (copied[i].size != sizeof(copied[i]) || !std::memchr(copied[i].name, 0, sizeof(copied[i].name)) ||
                !std::memchr(copied[i].group, 0, sizeof(copied[i].group))) return SCO_FAILED;
        for (uint32_t i = 0; i < copiedCount; ++i) page[i] = copied[i];
        count = copiedCount;
        total = copiedTotal;
        return SCO_OK;
    }

private:
    const sc_gamerules_v1* t_ = nullptr;
    const sco_api* api_ = nullptr;
    sco_plugin* self_ = nullptr;
};

}  // namespace sco::sdk::game

#endif  // SCOSDK_GAME_GAMERULES_HPP
