// scosdk/game/hangar.hpp: the game pack's service "game.hangar" (sc_hangar.h) for C++ plugins.
// Header-only, over sco_api.h and sc_hangar.h.
//
//   sco::sdk::game::Hangar hangar;
//   if (hangar.Open(*this) == SCO_OK && Has("game.hangar.lift")) {
//       uint64_t lift = 0;
//       if (hangar.LiftOfLandingArea(landingArea, lift) == SCO_OK)
//           hangar.LiftRequest(lift, SC_HANGAR_LIFT_OPEN);   // SCO_OK: handed to the game, not moved yet
//       else Warn("lift: %s", hangar.LastError().c_str());
//   }
//
// Game thread only, like the table. Every call answers sco_result and is noexcept. Nothing is owned:
// a lift request or an ATC cancel is one call into the game and nothing is undone at unload.
// Capabilities: has("game.hangar.lift"), has("game.hangar.atc_cancel"); both OFF by default (a call
// then answers SCO_UNAVAILABLE without touching the game). Reference: docs/game/hangar.md.
// GPL-3.0, like sco-core.
#ifndef SCOSDK_GAME_HANGAR_HPP
#define SCOSDK_GAME_HANGAR_HPP

#include "../plugin.hpp"
#include "sc_hangar.h"

#include <cstddef>
#include <string>

namespace sco::sdk::game {

class Hangar {
public:
    // Finds game.hangar 1.x. SCO_UNAVAILABLE on a 1.0 sco_api host; SCO_NOT_FOUND when the host has
    // no game pack (or the product didn't start its services). Both leave the Hangar empty.
    sco_result Open(const Plugin& plugin) noexcept { return Open(plugin.Api(), plugin.Self()); }
    // The same for code that holds the C handles (a C-style plugin, a test).
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        t_ = nullptr;
        self_ = self;
        if (!api || !self_) return SCO_BAD_ARG;
        if (!Covers(api->size, offsetof(sco_api, query_service)) || !api->query_service) return SCO_UNAVAILABLE;
        const void* table = nullptr;
        const sco_result r = api->query_service(SC_HANGAR_SERVICE_NAME, SC_HANGAR_SERVICE_VERSION, &table);
        if (r != SCO_OK) return r;
        if (!table) return SCO_UNAVAILABLE;
        t_ = static_cast<const sc_hangar_v1*>(table);
        return SCO_OK;
    }
    explicit operator bool() const noexcept { return t_ != nullptr; }
    const sc_hangar_v1* Table() const noexcept { return t_; }

    // lift = the ship lift serving the landing area landingAreaId; 0 on failure.
    // SCO_NOT_FOUND: the landing area isn't streamed in, or no lift goes with it. game.hangar.lift.
    sco_result LiftOfLandingArea(uint64_t landingAreaId, uint64_t& lift) const noexcept {
        lift = 0;
        if (!t_ || !Covers(t_->size, offsetof(sc_hangar_v1, lift_of_landing_area)) || !t_->lift_of_landing_area)
            return SCO_UNAVAILABLE;
        return t_->lift_of_landing_area(self_, landingAreaId, &lift);
    }
    // Asks the ship lift liftId to open or close. SCO_OK means the request was handed to the game,
    // not that the lift has moved. game.hangar.lift.
    sco_result LiftRequest(uint64_t liftId, sc_hangar_lift_op op) const noexcept {
        if (!t_ || !Covers(t_->size, offsetof(sc_hangar_v1, lift_request)) || !t_->lift_request)
            return SCO_UNAVAILABLE;
        return t_->lift_request(self_, liftId, static_cast<uint32_t>(op));
    }
    // Withdraws the local player's pending request at the air traffic controller atcId.
    // game.hangar.atc_cancel.
    sco_result AtcCancelRequest(uint64_t atcId) const noexcept {
        if (!t_ || !Covers(t_->size, offsetof(sc_hangar_v1, atc_cancel_request)) || !t_->atc_cancel_request)
            return SCO_UNAVAILABLE;
        return t_->atc_cancel_request(self_, atcId);
    }

    // The reason for your last failed call ("" if none or unavailable).
    std::string LastError() const noexcept {
        if (!t_ || !Covers(t_->size, offsetof(sc_hangar_v1, last_error)) || !t_->last_error) return {};
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
    const sc_hangar_v1* t_ = nullptr;
    sco_plugin* self_ = nullptr;
};

}  // namespace sco::sdk::game

#endif  // SCOSDK_GAME_HANGAR_HPP
