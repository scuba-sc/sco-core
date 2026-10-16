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
#ifndef SCOSDK_GAME_GAMERULES_HPP
#define SCOSDK_GAME_GAMERULES_HPP

#include "../plugin.hpp"
#include "sc_gamerules.h"

#include <cstddef>
#include <cstring>

namespace sco::sdk::game {

class GameRules {
public:
    // Finds game.gamerules 1.x. SCO_UNAVAILABLE on a 1.0 sco_api host; SCO_NOT_FOUND when the host
    // has no game pack (or the product didn't start its services). Both leave the GameRules empty.
    sco_result Open(const Plugin& plugin) noexcept { return Open(plugin.Api(), plugin.Self()); }
    // The same for code that holds the C handles (a C-style plugin, a test).
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        t_ = nullptr;
        self_ = self;
        if (!api || !self_) return SCO_BAD_ARG;
        if (!Covers(api->size, offsetof(sco_api, query_service)) || !api->query_service) return SCO_UNAVAILABLE;
        const void* table = nullptr;
        const sco_result r = api->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_SERVICE_VERSION, &table);
        if (r != SCO_OK) return r;
        if (!table) return SCO_UNAVAILABLE;
        t_ = static_cast<const sc_gamerules_v1*>(table);
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

private:
    const sc_gamerules_v1* t_ = nullptr;
    sco_plugin* self_ = nullptr;
};

}  // namespace sco::sdk::game

#endif  // SCOSDK_GAME_GAMERULES_HPP
