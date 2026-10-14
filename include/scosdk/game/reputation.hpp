// scosdk/game/reputation.hpp: the game pack's service "game.reputation" (sc_reputation.h) for C++
// plugins. Header-only, over sco_api.h and sc_reputation.h.
//
//   sco::sdk::game::Reputation rep;
//   sc_reputation_checks c;
//   if (rep.Open(*this) == SCO_OK && rep.Checks(c) == SCO_OK) { ... c.sites, c.intact, c.services }
//   else Warn("reputation: %s", rep.LastError().c_str());
//
// Game thread only, like the table. Every call answers sco_result and is noexcept. Read-only: nothing
// here reads or changes a player's standing, and nothing writes game memory. Capability:
// has("game.reputation.checks"), OFF by default (Checks then answers SCO_UNAVAILABLE without touching
// the game). Reference: docs/game/reputation.md.
// GPL-3.0, like sco-core.
#ifndef SCOSDK_GAME_REPUTATION_HPP
#define SCOSDK_GAME_REPUTATION_HPP

#include "../plugin.hpp"
#include "sc_reputation.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace sco::sdk::game {

class Reputation {
public:
    // Finds game.reputation 1.x. SCO_UNAVAILABLE on a 1.0 sco_api host; SCO_NOT_FOUND when the host
    // has no game pack (or the product didn't start its services). Both leave the Reputation empty.
    sco_result Open(const Plugin& plugin) noexcept { return Open(plugin.Api(), plugin.Self()); }
    // The same for code that holds the C handles (a C-style plugin, a test).
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        t_ = nullptr;
        self_ = self;
        if (!api || !self_) return SCO_BAD_ARG;
        if (!Covers(api->size, offsetof(sco_api, query_service)) || !api->query_service) return SCO_UNAVAILABLE;
        const void* table = nullptr;
        const sco_result r = api->query_service(SC_REPUTATION_SERVICE_NAME, SC_REPUTATION_SERVICE_VERSION, &table);
        if (r != SCO_OK) return r;
        if (!table) return SCO_UNAVAILABLE;
        t_ = static_cast<const sc_reputation_v1*>(table);
        return SCO_OK;
    }
    explicit operator bool() const noexcept { return t_ != nullptr; }
    const sc_reputation_v1* Table() const noexcept { return t_; }

    // Fills out from the game's reputation-service check sites. out is zeroed (size set) first, so
    // it stays zero apart from size on any failure. game.reputation.checks.
    sco_result Checks(sc_reputation_checks& out) const noexcept {
        std::memset(&out, 0, sizeof(out));
        out.size = sizeof(out);
        if (!t_ || !Covers(t_->size, offsetof(sc_reputation_v1, checks)) || !t_->checks) return SCO_UNAVAILABLE;
        return t_->checks(self_, &out);
    }

    // The reason for your last failed call ("" if none or unavailable).
    std::string LastError() const noexcept {
        if (!t_ || !Covers(t_->size, offsetof(sc_reputation_v1, last_error)) || !t_->last_error) return {};
        try {
            std::string s(256, '\0');
            uint32_t size = static_cast<uint32_t>(s.size());
            sco_result r = t_->last_error(self_, s.data(), &size);
            if (r == SCO_TOO_MANY) {
                s.resize(size);
                r = t_->last_error(self_, s.data(), &size);
            }
            if (r != SCO_OK || size == 0) return {};
            s.resize(size - 1);
            return s;
        } catch (...) {
            return {};
        }
    }

private:
    const sc_reputation_v1* t_ = nullptr;
    sco_plugin* self_ = nullptr;
};

}  // namespace sco::sdk::game

#endif  // SCOSDK_GAME_REPUTATION_HPP
