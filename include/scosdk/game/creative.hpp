// scosdk/game/creative.hpp: the game service "game.creative" (sc_creative.h) for C++ plugins.
// Header-only, over sco_api.h and sc_creative.h; a thin layer that passes self for you.
//
//   sco::sdk::game::Creative cr;
//   if (cr.Open(*this) == SCO_OK && Has("game.creative.god_mode")) {
//       if (cr.SetGodMode(true) != SCO_OK) {
//           char why[256];
//           cr.LastError(why);   // the reason, for your log or your UI
//       }
//   }
//
// Every call answers sco_result and is noexcept; on failure LastError has the reason. Game thread
// only (sc_creative.h). A toggle you switch on goes off when your plugin unloads. The game pack
// publishes the service before plugins load and withdraws it after they unload, so a Creative may
// be kept for the plugin's life.
// Reference: docs/api-v1.md (game.creative), docs/game/creative.md, docs/sdk-cpp.md. GPL-3.0,
// like sco-core.
#ifndef SCOSDK_GAME_CREATIVE_HPP
#define SCOSDK_GAME_CREATIVE_HPP

#include "../plugin.hpp"
#include "../service.hpp"
#include "sc_creative.h"

#include <cstdint>
#include <span>

namespace sco::sdk::game {

class Creative {
public:
    // SCO_OK: found (1.0 or a later 1.x). SCO_NOT_FOUND: no game pack, or the product didn't turn
    // the game services on. SCO_UNAVAILABLE: a 1.0 host.
    sco_result Open(const Plugin& plugin) noexcept { return Open(plugin.Api(), plugin.Self()); }
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        self_ = self;
        if (!api || !self_) return SCO_BAD_ARG;
        return ref_.Query(api, SC_CREATIVE_SERVICE_NAME, SC_CREATIVE_SERVICE_VERSION);
    }
    explicit operator bool() const noexcept { return static_cast<bool>(ref_); }
    const sc_creative_v1* Table() const noexcept { return ref_.Get(); }

    sco_result SetGodMode(bool on) const noexcept { return detail::CanCall(ref_.Get(), &sc_creative_v1::set_god_mode) ? ref_->set_god_mode(self_, on ? 1 : 0) : SCO_UNAVAILABLE; }
    sco_result SetFlyMode(bool on) const noexcept { return detail::CanCall(ref_.Get(), &sc_creative_v1::set_fly_mode) ? ref_->set_fly_mode(self_, on ? 1 : 0) : SCO_UNAVAILABLE; }
    // Metres per second, above 0 and at most SC_CREATIVE_FLY_SPEED_MAX.
    sco_result SetFlySpeed(float mps) const noexcept { return detail::CanCall(ref_.Get(), &sc_creative_v1::set_fly_speed) ? ref_->set_fly_speed(self_, mps) : SCO_UNAVAILABLE; }
    sco_result SetInfiniteAmmo(bool on) const noexcept { return detail::CanCall(ref_.Get(), &sc_creative_v1::set_infinite_ammo) ? ref_->set_infinite_ammo(self_, on ? 1 : 0) : SCO_UNAVAILABLE; }
    sco_result SetInfiniteShipAmmo(bool on) const noexcept {
        return detail::CanCall(ref_.Get(), &sc_creative_v1::set_infinite_ship_ammo) ? ref_->set_infinite_ship_ammo(self_, on ? 1 : 0) : SCO_UNAVAILABLE;
    }
    // The sc_creative_flag bits that are on, whoever switched them on.
    sco_result State(uint32_t& flags) const noexcept {
        flags = 0;
        return detail::CanCall(ref_.Get(), &sc_creative_v1::state) ? ref_->state(self_, &flags) : SCO_UNAVAILABLE;
    }

    // The reason for your last failed call into out, NUL-terminated ("" when none). SCO_TOO_MANY:
    // out is too small and stays ""; 256 bytes always fit the game pack's reasons.
    sco_result LastError(std::span<char> out) const noexcept {
        if (out.empty()) return SCO_BAD_ARG;
        out[0] = '\0';
        if (!detail::CanCall(ref_.Get(), &sc_creative_v1::last_error)) return SCO_UNAVAILABLE;
        uint32_t size = static_cast<uint32_t>(out.size());
        return ref_->last_error(self_, out.data(), &size);
    }

private:
    ServiceRef<sc_creative_v1> ref_;
    sco_plugin* self_ = nullptr;
};

}  // namespace sco::sdk::game

#endif  // SCOSDK_GAME_CREATIVE_HPP
